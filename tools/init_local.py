#!/usr/bin/env python3
"""Crea configuración y claves locales sin sobrescribir instalaciones existentes."""
from pathlib import Path
import shutil
import subprocess
import sys

def main() -> None:
    root = Path(__file__).resolve().parents[1]
    target = root / "include/config.h"
    if not target.exists():
        shutil.copyfile(root / "include/config.example.h", target)
        print("Creado include/config.h. Completa tus credenciales localmente.")
    else:
        print("Se conserva include/config.h existente.")
    keys = root / "include/etwin/RadioKeys.h"
    if not keys.exists():
        subprocess.run([sys.executable, str(root / "tools/new_radio_keys.py")], check=True)
    else:
        print("Se conservan las claves locales existentes.")
    print("Abre la raíz que contiene platformio.ini. No publiques los archivos locales.")

if __name__ == "__main__":
    main()
