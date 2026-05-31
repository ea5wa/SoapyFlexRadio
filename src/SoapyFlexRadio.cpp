/**
 * SoapyFlexRadio.cpp
 * Plugin SoapySDR simplificado para FlexRadio 6600.
 *
 * Recibe IQ de smartsdr-iqtransfer via UDP (Float32 LE, interleaved I,Q).
 * No implementa el protocolo SmartSDR directamente.
 */

#include "SoapyFlexRadio.hpp"
#include <SoapySDR/Logger.hpp>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <fstream>
#include <ctime>

// ─── Log a archivo para diagnóstico ──────────────────────────────────────────
static void flog(const std::string &msg)
{
    std::ofstream f("C:\\Users\\Public\\SoapyFlexRadio_debug.txt", std::ios::app);
    if (!f) return;
    std::time_t t = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t));
    f << "[" << ts << "] " << msg << "\n";
    f.flush();
}

#ifdef _WIN32
static void initWinsock() {
    static bool done = false;
    if (!done) { WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd); done = true; }
}
#else
static void initWinsock() {}
#endif

// ─── Constructor ──────────────────────────────────────────────────────────────

SoapyFlexRadio::SoapyFlexRadio(const SoapySDR::Kwargs &args)
{
    initWinsock();

    // Puerto UDP donde escuchamos (debe coincidir con --FWD de smartsdr-iqtransfer)
    _udpPort    = args.count("udp_port") ? (uint16_t)std::stoi(args.at("udp_port")) : 5901;
    _udpHost    = args.count("udp_host") ? args.at("udp_host") : "0.0.0.0";
    _radioHost  = args.count("host")     ? args.at("host")     : "192.168.0.208";
    _centerFreq = args.count("freq")     ? std::stod(args.at("freq"))   : 14200000.0;
    _sampleRate = args.count("rate")     ? std::stod(args.at("rate"))   : 192000.0;
    _bandwidth  = _sampleRate;
    _gain       = 0.0;
    _antenna    = "ANT1";

    // Reservar buffer circular (I+Q = 2 floats por muestra)
    _buf.resize(CIRC_BUF_SAMPLES * 2, 0.0f);

    flog("Constructor: puerto=" + std::to_string(_udpPort));

    // Abrir socket UDP ya en el constructor y arrancar hilo receptor
    if (openUDP()) {
        flog("Constructor: socket UDP abierto OK en puerto " + std::to_string(_udpPort));
        _running   = true;
        _streaming = true;
        _rxThread  = std::thread(&SoapyFlexRadio::udpRxThread, this);
    } else {
        flog("Constructor: ERROR abriendo socket UDP puerto " + std::to_string(_udpPort));
    }

    SoapySDR_logf(SOAPY_SDR_INFO,
        "SoapyFlexRadio: iniciado en UDP puerto %d", _udpPort);
}

// ─── Destructor ───────────────────────────────────────────────────────────────

SoapyFlexRadio::~SoapyFlexRadio()
{
    flog("Destructor: cerrando plugin");
    _running   = false;
    _streaming = false;
    _bufCv.notify_all();

    if (_rxThread.joinable()) _rxThread.join();
    if (_sock != INVALID_SOCK) {
        flog("Destructor: cerrando socket UDP");
        CLOSE_SOCK(_sock);
        _sock = INVALID_SOCK;
    }
    if (_catSock != INVALID_SOCK) {
        CLOSE_SOCK(_catSock);
        _catSock = INVALID_SOCK;
    }
    if (_sdrSock != INVALID_SOCK) {
        CLOSE_SOCK(_sdrSock);
        _sdrSock = INVALID_SOCK;
    }
    flog("Destructor: completado");
}

// ─── Abrir socket UDP ─────────────────────────────────────────────────────────

bool SoapyFlexRadio::openUDP()
{
    _sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_sock == INVALID_SOCK) {
        flog("openUDP: ERROR creando socket err=" + std::to_string(WSAGetLastError()));
        return false;
    }

    // Reusar dirección
    int reuse = 1;
    setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    // Buffer de recepción amplio (8 MB)
    int rcvbuf = 8 * 1024 * 1024;
    setsockopt(_sock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, sizeof(rcvbuf));

    // Timeout de recepción: 200 ms para no bloquear al parar
#ifdef _WIN32
    DWORD tv = 200;
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
#else
    struct timeval tv{0, 200000};
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(_udpPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        int err = WSAGetLastError();
        flog("openUDP: ERROR bind puerto=" + std::to_string(_udpPort) + " WSAerr=" + std::to_string(err));
        CLOSE_SOCK(_sock);
        _sock = INVALID_SOCK;
        return false;
    }
    flog("openUDP: bind OK en puerto " + std::to_string(_udpPort));
    return true;
}

