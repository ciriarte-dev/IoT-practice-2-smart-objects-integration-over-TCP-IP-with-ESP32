"""Transporte TCP básico; todavía no interpreta el protocolo de aplicación."""

from dataclasses import dataclass, field
import logging
import selectors
import socket
from typing import Dict, Optional, Tuple


LOGGER = logging.getLogger(__name__)


@dataclass
class ClientConnection:
    """Estado de transporte independiente para cada socket aceptado."""

    sock: socket.socket
    address: Tuple[str, int]
    receive_buffer: bytearray = field(default_factory=bytearray)


class TCPServer:
    """Servidor IPv4 atendido por un solo bucle de eventos.

    start(), poll_once() y close() permiten controlar el ciclo en pruebas.
    serve_forever() gestiona el ciclo completo y libera recursos al salir.
    Estos métodos deben ejecutarse desde el mismo hilo.
    """

    def __init__(self, host: str = "0.0.0.0", port: int = 5000,
                 max_buffer_bytes: int = 65536) -> None:
        if not 0 <= port <= 65535:
            raise ValueError("El puerto debe estar entre 0 y 65535")
        if max_buffer_bytes <= 0:
            raise ValueError("El límite del buffer debe ser positivo")
        self.host = host
        self.port = port
        self.max_buffer_bytes = max_buffer_bytes
        self._selector: Optional[selectors.BaseSelector] = None
        self._listener: Optional[socket.socket] = None
        self._clients: Dict[socket.socket, ClientConnection] = {}

    @property
    def address(self) -> Tuple[str, int]:
        """Dirección real de escucha, incluido el puerto asignado si se usó 0."""
        if self._listener is None:
            raise RuntimeError("El servidor no está iniciado")
        return self._listener.getsockname()

    @property
    def connections(self) -> Tuple[ClientConnection, ...]:
        return tuple(self._clients.values())

    def start(self) -> None:
        if self._selector is not None:
            raise RuntimeError("El servidor ya está iniciado")
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
            # También liberar recursos si Ctrl+C interrumpe la inicialización.
            self.close()
            raise

    def serve_forever(self) -> None:
        try:
            self.start()
            while True:
                self.poll_once()
        finally:
            self.close()

    def poll_once(self, timeout: float = 0.5) -> None:
        if self._selector is None:
            raise RuntimeError("El servidor no está iniciado")
        for key, _ in self._selector.select(timeout):
            if key.data is None:
                self._accept_client()
            else:
                self._read_client(key.data)

    def _accept_client(self) -> None:
        try:
            sock, address = self._listener.accept()
        except BlockingIOError:
            return
        except OSError as exc:
            LOGGER.warning("No se pudo aceptar una conexión: %s", exc)
            return

        client = ClientConnection(sock, address)
        try:
            sock.setblocking(False)
            self._selector.register(sock, selectors.EVENT_READ, client)
            self._clients[sock] = client
        except OSError as exc:
            self._close_socket(sock)
            LOGGER.warning("No se pudo registrar al cliente %s: %s", address, exc)
            return
        LOGGER.info("Cliente conectado: %s:%d", *address)

    def _read_client(self, client: ClientConnection) -> None:
        try:
            # Un bloque por evento evita que un emisor monopolice el bucle.
            data = client.sock.recv(4096)
        except BlockingIOError:
            return
        except OSError as exc:
            self._disconnect(client, "error de recepción: %s" % exc)
            return

        if not data:
            self._disconnect(client, "cierre remoto")
            return
        if len(client.receive_buffer) + len(data) > self.max_buffer_bytes:
            self._disconnect(client, "límite del buffer de recepción excedido")
            return

        client.receive_buffer.extend(data)
        # Punto de integración futuro para ProtocolHandler. En esta etapa los
        # bytes se conservan sin decodificar, delimitar ni responder mensajes.
        LOGGER.debug("Recibidos %d bytes de %s:%d", len(data), *client.address)

    def _disconnect(self, client: ClientConnection, reason: str) -> None:
        self._clients.pop(client.sock, None)
        self._close_socket(client.sock)
        client.receive_buffer.clear()
        LOGGER.info("Cliente desconectado: %s:%d (%s)", *client.address, reason)

    def _close_socket(self, sock: socket.socket) -> None:
        if self._selector is not None:
            try:
                self._selector.unregister(sock)
            except (KeyError, ValueError, OSError):
                # Puede no haberse registrado si falló la inicialización.
                pass
        try:
            sock.close()
        except OSError as exc:
            LOGGER.warning("Error al cerrar socket: %s", exc)

    def close(self) -> None:
        """Cierra clientes, escucha y selector; se puede llamar varias veces."""
        was_started = self._selector is not None
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
