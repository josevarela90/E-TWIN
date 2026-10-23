// EJEMPLO PUBLICABLE: copiar a config.h y completar localmente.
// Las credenciales reales y RadioKeys.h nunca se suben a GitHub.
#pragma once
#include <stdint.h>

// CONFIGURACION DEL PROYECTO. Compilar los 3 entornos despues de cambiar radio/MAC.
// Credenciales se usan SOLO en el maestro; los esclavos no necesitan Internet.
namespace cfg {
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr uint32_t THINGSPEAK_CHANNEL_ID = 1309330;
// Devices > MQTT en ThingSpeak. NO son las claves API Read/Write del canal.
constexpr char MQTT_CLIENT_ID[] = "";
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_HOST[] = "mqtt3.thingspeak.com";
constexpr uint16_t MQTT_PORT = 8883;
constexpr uint32_t CLOUD_INTERVAL_MS = 20000;
static_assert(CLOUD_INTERVAL_MS>=15000, "ThingSpeak: respetar intervalo de escritura");
constexpr uint32_t SENSOR_TTL_MS = 5000;
constexpr uint8_t FALLBACK_CHANNEL = 6;
// Descubrimiento canales1..11 (comunes internacionalmente). Si tu AP usa12/13,
// ajustar MAX_RADIO_CHANNEL segun dominio regulatorio y configurar ambos extremos.
constexpr uint8_t MAX_RADIO_CHANNEL = 11;
constexpr bool CHECK_FACTORY_MAC = true;
constexpr int PZEM_RX=2; // TX DEL PZEM -> GPIO2 del Atom
constexpr int PZEM_TX=1; // RX DEL PZEM <- GPIO1 del Atom
constexpr uint8_t PZEM_ADDRESS=0xF8;
constexpr uint16_t SAMPLE_RATE_HZ=500;
constexpr uint16_t VIBRATION_WINDOW_SAMPLES=500;

// Umbrales de PUESTA EN MARCHA: no son limites del fabricante ni de ISO.
// 0 deshabilita limites absolutos que dependen de la placa de la maquina.
constexpr float NOMINAL_VOLTAGE=0.0f; // rellenar 120/220/230... segun instalacion
constexpr float VOLTAGE_TOLERANCE_PERCENT=10.0f;
constexpr float RUNNING_THRESHOLD_W=10.0f; // ajustar segun consumo en reposo
constexpr float LOW_PF_WARNING=0.80f;
constexpr float PF_MIN_POWER_W=50.0f;
constexpr float POWER_DEVIATION_WARNING_PERCENT=20.0f;
constexpr float VIBRATION_RATIO_WARNING=1.50f;
constexpr float VIBRATION_RATIO_ALARM=2.00f;
constexpr float MIN_BASELINE_VIBRATION=0.03f; // evita cociente enorme por ruido casi0
constexpr float VIBRATION_ABSOLUTE_ALARM=0.0f; // m/s2, 0=sin limite absoluto
constexpr uint16_t BASELINE_SAMPLES=10; // 10 pares nuevos, consecutivos y validos
constexpr uint32_t BASELINE_SAMPLE_INTERVAL_MS=1000;
constexpr float BASELINE_MAX_POWER_CV=0.15f; // rechaza referencia inestable
constexpr float BASELINE_MAX_VIBRATION_CV=0.30f;
}
