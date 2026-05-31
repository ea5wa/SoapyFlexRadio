#!/usr/bin/env python3
"""
cat_proxy.py
Proxy CAT para SkyRoof + FlexRadio 6600

Mantiene una conexión ÚNICA y persistente a SmartSDR CAT compartida entre
todas las sesiones de SkyRoof. Esto evita el error de SmartSDR CAT cuando
rechaza una segunda conexión mientras la anterior sigue activa.

Flujo:
    SkyRoof → TCP:60010 → este proxy ──→ SmartSDR CAT TCP:60001 (1 sola conexión)
                                     └──→ SmartSDR API TCP:4992 (pan center)

Autor: EA5WA / Claude (Anthropic)
"""

import socket
import threading
import argparse
import logging
import re
import time
import queue

DEFAULT_LISTEN_PORT = 60010
DEFAULT_CAT_PORT    = 60001
DEFAULT_RADIO_IP    = "192.168.0.208"
DEFAULT_SDR_PORT    = 4992
DEFAULT_PAN_ID      = "0x40000000"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("cat_proxy")


# ── Conexión persistente a SmartSDR CAT (única, compartida) ───────────────────
class CATConn:
    """Una sola conexión TCP a SmartSDR CAT reutilizada por todos los clientes."""
    def __init__(self, host, port):
        self.host  = host
        self.port  = port
        self.sock  = None
        self.lock  = threading.Lock()
        self._subscribers = []  # clientes esperando respuestas
        self._sub_lock = threading.Lock()

    def connect(self):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect((self.host, self.port))
            s.settimeout(None)
            self.sock = s
            log.info(f"Conectado a SmartSDR CAT {self.host}:{self.port}")
            # Arrancar hilo lector de respuestas
            t = threading.Thread(target=self._reader, daemon=True)
            t.start()
            return True
        except Exception as e:
            log.error(f"No se pudo conectar a SmartSDR CAT: {e}")
            self.sock = None
            return False

    def _reader(self):
        """Lee respuestas de SmartSDR CAT y las reenvía a los clientes suscritos."""
        buf = b""
        while self.sock:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                buf += data
                log.debug(f"CAT← {data.decode(errors='replace').strip()}")
                # Reenviar a todos los clientes conectados
                with self._sub_lock:
                    dead = []
                    for client in self._subscribers:
                        try:
                            client.sendall(data)
                        except:
                            dead.append(client)
                    for d in dead:
                        self._subscribers.remove(d)
            except Exception as e:
                log.warning(f"Lector CAT: {e}")
                break
        log.warning("Conexión con SmartSDR CAT cerrada")
        self.sock = None

    def send(self, cmd):
        """Envía un comando a SmartSDR CAT, reconectando si es necesario."""
        with self.lock:
            if self.sock is None:
                log.info("Reconectando a SmartSDR CAT...")
                if not self.connect():
                    return False
            try:
                self.sock.sendall(cmd.encode() if isinstance(cmd, str) else cmd)
                log.debug(f"CAT→ {cmd.strip() if isinstance(cmd, str) else cmd}")
                return True
            except Exception as e:
                log.warning(f"Error enviando a CAT: {e} — reconectando")
                self.sock = None
                if self.connect():
                    try:
                        self.sock.sendall(cmd.encode() if isinstance(cmd, str) else cmd)
                        return True
                    except:
                        return False
                return False

    def subscribe(self, client_sock):
        """Añade un cliente para recibir respuestas de SmartSDR CAT."""
        with self._sub_lock:
            if client_sock not in self._subscribers:
                self._subscribers.append(client_sock)

    def unsubscribe(self, client_sock):
        """Elimina un cliente de la lista de suscriptores."""
        with self._sub_lock:
            if client_sock in self._subscribers:
                self._subscribers.remove(client_sock)


# ── Conexión persistente a SmartSDR API (panadaptador) ────────────────────────
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
            self.sock.settimeout(None)
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
            except Exception as e:
                log.warning(f"Error enviando a SmartSDR API: {e}")
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
        self.send(f"display pan set {pan_id} center={freq_mhz:.6f}")
        log.info(f"Pan center → {freq_mhz:.6f} MHz")


