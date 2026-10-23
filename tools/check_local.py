#!/usr/bin/env python3
"""Pruebas significativas sin hardware. No instala dependencias."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which("g++") or shutil.which("c++")
node = shutil.which("node")
if not compiler or not node:
    raise SystemExit("Se requieren compilador C++ (g++/clang++) y Node.js en PATH.")
with tempfile.TemporaryDirectory(prefix="etwin-tests-") as tmp:
    for name in ("core", "filter"):
        binary = str(Path(tmp) / ("test_" + name))
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic",
                        "-Iinclude", "-DETWIN_HOST_TEST" if name == "core" else "-DETWIN_FILTER_TEST", "tests/test_" + name + ".cpp", "-o", binary, *(["-lcrypto"] if name == "core" else [])],
                       cwd=root, check=True)
        subprocess.run([binary], cwd=root, check=True)
for args in (["--check", "web/plugin.js"], ["web/test_data.cjs"], ["web/test_ui.cjs"]):
    subprocess.run([node, *args], cwd=root, check=True)
print("PASS: protocolo, analisis, filtro y panel. No equivale a prueba fisica.")
