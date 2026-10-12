"""Punto de entrada del servidor TCP básico."""

import argparse
import logging

if __package__:
    from .tcp_server import TCPServer
else:
    from tcp_server import TCPServer


def port_number(value: str) -> int:
    try:
        port = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("El puerto debe ser un entero") from exc
    if not 0 <= port <= 65535:
        raise argparse.ArgumentTypeError("El puerto debe estar entre 0 y 65535")
    return port


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Servidor TCP IoT — etapa 2")
    parser.add_argument("--host", default="0.0.0.0", help="IPv4 de escucha (0.0.0.0)")
    parser.add_argument("--port", type=port_number, default=5000,
                        help="Puerto de escucha (5000); 0 elige un puerto libre")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    server = TCPServer(args.host, args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("Interrupción Ctrl+C recibida; salida normal")
        return 0
    except OSError as exc:
        logging.error("No se pudo ejecutar el servidor: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
