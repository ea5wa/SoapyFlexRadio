#!/usr/bin/env python3
"""
cat_proxy.py
Proxy CAT para SkyRoof + FlexRadio 6600

Intercepta los comandos CAT Kenwood que SkyRoof envía para corrección Doppler
y los reenvía a SmartSDR CAT, añadiendo además el comando SmartSDR API para
mover el centro del panadaptador.

Flujo:
    SkyRoof → TCP:60010 → este proxy → SmartSDR CAT TCP:60001
                                     → SmartSDR API TCP:4992 (pan center)

Uso:
    python cat_proxy.py
    python cat_proxy.py --radio 192.168.0.208 --listen 60010 --cat 60001

Autor: EA5WA / Claude (Anthropic)
"""

import socket
import threading
import argparse
import logging
import re
import time

# ── Configuración por defecto ─────────────────────────────────────────────────
DEFAULT_LISTEN_PORT = 60010      # Puerto donde escucha el proxy (SkyRoof conecta aquí)
DEFAULT_CAT_PORT    = 60001      # Puerto SmartSDR CAT
DEFAULT_RADIO_IP    = "192.168.0.208"  # IP del FlexRadio
DEFAULT_SDR_PORT    = 4992       # Puerto SmartSDR API
DEFAULT_PAN_ID      = "0x40000000"     # ID del panadaptador principal

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("cat_proxy")

# ── Conexión persistente a SmartSDR API (para mover el panadaptador) ──────────
class SmartSDRConn:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.seq  = 1
        self.lock = threading.Lock()

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(3)
            self.sock.connect((self.host, self.port))
            log.info(f"Conectado a SmartSDR API {self.host}:{self.port}")
            return True
        except Exception as e:
            log.warning(f"No se pudo conectar a SmartSDR API: {e}")
            self.sock = None
            return False

    def send(self, cmd):
        with self.lock:
            if self.sock is None:
                self.connect()
            if self.sock is None:
                return
            try:
                msg = f"C{self.seq}|{cmd}\n"
                self.seq += 1
                self.sock.sendall(msg.encode())
                log.debug(f"SDR→ {msg.strip()}")
            except Exception as e:
                log.warning(f"Error enviando a SmartSDR API: {e} — reconectando")
                self.sock = None
                if self.connect():
                    try:
                        msg = f"C{self.seq}|{cmd}\n"
                        self.seq += 1
                        self.sock.sendall(msg.encode())
                    except:
                        pass

    def set_pan_center(self, freq_hz, pan_id):
        freq_mhz = freq_hz / 1e6
        cmd = f"display pan set {pan_id} center={freq_mhz:.6f}"
        self.send(cmd)
        log.info(f"Pan center → {freq_mhz:.6f} MHz")

