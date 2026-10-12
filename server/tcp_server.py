"""Servidor IPv4 no bloqueante y coordinador del protocolo IoT."""
from collections import deque
from dataclasses import dataclass, field
import logging
import math
import selectors
import socket
import time
from typing import Optional, Tuple

if __package__:
    from .controller import DistanceController
    from .protocol import ProtocolError, ProtocolHandler
else:
    from controller import DistanceController
    from protocol import ProtocolError, ProtocolHandler

LOGGER = logging.getLogger(__name__)


@dataclass
class OutgoingMessage:
    data: bytes
    queued_at: float
    expires_at: Optional[float] = None
    is_state: bool = False
    offset: int = 0


@dataclass
class ClientConnection:
    sock: socket.socket
    address: Tuple[str, int]
    accepted_at: float = 0.0
    role: Optional[str] = None
    receive_buffer: bytearray = field(default_factory=bytearray)
    outgoing: deque = field(default_factory=deque)
    closing: bool = False


class TCPServer:
    """Un solo hilo atiende sockets, sesiones y plazos monotónicos."""

    def __init__(self, host="0.0.0.0", port=8080, *, clock=time.monotonic,
                 identification_timeout=5.0, measurement_ttl=2.0,
                 resend_interval=0.5, send_timeout=1.0, max_pending_bytes=1024):
        if not 0 <= port <= 65535:
            raise ValueError("El puerto debe estar entre 0 y 65535")
        for value in (identification_timeout, measurement_ttl, resend_interval, send_timeout):
            if not math.isfinite(value) or value <= 0:
                raise ValueError("Los tiempos deben ser positivos y finitos")
        if max_pending_bytes < 32:
            raise ValueError("La cola debe admitir al menos 32 bytes")
        self.host, self.port = host, port
        self.clock = clock
        self.identification_timeout = identification_timeout
        self.resend_interval = resend_interval
        self.send_timeout = send_timeout
        self.max_pending_bytes = max_pending_bytes
        self.controller = DistanceController(measurement_ttl)
        self._selector = None
        self._listener = None
        self._clients = {}
        self._roles = {}
        self._last_broadcast = self.clock()
        self._last_command = self.controller.OFF
        self._shutting_down = False

    @property
    def address(self):
        if self._listener is None:
            raise RuntimeError("El servidor no está iniciado")
        return self._listener.getsockname()

    @property
    def connections(self):
        return tuple(self._clients.values())

    def start(self):
        if self._selector is not None:
            raise RuntimeError("El servidor ya está iniciado")
        self._shutting_down = False
        try:
            self._selector = selectors.DefaultSelector()
            self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._listener.setblocking(False)
            self._listener.bind((self.host, self.port))
            self._listener.listen()
            self._selector.register(self._listener, selectors.EVENT_READ)
            LOGGER.info("Servidor escuchando en %s:%d", *self.address)
        except BaseException:
            self.close()
            raise

    def serve_forever(self):
        try:
            self.start()
            while True:
                self.poll_once()
        finally:
            self.close()

    def poll_once(self, timeout=0.5):
        if self._selector is None:
            raise RuntimeError("El servidor no está iniciado")
        self._timers()
        events = self._selector.select(self._wait_time(timeout))
        # Caducar antes de escribir, incluso si select esperó hasta el plazo.
        self._timers()
        for key, mask in events:
            client = key.data
            if client is None:
                self._accept_client()
                continue
            if client.sock not in self._clients:
                continue
            if mask & selectors.EVENT_READ and not client.closing:
                self._read_client(client)
            if client.sock in self._clients and mask & selectors.EVENT_WRITE:
                self._write_client(client)
        self._timers()

    def _wait_time(self, requested):
        now = self.clock()
        deadlines = []
        if self.controller.valid_until is not None:
            deadlines.append(self.controller.valid_until)
        if "ACTUATOR" in self._roles:
            deadlines.append(self._last_broadcast + self.resend_interval)
        for client in self.connections:
            if client.role is None and not client.closing:
                deadlines.append(client.accepted_at + self.identification_timeout)
            if client.outgoing:
                deadlines.append(client.outgoing[0].queued_at + self.send_timeout)
        return max(0.0, min([requested] + [deadline - now for deadline in deadlines]))

    def _accept_client(self):
        try:
            sock, address = self._listener.accept()
        except BlockingIOError:
            return
        except OSError as exc:
            LOGGER.warning("No se pudo aceptar una conexión: %s", exc)
            return
        client = ClientConnection(sock, address, self.clock())
        try:
            sock.setblocking(False)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._selector.register(sock, selectors.EVENT_READ, client)
            self._clients[sock] = client
        except OSError as exc:
            self._close_socket(sock)
            LOGGER.warning("No se pudo registrar al cliente %s: %s", address, exc)
            return
        LOGGER.info("Cliente conectado: %s:%d", *address)

    def _read_client(self, client):
        try:
            data = client.sock.recv(4096)
        except BlockingIOError:
            return
        except OSError as exc:
            self._disconnect(client, "error de recepción: %s" % exc)
            return
        if not data:
            self._disconnect(client, "cierre remoto")
            return
        for item in ProtocolHandler.feed(client.receive_buffer, data):
            try:
                if isinstance(item, ProtocolError):
                    raise item
                self._handle_line(client, item)
            except ProtocolError as exc:
                self._protocol_error(client, exc)
            if client.closing or client.sock not in self._clients:
                break

    def _handle_line(self, client, line):
        if client.role is None:
            role = ProtocolHandler.identify(line)
            if role in self._roles:
                raise ProtocolError("DUPLICATE", True)
            client.role = role
            self._roles[role] = client
            LOGGER.info("Identificación %s: %s:%d", role, *client.address)
            self._queue(client, "OK")
            if role == "ACTUATOR":
                # Esperar una lectura posterior a esta identificación, aunque
                # la anterior todavía no haya caducado.
                self.controller.invalidate()
                self._publish()
            return
        if client.role != "SENSOR":
            raise ProtocolError("FORMAT")
        measurement = ProtocolHandler.measurement(line)
        LOGGER.info("Medición SENSOR: %s", measurement if measurement is not None else "INVALID")
        self.controller.update(measurement, self.clock())
        self._publish()

    def _protocol_error(self, client, error):
        LOGGER.warning("Error %s de %s (%s)", error.code, client.address, client.role)
        if client.role == "SENSOR":
            self.controller.invalidate()
            self._publish()
        self._queue(client, "ERROR " + error.code)
        if error.fatal and client.sock in self._clients:
            client.closing = True
            client.receive_buffer.clear()
            self._update_events(client)

    def _queue(self, client, line, *, is_state=False, expires_at=None):
        if client.sock not in self._clients or client.closing:
            return
        data = ProtocolHandler.encode(line)
        size = sum(len(frame.data) - frame.offset for frame in client.outgoing)
        if size + len(data) > self.max_pending_bytes:
            self._disconnect(client, "cliente lento: cola de salida excedida")
            return
        client.outgoing.append(OutgoingMessage(data, self.clock(), expires_at, is_state))
        self._update_events(client)

    def _update_events(self, client):
        if client.sock not in self._clients:
            return
        if client.closing and not client.outgoing:
            self._disconnect(client, "error de protocolo")
            return
        events = 0 if client.closing else selectors.EVENT_READ
        if client.outgoing:
            events |= selectors.EVENT_WRITE
        try:
            self._selector.modify(client.sock, events, client)
        except (OSError, ValueError, KeyError) as exc:
            self._disconnect(client, "error del selector: %s" % exc)

    def _write_client(self, client):
        # Una trama por evento: no monopolizar el bucle con clientes lentos.
        while client.outgoing:
            frame = client.outgoing[0]
            if frame.expires_at is not None and self.clock() >= frame.expires_at:
                if frame.offset:
                    self._disconnect(client, "trama parcial caducada")
                    return
                client.outgoing.popleft()
                continue
            if self.clock() - frame.queued_at >= self.send_timeout:
                self._disconnect(client, "cliente lento: timeout de envío")
                return
            try:
                sent = client.sock.send(frame.data[frame.offset:])
            except (BlockingIOError, InterruptedError):
                return
            except OSError as exc:
                self._disconnect(client, "error de envío: %s" % exc)
                return
            if sent == 0:
                self._disconnect(client, "envío sin progreso")
                return
            frame.offset += sent
            if frame.offset == len(frame.data):
                client.outgoing.popleft()
            break
        self._update_events(client)

    def _publish(self):
        self.controller.expire(self.clock())
        command = self.controller.command
        if command != self._last_command:
            LOGGER.info("Estado LEDs: %s", command)
            self._last_command = command
        self._last_broadcast = self.clock()
        actuator = self._roles.get("ACTUATOR")
        if actuator is None or actuator.closing:
            return
        if command == DistanceController.OFF:
            # No entregar estados antiguos después de invalidar la lectura.
            pending = deque()
            for frame in actuator.outgoing:
                if frame.is_state and frame.data != b"LED ALL OFF\n":
                    if frame.offset:
                        self._disconnect(actuator, "apagado durante trama parcial; cierre seguro")
                        return
                else:
                    pending.append(frame)
            actuator.outgoing = pending
            if any(frame.data == b"LED ALL OFF\n" for frame in pending):
                return  # No renovar el plazo de un OFF que el cliente no consume.
        self._queue(actuator, command, is_state=True, expires_at=self.controller.valid_until)

    def _timers(self):
        now = self.clock()
        if self.controller.expire(now):
            LOGGER.info("Medición caducada; apagado")
            self._publish()
        elif "ACTUATOR" in self._roles and now - self._last_broadcast >= self.resend_interval:
            self._publish()
        for client in self.connections:
            if client.outgoing and now - client.outgoing[0].queued_at >= self.send_timeout:
                self._disconnect(client, "cliente lento: timeout de envío")
            elif client.role is None and not client.closing and now - client.accepted_at >= self.identification_timeout:
                self._protocol_error(client, ProtocolError("TIMEOUT", True))

    def _disconnect(self, client, reason):
        if self._clients.pop(client.sock, None) is None:
            return
        if self._roles.get(client.role) is client:
            del self._roles[client.role]
        self._close_socket(client.sock)
        client.receive_buffer.clear()
        client.outgoing.clear()
        LOGGER.info("Cliente desconectado: %s:%d (%s)", *client.address, reason)
        if client.role == "SENSOR":
            self.controller.invalidate()
            if not self._shutting_down:
                self._publish()

    def _close_socket(self, sock):
        if self._selector is not None:
            try:
                self._selector.unregister(sock)
            except (KeyError, ValueError, OSError):
                pass
        try:
            sock.close()
        except OSError as exc:
            LOGGER.warning("Error al cerrar socket: %s", exc)

    def close(self):
        """Apagado de mejor esfuerzo, sin esperas, y limpieza idempotente."""
        was_started = self._selector is not None
        self._shutting_down = True
        self.controller.invalidate()
        actuator = self._roles.get("ACTUATOR")
        if actuator is not None:
            self._publish()
            # Intentar completar OK y OFF sin bloquear; no garantiza recepción.
            for _ in range(2):
                if actuator.sock in self._clients and actuator.outgoing:
                    self._write_client(actuator)
        for client in self.connections:
            self._disconnect(client, "cierre del servidor")
        if self._listener is not None:
            self._close_socket(self._listener)
            self._listener = None
        if self._selector is not None:
            self._selector.close()
            self._selector = None
        if was_started:
            LOGGER.info("Servidor cerrado")
