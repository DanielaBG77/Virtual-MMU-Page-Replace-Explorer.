#!/usr/bin/env python3
"""Servidor local del dashboard: sirve la pagina y ejecuta el simulador desde el boton Ejecutar.
Uso (desde la raiz del proyecto):  python3 servidor.py   ->  http://localhost:8000
Solo usa la biblioteca estandar de Python (no hay que instalar nada)."""
import json
import os
import re
import subprocess
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

RAIZ = os.path.dirname(os.path.abspath(__file__))
SIMULADOR = os.path.join(RAIZ, "simulador")
ALGORITMOS = ["fifo", "lru", "reloj", "optimo"]
candado = threading.Lock()
procesos = []  # simulaciones en vivo que estan corriendo


def secuencias():
    try:
        return sorted(f for f in os.listdir(os.path.join(RAIZ, "pruebas")) if re.fullmatch(r"[\w.-]+\.txt", f))
    except OSError:
        return []


def entero(valor, minimo, maximo):
    n = int(valor)
    if not minimo <= n <= maximo:
        raise ValueError("fuera de rango")
    return n


def carpeta_vivo(alg):
    """Cada algoritmo corre en su propia carpeta para que no se pisen sus archivos de estado."""
    return os.path.join(RAIZ, ".vivo", alg)


def detener_todo():
    for p in procesos:
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
    procesos.clear()


class Manejador(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=os.path.join(RAIZ, "dashboard"), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def responder(self, datos, codigo=200):
        cuerpo = json.dumps(datos).encode()
        self.send_response(codigo)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(cuerpo)))
        self.end_headers()
        self.wfile.write(cuerpo)

    def do_GET(self):
        ruta = urlparse(self.path).path
        if ruta == "/api/secuencias":
            return self.responder({"ok": True, "secuencias": secuencias()})
        if ruta == "/api/estados":  # estado actual de cada algoritmo que esta corriendo
            estados = {}
            for alg in ALGORITMOS:
                try:
                    with open(os.path.join(carpeta_vivo(alg), "dashboard", "estado.json")) as f:
                        estados[alg] = json.load(f)
                except (OSError, ValueError):
                    pass
            return self.responder({"ok": True, "estados": estados})
        super().do_GET()

    def do_POST(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        if url.path not in ("/api/vivo", "/api/belady"):
            return self.responder({"ok": False, "error": "ruta desconocida"}, 404)
        if not os.path.exists(SIMULADOR):
            return self.responder({"ok": False, "error": "falta compilar: g++ -std=c++17 -pthread -o simulador src/main.cpp"})
        try:
            seq = q["seq"][0]
            if seq not in secuencias():
                raise ValueError("secuencia invalida")
            ruta = os.path.join(RAIZ, "pruebas", seq)
            if url.path == "/api/belady":
                r = subprocess.run([SIMULADOR, ruta, "3", "belady"], cwd=RAIZ, capture_output=True, timeout=30)
                return self.responder({"ok": r.returncode == 0, "error": r.stderr.decode()[:200]})
            alg = q["alg"][0]
            if alg != "todos" and alg not in ALGORITMOS:
                raise ValueError("algoritmo invalido")
            marcos = entero(q["marcos"][0], 1, 10)
            ms = entero(q["ms"][0], 0, 3000)
        except (KeyError, ValueError, subprocess.SubprocessError):
            return self.responder({"ok": False, "error": "parametros invalidos"}, 400)
        elegidos = ALGORITMOS if alg == "todos" else [alg]
        with candado:
            detener_todo()  # solo una corrida a la vez
            for a in ALGORITMOS:  # borra datos de la corrida anterior
                try:
                    os.remove(os.path.join(carpeta_vivo(a), "dashboard", "estado.json"))
                except OSError:
                    pass
            for a in elegidos:
                os.makedirs(os.path.join(carpeta_vivo(a), "dashboard"), exist_ok=True)
                procesos.append(subprocess.Popen([SIMULADOR, ruta, str(marcos), "vivo", a, str(ms)],
                                                 cwd=carpeta_vivo(a), stdout=subprocess.DEVNULL,
                                                 stderr=subprocess.DEVNULL))
        self.responder({"ok": True})


if __name__ == "__main__":
    puerto = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    servidor = ThreadingHTTPServer(("127.0.0.1", puerto), Manejador)  # solo accesible desde este computador
    print(f"Dashboard en http://localhost:{puerto}  (Ctrl + C para apagar)")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        detener_todo()
        print("\nServidor apagado.")