# ── Hilo que maneja una conexión de SkyRoof ───────────────────────────────────
class ClientHandler(threading.Thread):
    def __init__(self, client_sock, client_addr, cat_host, cat_port, sdr_conn, pan_id):
        super().__init__(daemon=True)
        self.client     = client_sock
        self.addr       = client_addr
        self.cat_host   = cat_host
        self.cat_port   = cat_port
        self.sdr_conn   = sdr_conn
        self.pan_id     = pan_id
        self.cat_sock   = None

    def connect_cat(self):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(3)
            s.connect((self.cat_host, self.cat_port))
            log.info(f"Conectado a SmartSDR CAT {self.cat_host}:{self.cat_port}")
            return s
        except Exception as e:
            log.error(f"No se pudo conectar a SmartSDR CAT: {e}")
            return None

    def run(self):
        log.info(f"SkyRoof conectado desde {self.addr}")
        self.cat_sock = self.connect_cat()
        if not self.cat_sock:
            self.client.close()
            return

        # Leer banner inicial de SmartSDR CAT y reenviarlo a SkyRoof
        try:
            self.cat_sock.settimeout(0.5)
            banner = self.cat_sock.recv(4096)
            if banner:
                self.client.sendall(banner)
                log.debug(f"Banner CAT→SkyRoof: {banner.decode(errors='replace').strip()}")
        except:
            pass
        finally:
            self.cat_sock.settimeout(None)

        # Hilo para reenviar respuestas de SmartSDR CAT → SkyRoof
        def cat_to_client():
            try:
                while True:
                    data = self.cat_sock.recv(4096)
                    if not data:
                        break
                    self.client.sendall(data)
                    log.debug(f"CAT→SkyRoof: {data.decode(errors='replace').strip()}")
            except:
                pass

        t = threading.Thread(target=cat_to_client, daemon=True)
        t.start()

        # Leer comandos de SkyRoof y procesarlos
        buf = ""
        try:
            while True:
                data = self.client.recv(4096)
                if not data:
                    break
                buf += data.decode(errors="replace")

                # Procesar comandos completos (terminados en ;)
                while ";" in buf:
                    idx = buf.index(";") + 1
                    cmd = buf[:idx]
                    buf = buf[idx:]

                    log.debug(f"SkyRoof→ {cmd.strip()}")

                    # Reenviar siempre a SmartSDR CAT
                    try:
                        self.cat_sock.sendall(cmd.encode())
                    except Exception as e:
                        log.warning(f"Error reenviando a CAT: {e}")
                        self.cat_sock = self.connect_cat()
                        if self.cat_sock:
                            self.cat_sock.sendall(cmd.encode())

                    # Si es un comando FA (set frequency), mover también el panadaptador
                    m = re.match(r"FA(\d{11});", cmd.strip())
                    if m:
                        freq_hz = int(m.group(1))
                        log.info(f"Doppler → {freq_hz/1e6:.6f} MHz")
                        self.sdr_conn.set_pan_center(freq_hz, self.pan_id)

        except Exception as e:
            log.warning(f"Conexión con SkyRoof cerrada: {e}")
        finally:
            log.info(f"SkyRoof desconectado desde {self.addr}")
            self.client.close()
            if self.cat_sock:
                self.cat_sock.close()

# ── Servidor proxy ────────────────────────────────────────────────────────────
def run_proxy(listen_port, cat_host, cat_port, radio_ip, sdr_port, pan_id):
    sdr_conn = SmartSDRConn(radio_ip, sdr_port)
    sdr_conn.connect()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", listen_port))
    server.listen(5)

    log.info("=" * 50)
    log.info("  CAT Proxy para SkyRoof + FlexRadio 6600")
    log.info("=" * 50)
    log.info(f"  Escuchando en        : 0.0.0.0:{listen_port}")
    log.info(f"  SmartSDR CAT         : {cat_host}:{cat_port}")
    log.info(f"  SmartSDR API         : {radio_ip}:{sdr_port}")
    log.info(f"  Panadaptador         : {pan_id}")
    log.info("=" * 50)
    log.info("Configura SkyRoof CAT → Host: 127.0.0.1, Port: {listen_port}")
    log.info("Esperando conexión de SkyRoof...")

    while True:
        try:
            client_sock, client_addr = server.accept()
            handler = ClientHandler(
                client_sock, client_addr,
                cat_host, cat_port,
                sdr_conn, pan_id
            )
            handler.start()
        except KeyboardInterrupt:
            log.info("Proxy detenido.")
            break
        except Exception as e:
            log.error(f"Error en servidor: {e}")
            time.sleep(1)

    server.close()

# ── Main ──────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="CAT Proxy para SkyRoof + FlexRadio 6600"
    )
    parser.add_argument("--listen", type=int, default=DEFAULT_LISTEN_PORT,
                        help=f"Puerto donde escucha el proxy (defecto: {DEFAULT_LISTEN_PORT})")
    parser.add_argument("--cat",    type=int, default=DEFAULT_CAT_PORT,
                        help=f"Puerto SmartSDR CAT (defecto: {DEFAULT_CAT_PORT})")
    parser.add_argument("--radio",  default=DEFAULT_RADIO_IP,
                        help=f"IP del FlexRadio (defecto: {DEFAULT_RADIO_IP})")
    parser.add_argument("--sdrport",type=int, default=DEFAULT_SDR_PORT,
                        help=f"Puerto SmartSDR API (defecto: {DEFAULT_SDR_PORT})")
    parser.add_argument("--pan",    default=DEFAULT_PAN_ID,
                        help=f"ID panadaptador (defecto: {DEFAULT_PAN_ID})")
    parser.add_argument("--debug",  action="store_true",
                        help="Mostrar todos los comandos CAT")
    args = parser.parse_args()

    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)

    run_proxy(args.listen, "127.0.0.1", args.cat,
              args.radio, args.sdrport, args.pan)
