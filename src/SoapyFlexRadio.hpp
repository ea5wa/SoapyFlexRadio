#pragma once
/**
 * SoapyFlexRadio.hpp
 * Plugin SoapySDR para FlexRadio 6600 via smartsdr-iqtransfer
 *
 * Este plugin NO habla directamente con el radio.
 * Recibe los datos IQ que smartsdr-iqtransfer envía por UDP (Float32 LE raw).
 *
 * Flujo:
 *   SkyRoof → SoapySDR API → este plugin (UDP:5900) ← smartsdr-iqtransfer ← FlexRadio 6600
 *
 * Formato de datos entrantes (smartsdr-iqtransfer --FWD):
 *   Float32 little-endian, interleaved I,Q,I,Q,...
 *   Sin cabecera, datos puros.
 */

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Formats.hpp>

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define INVALID_SOCK  INVALID_SOCKET
  #define CLOSE_SOCK(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCK  (-1)
  #define CLOSE_SOCK(s) ::close(s)
#endif

// Tamaño del buffer circular en muestras CF32 (I+Q = 2 floats por muestra)
static constexpr size_t CIRC_BUF_SAMPLES = 262144; // 256k muestras

class SoapyFlexRadio : public SoapySDR::Device
{
public:
    explicit SoapyFlexRadio(const SoapySDR::Kwargs &args);
    ~SoapyFlexRadio() override;

    // ── Identificación ────────────────────────────────────────────────────────
    std::string getDriverKey()   const override { return "FlexRadio"; }
    std::string getHardwareKey() const override { return "FLEX-6600"; }
    SoapySDR::Kwargs getHardwareInfo() const override;

    // ── Canales ───────────────────────────────────────────────────────────────
    size_t getNumChannels(const int dir) const override;
    bool   getFullDuplex(const int, const size_t) const override { return false; }

    // ── Streams ───────────────────────────────────────────────────────────────
    SoapySDR::Stream* setupStream(const int dir,
                                  const std::string &fmt,
                                  const std::vector<size_t> &chs,
                                  const SoapySDR::Kwargs &args) override;
    void   closeStream(SoapySDR::Stream *stream) override;
    size_t getStreamMTU(SoapySDR::Stream *stream) const override;
    int    activateStream(SoapySDR::Stream *stream,
                          const int flags = 0,
                          const long long timeNs = 0,
                          const size_t numElems = 0) override;
    int    deactivateStream(SoapySDR::Stream *stream,
                            const int flags = 0,
                            const long long timeNs = 0) override;
    int    readStream(SoapySDR::Stream *stream,
                      void * const *buffs,
                      const size_t numElems,
                      int &flags,
                      long long &timeNs,
                      const long timeoutUs = 100000) override;

    // ── Frecuencia ────────────────────────────────────────────────────────────
    void   setFrequency(int dir, size_t ch, const std::string &name,
                        double freq, const SoapySDR::Kwargs &args) override;
    double getFrequency(int dir, size_t ch, const std::string &name) const override;
    std::vector<std::string> listFrequencies(int dir, size_t ch) const override;
    SoapySDR::RangeList getFrequencyRange(int dir, size_t ch,
                                          const std::string &name) const override;

    // ── Sample rate ───────────────────────────────────────────────────────────
    void   setSampleRate(int dir, size_t ch, double rate) override;
    double getSampleRate(int dir, size_t ch) const override;
    std::vector<double> listSampleRates(int dir, size_t ch) const override;
    SoapySDR::RangeList getSampleRateRange(int dir, size_t ch) const override;

    // ── Ganancia ──────────────────────────────────────────────────────────────
    void   setGain(int dir, size_t ch, double gain) override;
    double getGain(int dir, size_t ch) const override;
    SoapySDR::Range getGainRange(int dir, size_t ch) const override;

    // ── Ancho de banda ────────────────────────────────────────────────────────
    void   setBandwidth(int dir, size_t ch, double bw) override;
    double getBandwidth(int dir, size_t ch) const override;
    SoapySDR::RangeList getBandwidthRange(int dir, size_t ch) const override;

    // ── Antenas ───────────────────────────────────────────────────────────────
    std::vector<std::string> listAntennas(int dir, size_t ch) const override;
    void        setAntenna(int dir, size_t ch, const std::string &name) override;
    std::string getAntenna(int dir, size_t ch) const override;

private:
    bool openUDP();
    void udpRxThread();

    // Parámetros de configuración
    std::string _udpHost;   // IP donde escucha este plugin (normalmente 0.0.0.0)
    uint16_t    _udpPort;   // Puerto UDP local (debe coincidir con --FWD de iqtransfer)
    double      _centerFreq;
    double      _sampleRate;
    double      _bandwidth;
    double      _gain;
    std::string _antenna;

    // Socket UDP
    sock_t _sock = INVALID_SOCK;

    // Socket TCP persistente para CAT (control de frecuencia)
    sock_t _catSock = INVALID_SOCK;
    bool   connectCAT();
    void   sendCAT(const std::string &cmd);

    // Socket TCP para SmartSDR API (mover centro del panadaptador)
    sock_t   _sdrSock  = INVALID_SOCK;
    uint32_t _seqNo    = 1;
    std::string _radioHost;
    bool   connectSDR();
    void   sendSDR(const std::string &cmd);

    // Buffer circular CF32 (floats interleaved I,Q)
    std::vector<float> _buf;
    size_t _wPos = 0;   // posición de escritura
    size_t _rPos = 0;   // posición de lectura
    size_t _avail = 0;  // muestras CF32 disponibles (pares I+Q)
    std::mutex _bufMtx;
    std::condition_variable _bufCv;

    // Hilo receptor
    std::thread      _rxThread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _streaming{false};
};
