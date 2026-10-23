#!/usr/bin/env python3
"""Genera claves ESP-NOW locales. Obliga a recompilar/cargar los tres equipos."""
from pathlib import Path
import secrets
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--replace", action="store_true",
                    help="Reemplazar claves actuales; luego recompilar y cargar los tres equipos")
args = parser.parse_args()
target = Path(__file__).resolve().parents[1] / "include/etwin/RadioKeys.h"
if target.exists() and not args.replace:
    raise SystemExit("Ya existen claves. Usa --replace si vas a recargar los tres equipos.")
lines = ["#pragma once", "#include <stdint.h>",
         "// Claves propias de esta instalacion. Recompilar los tres equipos.",
         "namespace etwin { namespace keys {"]
for name, length in (("PMK",16), ("ENERGY_LMK",16), ("VIBRATION_LMK",16), ("BEACON_AUTH_KEY",32), ("ENERGY_AUTH_KEY",32), ("VIBRATION_AUTH_KEY",32)):
    key = secrets.token_bytes(length)
    lines.append("constexpr uint8_t " + name + "[" + str(length) + "] = {" +
                 ",".join(f"0x{x:02X}" for x in key) + "};")
lines.extend(["} }", ""])
target.write_text("\n".join(lines), encoding="utf-8")
print("Claves actualizadas. Recompila y carga MAESTRO + AMBOS ESCLAVOS.")
