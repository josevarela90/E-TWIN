#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "etwin/RadioKeys.h"
#ifdef ETWIN_HOST_TEST
#include <openssl/hmac.h>
#else
#include <mbedtls/md.h>
#endif

namespace etwin {
constexpr uint32_t MAGIC = 0x4554574E; // E T W N
constexpr uint8_t PROTOCOL_VERSION = 2;
enum class Node : uint8_t { Master=0, Energy=1, Vibration=2 };
enum class Kind : uint8_t { Beacon=1, Energy=2, Vibration=3, Ack=4 };
enum Flags : uint16_t {
  VALID=1u<<0, WARMUP=1u<<1, TIMING_BAD=1u<<2,
  CLIPPED=1u<<3, SENSOR_ERROR=1u<<4, FIFO_OVERFLOW=1u<<5
};
constexpr uint8_t MASTER_MAC[6] = {0x34,0x98,0x7A,0x6A,0x13,0xE4};
constexpr uint8_t ENERGY_MAC[6] = {0x34,0xB7,0xDA,0x56,0x9F,0x64};
constexpr uint8_t VIBRATION_MAC[6] = {0x34,0xB7,0xDA,0x5E,0x77,0x00};
constexpr uint8_t BROADCAST_MAC[6] = {255,255,255,255,255,255};

#pragma pack(push,1)
struct Header {
  uint32_t magic;
  uint8_t version, kind, node, reserved;
  uint32_t sequence, bootId, uptimeMs, session;
  uint16_t flags, size;
};
struct EnergyPacket {
  Header h;
  float voltage, current, power, energyKWh, frequency, powerFactor;
  uint8_t tag[16];
};
struct VibrationPacket {
  Header h;
  float rmsX, rmsY, rmsZ, rms, peak, crest, sampleRateHz;
  uint16_t samples, missed;
  uint8_t tag[16];
};
struct BeaconPacket {
  Header h;
  uint8_t channel, reserved[3];
  uint8_t tag[16];
};
struct AckPacket {
  Header h;
  uint32_t acknowledgedBoot, acknowledgedSequence;
  uint8_t tag[16];
};
#pragma pack(pop)
static_assert(sizeof(Header)==28, "Unexpected protocol layout");
static_assert(sizeof(EnergyPacket)==68, "Unexpected energy layout");
static_assert(sizeof(VibrationPacket)==76, "Unexpected vibration layout");
static_assert(sizeof(VibrationPacket)<=250, "ESP-NOW v1 limit");

inline const uint8_t* macFor(Node node) {
  return node==Node::Energy ? ENERGY_MAC : node==Node::Vibration ? VIBRATION_MAC : MASTER_MAC;
}
inline bool sameMac(const uint8_t* a,const uint8_t* b) { return a && b && memcmp(a,b,6)==0; }
// Integridad y autenticidad de los paquetes: HMAC-SHA256/128, con claves
// independientes para cada nodo; el enlace unicast tambien lleva AES-CCMP.
// El beacon es broadcast (NO es cifrable en ESP-NOW), PERO SI autenticado.
inline bool calculateTag(const void* data, size_t length, const uint8_t* secret, uint8_t tag[16]) {
  if (!data || !secret || !tag) return false;
  uint8_t full[32]{};
#ifdef ETWIN_HOST_TEST
  unsigned int digestLength = 0;
  if (!HMAC(EVP_sha256(), secret, 32, static_cast<const unsigned char*>(data), length, full, &digestLength)
      || digestLength != sizeof(full)) return false;
#else
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md || mbedtls_md_hmac(md, secret, 32, static_cast<const unsigned char*>(data), length, full) != 0) return false;
#endif
  memcpy(tag, full, 16);
  memset(full, 0, sizeof(full));
  return true;
}
inline const uint8_t* authenticationKey(Node sender, Kind kind, Node ackTarget = Node::Master) {
  if (kind == Kind::Beacon && sender == Node::Master) return keys::BEACON_AUTH_KEY;
  if (kind == Kind::Ack && sender == Node::Master) {
    return ackTarget == Node::Energy ? keys::ENERGY_AUTH_KEY : ackTarget == Node::Vibration ? keys::VIBRATION_AUTH_KEY : nullptr;
  }
  return sender == Node::Energy ? keys::ENERGY_AUTH_KEY : sender == Node::Vibration ? keys::VIBRATION_AUTH_KEY : nullptr;
}
template<class T> inline bool seal(T& p, Kind kind, Node node, uint32_t seq, uint32_t boot, uint32_t now,
                                    uint32_t masterSession, const uint8_t* secret) {
  p.h.magic = MAGIC; p.h.version = PROTOCOL_VERSION; p.h.kind = static_cast<uint8_t>(kind);
  p.h.node = static_cast<uint8_t>(node); p.h.reserved = 0; p.h.sequence = seq;
  p.h.bootId = boot; p.h.uptimeMs = now; p.h.session = masterSession; p.h.size = sizeof(T);
  memset(p.tag, 0, sizeof(p.tag));
  return calculateTag(&p, sizeof(T) - sizeof(p.tag), secret, p.tag);
}
template<class T> inline bool decode(const uint8_t* bytes, size_t length, Kind kind, Node node,
                                      T& out, const uint8_t* secret) {
  if (!bytes || length != sizeof(T)) return false;
  T tmp{}; memcpy(&tmp, bytes, sizeof(T));
  if (tmp.h.magic != MAGIC || tmp.h.version != PROTOCOL_VERSION || tmp.h.size != sizeof(T)
      || tmp.h.kind != static_cast<uint8_t>(kind) || tmp.h.node != static_cast<uint8_t>(node)
      || tmp.h.reserved != 0 || !tmp.h.bootId || !tmp.h.session) return false;
  uint8_t expected[16]{};
  if (!calculateTag(&tmp, sizeof(T) - sizeof(tmp.tag), secret, expected)) return false;
  // Comparacion de tiempo independiente del byte que difiera.
  uint8_t mismatch = 0;
  for (size_t i = 0; i < sizeof(expected); ++i) mismatch |= expected[i] ^ tmp.tag[i];
  if (mismatch) return false;
  out = tmp;
  return true;
}
// Boot monotono; dentro del mismo boot, secuencia estrictamente creciente.
inline bool newerFrame(uint32_t boot, uint32_t seq, uint32_t previousBoot, uint32_t previousSeq) {
  return boot > previousBoot || (boot == previousBoot && static_cast<int32_t>(seq - previousSeq) > 0);
}
inline bool newerSequence(uint32_t incoming,uint32_t previous) { return static_cast<int32_t>(incoming-previous)>0; }
inline bool validEnergy(const EnergyPacket& p) {
  return (p.h.flags&VALID) && !(p.h.flags&(WARMUP|TIMING_BAD|CLIPPED|SENSOR_ERROR|FIFO_OVERFLOW)) && isfinite(p.voltage) && p.voltage>=0 && p.voltage<=300
    && isfinite(p.current) && p.current>=0 && p.current<=110
    && isfinite(p.power) && p.power>=0 && p.power<=33000
    && isfinite(p.energyKWh) && p.energyKWh>=0
    && isfinite(p.frequency) && p.frequency>=40 && p.frequency<=70
    && isfinite(p.powerFactor) && p.powerFactor>=0 && p.powerFactor<=1.01f;
}
inline bool validVibration(const VibrationPacket& p) {
  return (p.h.flags&VALID) && !(p.h.flags&(WARMUP|TIMING_BAD|CLIPPED|SENSOR_ERROR|FIFO_OVERFLOW))
    && isfinite(p.rmsX) && p.rmsX>=0 && isfinite(p.rmsY) && p.rmsY>=0
    && isfinite(p.rmsZ) && p.rmsZ>=0 && isfinite(p.rms) && p.rms>=0 && p.rms<150
    && isfinite(p.peak) && p.peak>=p.rms && p.peak<150
    && isfinite(p.crest) && p.crest>=0 && isfinite(p.sampleRateHz)
    && p.sampleRateHz>=475 && p.sampleRateHz<=525 && p.samples==500 && p.missed==0;
}
} // namespace etwin