# ── Hilo que maneja una conexión de SkyRoof ───────────────────────────────────
class ClientHandler(threading.Thread):
    def __init__(self, client_sock, client_addr, cat_conn, sdr_conn, pan_id):
        super().__init__(daemon=True)
        self.client   = client_sock
        self.addr     = client_addr
        self.cat      = cat_conn
        self.sdr_conn = sdr_conn
        self.pan_id   = pan_id

    def run(self):
        log.info(f"SkyRoof conectado desde {self.addr}")

        # Suscribir este cliente para recibir respuestas de SmartSDR CAT
        self.cat.subscribe(self.client)

        buf = ""
        try:
            self.client.settimeout(60.0)
            while True:
                try:
                    data = self.client.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    break
                buf += data.decode(errors="replace")

                while ";" in buf:
                    idx = buf.index(";") + 1
                    cmd = buf[:idx]
                    buf = buf[idx:]

                    log.debug(f"SkyRoof→ {cmd.strip()}")

                    # Reenviar a SmartSDR CAT via la conexión compartida
                    self.cat.send(cmd)

                    # Si es FA (set frequency), mover el panadaptador
                    m = re.match(r"FA(\d{11});", cmd.strip())
                    if m:
                        freq_hz = int(m.group(1))
                        log.info(f"Doppler → {freq_hz/1e6:.6f} MHz")
                        self.sdr_conn.set_pan_center(freq_hz, self.pan_id)

        except Exception as e:
            log.debug(f"Sesión SkyRoof cerrada: {e}")
        finally:
            self.cat.unsubscribe(self.client)
            try:
                self.client.close()
            except:
                pass
            log.info(f"SkyRoof desconectado desde {self.addr}")


# ── Servidor proxy ─────────────────────────────────────────────────────────────
def run_proxy(listen_port, cat_host, cat_port, radio_ip, sdr_port, pan_id):
    # Conexión única a SmartSDR CAT
    cat_conn = CATConn(cat_host, cat_port)
    cat_conn.connect()

    # Conexión a SmartSDR API
    sdr_conn = SmartSDRConn(radio_ip, sdr_port)
    sdr_conn.connect()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", listen_port))
    server.listen(5)

    log.info("=" * 50)
    log.info("  CAT Proxy para SkyRoof + FlexRadio 6600")
    log.info("=" * 50)
    log.info(f"  Escuchando en  : 0.0.0.0:{listen_port}")
    log.info(f"  SmartSDR CAT   : {cat_host}:{cat_port}")
    log.info(f"  SmartSDR API   : {radio_ip}:{sdr_port}")
    log.info(f"  Panadaptador   : {pan_id}")
    log.info("=" * 50)
    log.info(f"Configura SkyRoof CAT → Host: 127.0.0.1, Port: {listen_port}")
    log.info("Esperando conexion de SkyRoof...")

    while True:
        try:
            client_sock, client_addr = server.accept()
            handler = ClientHandler(
                client_sock, client_addr,
                cat_conn, sdr_conn, pan_id
            )
            handler.start()
        except KeyboardInterrupt:
            log.info("Proxy detenido.")
            break
        except Exception as e:
            log.error(f"Error en servidor: {e}")
            time.sleep(1)

    server.close()


# ── Main ───────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="CAT Proxy para SkyRoof + FlexRadio 6600"
    )
    parser.add_argument("--listen",  type=int, default=DEFAULT_LISTEN_PORT)
    parser.add_argument("--cat",     type=int, default=DEFAULT_CAT_PORT)
    parser.add_argument("--radio",   default=DEFAULT_RADIO_IP)
    parser.add_argument("--sdrport", type=int, default=DEFAULT_SDR_PORT)
    parser.add_argument("--pan",     default=DEFAULT_PAN_ID)
    parser.add_argument("--debug",   action="store_true")
    args = parser.parse_args()

    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)

    run_proxy(args.listen, "127.0.0.1", args.cat,
              args.radio, args.sdrport, args.pan)