// ─── Hilo receptor UDP ────────────────────────────────────────────────────────
//
// smartsdr-iqtransfer envía paquetes UDP con floats F32 little-endian
// interleaved: I0, Q0, I1, Q1, ...
// En Windows el sistema ya es little-endian, no hay que convertir nada.

void SoapyFlexRadio::udpRxThread()
{
    static constexpr size_t MAX_PKT = 65507;
    std::vector<uint8_t> pkt(MAX_PKT);

    while (_running) {
        int n = recv(_sock, (char*)pkt.data(), (int)pkt.size(), 0);
        if (n <= 0) continue;
        if (!_streaming) continue;

        // Número de floats — debe ser par (I,Q)
        size_t nFloats = (size_t)n / sizeof(float);
        if (nFloats < 2) continue;
        nFloats &= ~1UL; // forzar par

        const float *samples = reinterpret_cast<const float*>(pkt.data());
        size_t nSamples = nFloats / 2; // muestras CF32

        std::lock_guard<std::mutex> lk(_bufMtx);
        size_t bufSize = _buf.size(); // CIRC_BUF_SAMPLES * 2

        for (size_t i = 0; i < nFloats; i++) {
            _buf[_wPos] = samples[i];
            _wPos = (_wPos + 1) % bufSize;
        }

        if (_avail + nSamples <= CIRC_BUF_SAMPLES) {
            _avail += nSamples;
        } else {
            // Buffer lleno: descartar las muestras más antiguas
            size_t overflow = (_avail + nSamples) - CIRC_BUF_SAMPLES;
            _rPos = (_rPos + overflow * 2) % bufSize;
            _avail = CIRC_BUF_SAMPLES;
        }
        _bufCv.notify_one();
    }
}

// ─── Info del hardware ────────────────────────────────────────────────────────

SoapySDR::Kwargs SoapyFlexRadio::getHardwareInfo() const
{
    SoapySDR::Kwargs info;
    info["backend"]  = "smartsdr-iqtransfer";
    info["udp_port"] = std::to_string(_udpPort);
    info["model"]    = "FLEX-6600";
    return info;
}

// ─── Canales ──────────────────────────────────────────────────────────────────

size_t SoapyFlexRadio::getNumChannels(const int dir) const
{
    return (dir == SOAPY_SDR_RX) ? 1 : 0;
}

// ─── Setup / Close / MTU de stream ───────────────────────────────────────────

SoapySDR::Stream* SoapyFlexRadio::setupStream(
    const int dir,
    const std::string &fmt,
    const std::vector<size_t> &,
    const SoapySDR::Kwargs &)
{
    if (dir != SOAPY_SDR_RX) {
        flog("setupStream: ERROR dir != RX");
        throw std::runtime_error("SoapyFlexRadio: solo RX soportado");
    }
    if (fmt != SOAPY_SDR_CF32) {
        flog("setupStream: ERROR formato=" + fmt);
        throw std::runtime_error("SoapyFlexRadio: solo formato CF32 soportado");
    }
    flog("setupStream: OK formato=" + fmt);
    return reinterpret_cast<SoapySDR::Stream*>(this);
}

void SoapyFlexRadio::closeStream(SoapySDR::Stream *stream)
{
    deactivateStream(stream);
}

size_t SoapyFlexRadio::getStreamMTU(SoapySDR::Stream *) const
{
    return 1024; // muestras CF32 por llamada recomendada
}

// ─── Activar / Desactivar stream ─────────────────────────────────────────────

