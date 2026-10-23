#include <Arduino.h>
#include <M5Unified.h>
#include <PZEM004Tv30.h>
#include <cmath>
#include <new>

#include "config.h"
#include "etwin/Protocol.h"
#include "etwin/SlaveRadio.h"

namespace {
constexpr uint32_t READ_INTERVAL_MS = 1000;
HardwareSerial meterUart(1);
PZEM004Tv30* meter = nullptr;
etwin::SlaveRadio radio(etwin::Node::Energy);
uint32_t lastReadMs = 0;
uint32_t previousLoopMs = 0;
bool resetMeterAfterRollover = false;

etwin::EnergyPacket readMeter() {
  etwin::EnergyPacket packet{};
  packet.voltage = packet.current = packet.power = packet.energyKWh =
      packet.frequency = packet.powerFactor = NAN;
  packet.h.flags = etwin::SENSOR_ERROR;

  // The v3 library refreshes the whole Modbus measurement block in voltage().
  // Abort immediately on a failed FIRST getter; do not mix this failed request
  // with additional getters or publish an earlier block as a new measurement.
  packet.voltage = meter->voltage();
  if (!std::isfinite(packet.voltage)) return packet;
  packet.current = meter->current();
  packet.power = meter->power();
  packet.energyKWh = meter->energy();
  packet.frequency = meter->frequency();
  packet.powerFactor = meter->pf();
  packet.h.flags = etwin::VALID;
  if (!etwin::validEnergy(packet)) packet.h.flags = etwin::SENSOR_ERROR;
  return packet;
}
}  // namespace

void setup() {
  auto m5cfg = M5.config();
  m5cfg.serial_baudrate = 115200;
  m5cfg.fallback_board = m5::board_t::board_M5AtomS3Lite;
  m5cfg.internal_imu = false;
  m5cfg.internal_rtc = false;
  m5cfg.internal_mic = false;
  m5cfg.internal_spk = false;
  m5cfg.external_imu = false;
  m5cfg.external_rtc = false;
  m5cfg.external_display_value = 0;
  M5.begin(m5cfg);

  // Initialize UART AFTER M5.begin: no later external-I2C probe may claim G1/G2.
  // PZEM TX -> ESP RX G2. PZEM RX <- ESP TX G1. Common low-voltage GND.
  // PZEM interface supply is 5 V; use UART level adaptation for 3.3 V ESP GPIO.
  static PZEM004Tv30 instance(meterUart, cfg::PZEM_RX, cfg::PZEM_TX,
                            cfg::PZEM_ADDRESS);
  meter = &instance;

  Serial.println("\nE-TWIN / esclavo 1 / ATOMS3 Lite / PZEM-004T v3");
  Serial.printf("UART: RX=G%d <- PZEM TX; TX=G%d -> PZEM RX\n",
                cfg::PZEM_RX, cfg::PZEM_TX);
  Serial.println("El contador kWh NO se reinicia al encender.");
  if (!radio.begin()) Serial.println("ESP-NOW no iniciado: revisar MAC/configuracion.");
  lastReadMs = millis();
}

void loop() {
  M5.update();
  radio.update();
  const uint32_t now = millis();
  if (now < previousLoopMs) resetMeterAfterRollover = true;
  previousLoopMs = now;
  if (static_cast<uint32_t>(now - lastReadMs) >= READ_INTERVAL_MS) {
    // Library1.1.2 uses lastRead+interval>millis internally. Reconstruct its
    // cache at uint32 rollover so old values cannot survive for another49days.
    // This only resets the UART wrapper; NEVER resets the PZEM energy counter.
    if (resetMeterAfterRollover) {
      meter->~PZEM004Tv30();
      new (meter) PZEM004Tv30(meterUart, cfg::PZEM_RX, cfg::PZEM_TX, cfg::PZEM_ADDRESS);
      resetMeterAfterRollover = false;
    }
    lastReadMs = now;
    etwin::EnergyPacket packet = readMeter();
    const bool queued = radio.send(packet);
    if (etwin::validEnergy(packet)) {
      Serial.printf("V=%.1f I=%.3f P=%.1f E=%.3f f=%.1f PF=%.2f | ch=%u enlace=%s envio=%s\n",
                    packet.voltage, packet.current, packet.power,
                    packet.energyKWh, packet.frequency, packet.powerFactor,
                    radio.channel(), radio.linked() ? "SI" : "NO",
                    queued ? "encolado" : "pendiente");
    } else {
      Serial.printf("PZEM SIN LECTURA VALIDA | ch=%u enlace=%s\n",
                    radio.channel(), radio.linked() ? "SI" : "NO");
    }
  }
  delay(2);
}
