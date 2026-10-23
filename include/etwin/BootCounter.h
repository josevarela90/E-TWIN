#pragma once
#include <Arduino.h>
#include <Preferences.h>
namespace etwin {
// Escribe 1 vez por arranque, no cada muestra. Fallo NVS => NO ARRANCA la radio.
inline uint32_t newBootCounter() {
  Preferences pref;
  if (!pref.begin("etwin-ot", false)) return 0;
  const uint32_t old = pref.getUInt("boot", 0);
  if (old == UINT32_MAX) { pref.end(); return 0; }
  const uint32_t next = old + 1;
  const bool saved = pref.putUInt("boot", next) == sizeof(uint32_t);
  pref.end();
  return saved ? next : 0;
}
}