int SoapyFlexRadio::activateStream(SoapySDR::Stream *, const int,
                                    const long long, const size_t)
{
    flog("activateStream: inicio, socket=" + std::to_string(_sock != INVALID_SOCK));

    // Si el socket ya está abierto desde el constructor, reutilizarlo
    if (_sock == INVALID_SOCK) {
        bool ok = false;
        for (int i = 0; i < 5; i++) {
            if (openUDP()) { ok = true; break; }
            flog("activateStream: reintento " + std::to_string(i+1) + "/5");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (!ok) {
            flog("activateStream: ERROR no se pudo abrir UDP:" + std::to_string(_udpPort));
            return SOAPY_SDR_STREAM_ERROR;
        }
    }

    // Limpiar buffer
    {
        std::lock_guard<std::mutex> lk(_bufMtx);
        _wPos = _rPos = _avail = 0;
    }

    // Arrancar hilo receptor si no está corriendo
    if (!_running) {
        _running  = true;
        _rxThread = std::thread(&SoapyFlexRadio::udpRxThread, this);
    }

    _streaming = true;
    flog("activateStream: OK, escuchando UDP:" + std::to_string(_udpPort));
    SoapySDR_logf(SOAPY_SDR_INFO,
        "SoapyFlexRadio: stream activado, escuchando UDP:%d", _udpPort);
    return 0;
}

int SoapyFlexRadio::deactivateStream(SoapySDR::Stream *, const int, const long long)
{
    _streaming = false;
    _running   = false;
    _bufCv.notify_all();

    // Cerrar socket para desbloquear el recv() en el hilo
    if (_sock != INVALID_SOCK) {
        CLOSE_SOCK(_sock);
        _sock = INVALID_SOCK;
    }

    if (_rxThread.joinable()) _rxThread.join();

    SoapySDR_log(SOAPY_SDR_INFO, "SoapyFlexRadio: stream desactivado");
    return 0;
}

// ─── readStream ───────────────────────────────────────────────────────────────
//
// SkyRoof llama a esta función para obtener muestras IQ.
// Copiamos del buffer circular al buffer de salida.

int SoapyFlexRadio::readStream(
    SoapySDR::Stream *,
    void * const *buffs,
    const size_t numElems,
    int &flags,
    long long &timeNs,
    const long timeoutUs)
{
    float *out = reinterpret_cast<float*>(buffs[0]);

    std::unique_lock<std::mutex> lk(_bufMtx);

    // Esperar hasta tener datos o timeout
    bool ok = _bufCv.wait_for(lk,
        std::chrono::microseconds(timeoutUs),
        [&]{ return _avail >= numElems || !_running; });

    if (!ok || !_running) return SOAPY_SDR_TIMEOUT;
    if (_avail == 0)      return SOAPY_SDR_TIMEOUT;

    // Cuántas muestras CF32 podemos entregar
    size_t toRead   = std::min(numElems, _avail);
    size_t toRead2  = toRead * 2; // floats (I+Q)
    size_t bufSize  = _buf.size();

    for (size_t i = 0; i < toRead2; i++) {
        out[i] = _buf[_rPos];
        _rPos  = (_rPos + 1) % bufSize;
    }
    _avail -= toRead;

    flags  = 0;
    timeNs = 0;
    return (int)toRead;
}

// ─── Frecuencia ───────────────────────────────────────────────────────────────
// Nota: la frecuencia real la controla smartsdr-iqtransfer / SmartSDR.
// Aquí solo almacenamos el valor para que SkyRoof sepa en qué frecuencia estamos.

bool SoapyFlexRadio::connectSDR()
{
    if (_sdrSock != INVALID_SOCK) {
        CLOSE_SOCK(_sdrSock);
        _sdrSock = INVALID_SOCK;
    }
    _sdrSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_sdrSock == INVALID_SOCK) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(4992);
    inet_pton(AF_INET, _radioHost.c_str(), &addr.sin_addr);

    if (connect(_sdrSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCK(_sdrSock);
        _sdrSock = INVALID_SOCK;
        flog("connectSDR: ERROR conectando a " + _radioHost + ":4992");
        return false;
    }
    flog("connectSDR: conectado a SmartSDR " + _radioHost + ":4992");
    return true;
}

void SoapyFlexRadio::sendSDR(const std::string &cmd)
{
    if (_sdrSock == INVALID_SOCK) {
        if (!connectSDR()) return;
    }
    std::string full = "C" + std::to_string(_seqNo++) + "|" + cmd + "\n";
    int n = send(_sdrSock, full.c_str(), (int)full.size(), 0);
    if (n <= 0) {
        flog("sendSDR: error enviando, reconectando...");
        if (connectSDR())
            send(_sdrSock, full.c_str(), (int)full.size(), 0);
    }
}

bool SoapyFlexRadio::connectCAT()
{
    if (_catSock != INVALID_SOCK) {
        CLOSE_SOCK(_catSock);
        _catSock = INVALID_SOCK;
    }
    _catSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_catSock == INVALID_SOCK) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(60001);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(_catSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCK(_catSock);
        _catSock = INVALID_SOCK;
        flog("connectCAT: ERROR conectando a puerto 60001");
        return false;
    }
    flog("connectCAT: conectado a SmartSDR CAT puerto 60001");
    return true;
}

void SoapyFlexRadio::sendCAT(const std::string &cmd)
{
    // Si no hay conexión, intentar conectar
    if (_catSock == INVALID_SOCK) {
        if (!connectCAT()) return;
    }
    int n = send(_catSock, cmd.c_str(), (int)cmd.size(), 0);
    if (n <= 0) {
        flog("sendCAT: error enviando, reconectando...");
        if (connectCAT())
            send(_catSock, cmd.c_str(), (int)cmd.size(), 0);
    }
}

/* void SoapyFlexRadio::setFrequency(int, size_t, const std::string &name,
                                   double freq, const SoapySDR::Kwargs &)
{
    _centerFreq = freq;
    flog("setFrequency: name=" + name + " freq=" + std::to_string((long long)freq));

    // 1. Mover el slice via CAT (Kenwood FA)
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "FA%011lld;", (long long)freq);
    sendCAT(std::string(cmd));

    // 2. Mover el centro del panadaptador via SmartSDR API
    // Formato: display pan set 0x40000000 center=<MHz>
    double freqMHz = freq / 1e6;
    char panCmd[128];
    snprintf(panCmd, sizeof(panCmd),
        "display pan set 0x40000000 center=%.6f", freqMHz);
    sendSDR(std::string(panCmd));

    flog("setFrequency: CAT=" + std::string(cmd) +
         " pan center=" + std::to_string(freqMHz) + " MHz");
} */

void SoapyFlexRadio::setFrequency(int, size_t, const std::string &name,
                                   double freq, const SoapySDR::Kwargs &)
{
    _centerFreq = freq;
    flog("setFrequency: name=" + name + " freq=" + std::to_string((long long)freq));

    // 1. Mover el slice via CAT (Kenwood FA)
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "FA%011lld;", (long long)freq);
    sendCAT(std::string(cmd));

    // 2. Mover el centro del panadaptador via SmartSDR API
    double freqMHz = freq / 1e6;
    char panCmd[128];
    snprintf(panCmd, sizeof(panCmd),
        "display pan set 0x40000000 center=%.6f", freqMHz);
    sendSDR(std::string(panCmd));

    flog("setFrequency: CAT=" + std::string(cmd) +
         " pan center=" + std::to_string(freqMHz) + " MHz");
}

double SoapyFlexRadio::getFrequency(int, size_t, const std::string &) const
{
    return _centerFreq;
}

std::vector<std::string> SoapyFlexRadio::listFrequencies(int, size_t) const
{
    return {"RF"};
}

/* SoapySDR::RangeList SoapyFlexRadio::getFrequencyRange(int, size_t,
                                                       const std::string &) const
{
    // Rango amplio: 10 kHz – 6 GHz para cubrir HF + VHF/UHF via transverter
    return {{10000.0, 6000000000.0, 1.0}};
} */

SoapySDR::RangeList SoapyFlexRadio::getFrequencyRange(
    int, size_t, const std::string &name) const
{
    // SkyRoof solo activa Doppler si el rango corresponde al nombre "RF"
    if (name == "RF")
        return { SoapySDR::Range(10000.0, 6000000000.0) }; // 10 kHz – 6 GHz

    return {};
}

// ─── Sample rate ─────────────────────────────────────────────────────────────

void SoapyFlexRadio::setSampleRate(int, size_t, double rate)
{
    const double valid[] = {24000, 48000, 96000, 192000};
    double best = valid[0];
    for (double v : valid)
        if (std::abs(rate - v) < std::abs(rate - best)) best = v;

    _sampleRate = best;
    _bandwidth  = best;
    flog("setSampleRate: SkyRoof pidio=" + std::to_string((int)rate) + " usando=" + std::to_string((int)best));
}

double SoapyFlexRadio::getSampleRate(int, size_t) const { return _sampleRate; }

std::vector<double> SoapyFlexRadio::listSampleRates(int, size_t) const
{
    return {24000, 48000, 96000, 192000};
}

SoapySDR::RangeList SoapyFlexRadio::getSampleRateRange(int, size_t) const
{
    return {{24000.0, 192000.0, 0.0}};
}

// ─── Ganancia ─────────────────────────────────────────────────────────────────

void SoapyFlexRadio::setGain(int, size_t, double gain)
{
    _gain = std::max(0.0, std::min(gain, 100.0));
}
double SoapyFlexRadio::getGain(int, size_t) const { return _gain; }
SoapySDR::Range SoapyFlexRadio::getGainRange(int, size_t) const { return {0.0, 100.0, 1.0}; }

// ─── Ancho de banda ───────────────────────────────────────────────────────────

void SoapyFlexRadio::setBandwidth(int, size_t, double bw)
{
    _bandwidth = bw;
    flog("setBandwidth: bw=" + std::to_string((int)bw));
}
double SoapyFlexRadio::getBandwidth(int, size_t) const { return _bandwidth; }
SoapySDR::RangeList SoapyFlexRadio::getBandwidthRange(int, size_t) const
{
    return {{24000.0, 192000.0, 0.0}};
}

// ─── Antenas ─────────────────────────────────────────────────────────────────

std::vector<std::string> SoapyFlexRadio::listAntennas(int, size_t) const
{
    return {"ANT1", "ANT2", "RX_A", "RX_B", "XVTA", "XVTB"};
}
void SoapyFlexRadio::setAntenna(int, size_t, const std::string &name) { _antenna = name; }
std::string SoapyFlexRadio::getAntenna(int, size_t) const { return _antenna; }
