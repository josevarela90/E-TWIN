#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include "etwin/Protocol.h"
#include "etwin/Analysis.h"

// Host tests exercise external invariants: wire integrity, stale/invalid data,
// restart/wrap behavior and honest reference-based monitoring. No ESP32 needed.
using namespace etwin;
static int checks = 0, failures = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
static bool near(double a, double b, double tolerance = 1e-5) {
  return std::fabs(a - b) <= tolerance;
}

static EnergyPacket energy(float power = 100.0f) {
  EnergyPacket p{};
  p.h.flags = VALID;
  p.voltage = 230.0f; p.current = 0.5f; p.power = power;
  p.energyKWh = 12.5f; p.frequency = 60.0f; p.powerFactor = 0.9f;
  seal(p, Kind::Energy, Node::Energy, 7, 123, 1000, 4002, keys::ENERGY_AUTH_KEY);
  return p;
}
static VibrationPacket vibration(float rms = 0.5f) {
  VibrationPacket p{};
  p.h.flags = VALID;
  p.rmsX = rms; p.rmsY = 0; p.rmsZ = 0; p.rms = rms;
  p.peak = rms * 1.6f; p.crest = 1.6f;
  p.sampleRateHz = 500.0f; p.samples = 500; p.missed = 0;
  seal(p, Kind::Vibration, Node::Vibration, 8, 234, 1000, 4002, keys::VIBRATION_AUTH_KEY);
  return p;
}
static Baseline reference() {
  Baseline b;
  b.power = 100.0f; b.vibration = 0.5f;
  b.samples = cfg::BASELINE_SAMPLES; b.capturedEpoch = 1700000000;
  return b;
}
template<class T> static Sample<T> sample(const T& packet, uint32_t now = 1000) {
  Sample<T> result;
  result.packet = packet; result.receivedMs = now; result.seen = true;
  return result;
}
template<class T> static bool reauth(T& packet, const uint8_t* key) {
  return calculateTag(&packet, sizeof(packet)-sizeof(packet.tag), key, packet.tag);
}
template<class T> static bool accepts(const T& packet, Kind kind, Node node, const uint8_t* key=nullptr) {
  if (!key) key=authenticationKey(node,kind,Node::Energy);
  T decoded{};
  return decode(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet), kind, node, decoded, key);
}
static void testWire() {
  EnergyPacket p=energy(),decoded{};
  const uint8_t* raw=reinterpret_cast<const uint8_t*>(&p);
  CHECK(decode(raw,sizeof(p),Kind::Energy,Node::Energy,decoded,keys::ENERGY_AUTH_KEY));
  CHECK(decoded.h.flags==VALID && decoded.h.sequence==7 && decoded.h.bootId==123 && decoded.h.session==4002);
  CHECK(decoded.voltage==230.0f && decoded.energyKWh==12.5f);
  CHECK(!decode(raw,sizeof(p)-1,Kind::Energy,Node::Energy,decoded,keys::ENERGY_AUTH_KEY));
  CHECK(!decode(raw,sizeof(p)+1,Kind::Energy,Node::Energy,decoded,keys::ENERGY_AUTH_KEY));
  CHECK(!decode(nullptr,sizeof(p),Kind::Energy,Node::Energy,decoded,keys::ENERGY_AUTH_KEY));
  CHECK(!accepts(p,Kind::Vibration,Node::Energy));
  CHECK(!accepts(p,Kind::Energy,Node::Vibration));
  CHECK(!accepts(p,Kind::Energy,Node::Energy,keys::VIBRATION_AUTH_KEY)); // wrong key
  auto tampered=std::vector<uint8_t>(raw,raw+sizeof(p));
  tampered[sizeof(Header)+3]^=0x01;
  CHECK(!decode(tampered.data(),tampered.size(),Kind::Energy,Node::Energy,decoded,keys::ENERGY_AUTH_KEY));
  tampered.assign(raw,raw+sizeof(p)); tampered.back()^=0x55;
  CHECK(!decode(tampered.data(),tampered.size(),Kind::Energy,Node::Energy,decoded,keys::ENERGY_AUTH_KEY));
  p=energy(); p.h.version=3; CHECK(reauth(p,keys::ENERGY_AUTH_KEY)); CHECK(!accepts(p,Kind::Energy,Node::Energy));
  p=energy(); p.h.magic=0; CHECK(reauth(p,keys::ENERGY_AUTH_KEY)); CHECK(!accepts(p,Kind::Energy,Node::Energy));
  p=energy(); p.h.size=sizeof(p)-1; CHECK(reauth(p,keys::ENERGY_AUTH_KEY)); CHECK(!accepts(p,Kind::Energy,Node::Energy));
  p=energy(); p.h.reserved=1; CHECK(reauth(p,keys::ENERGY_AUTH_KEY)); CHECK(!accepts(p,Kind::Energy,Node::Energy));
  p=energy(); p.h.session=0; CHECK(reauth(p,keys::ENERGY_AUTH_KEY)); CHECK(!accepts(p,Kind::Energy,Node::Energy));
  CHECK(sizeof(Header)==28 && sizeof(EnergyPacket)==68 && sizeof(VibrationPacket)==76);
  CHECK(sizeof(AckPacket)<=250 && sizeof(BeaconPacket)<=250);
  CHECK(sameMac(ENERGY_MAC,macFor(Node::Energy)) && !sameMac(ENERGY_MAC,VIBRATION_MAC));
  CHECK(!sameMac(nullptr,MASTER_MAC) && !sameMac(MASTER_MAC,nullptr));
  CHECK(accepts(vibration(),Kind::Vibration,Node::Vibration));
  BeaconPacket b{}; b.channel=6;
  CHECK(seal(b,Kind::Beacon,Node::Master,1,987,900,4002,keys::BEACON_AUTH_KEY));
  CHECK(accepts(b,Kind::Beacon,Node::Master,keys::BEACON_AUTH_KEY));
  CHECK(!accepts(b,Kind::Beacon,Node::Master,keys::ENERGY_AUTH_KEY));
  AckPacket a{}; a.acknowledgedBoot=123; a.acknowledgedSequence=7;
  CHECK(seal(a,Kind::Ack,Node::Master,2,987,901,4002,keys::ENERGY_AUTH_KEY));
  CHECK(accepts(a,Kind::Ack,Node::Master,keys::ENERGY_AUTH_KEY));
  CHECK(!accepts(a,Kind::Ack,Node::Master,keys::VIBRATION_AUTH_KEY));
  CHECK(newerFrame(11,1,10,500));
  CHECK(newerFrame(10,501,10,500));
  CHECK(!newerFrame(9,700,10,500));
  CHECK(!newerFrame(10,500,10,500));
  CHECK(!newerFrame(10,499,10,500));
}

static void testSequenceAndFreshness() {
  CHECK(newerSequence(11, 10));
  CHECK(!newerSequence(10, 10));
  CHECK(!newerSequence(9, 10));
  CHECK(newerSequence(0, UINT32_MAX));
  CHECK(newerSequence(3, UINT32_MAX - 2));
  CHECK(!newerSequence(UINT32_MAX - 2, 3));
  CHECK(!newerSequence(0x80000000u, 0)); // ambiguous half-range is not new
  // A changed bootId must be handled by the receive owner, not by this ordering function.
  CHECK(!newerSequence(1, 1000));

  Sample<EnergyPacket> never;
  CHECK(!never.fresh(0));
  auto e = sample(energy(), 1000);
  CHECK(e.fresh(1000));
  CHECK(e.fresh(1000 + cfg::SENSOR_TTL_MS - 1));
  CHECK(!e.fresh(1000 + cfg::SENSOR_TTL_MS));
  CHECK(!e.fresh(1000 + cfg::SENSOR_TTL_MS + 1));
  e.receivedMs = UINT32_MAX - 100;
  CHECK(e.fresh(50));
  CHECK(!e.fresh(cfg::SENSOR_TTL_MS - 101));
}

static void testMeasurementValidity() {
  CHECK(validEnergy(energy()));
  auto e = energy(0); e.current = 0; e.energyKWh = 0; e.powerFactor = 0;
  CHECK(validEnergy(e)); // genuine zero PF/energy/current are not missing values
  e.h.flags = 0; CHECK(!validEnergy(e));
  e = energy(); e.h.flags = VALID | SENSOR_ERROR;
  CHECK(!validEnergy(e));
  const float bad[] = {-1.0f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()};
  float EnergyPacket::* members[] = {&EnergyPacket::voltage, &EnergyPacket::current,
    &EnergyPacket::power, &EnergyPacket::energyKWh, &EnergyPacket::frequency, &EnergyPacket::powerFactor};
  for (auto member : members) for (float value : bad) {
    e = energy(); e.*member = value; CHECK(!validEnergy(e));
  }
  e = energy(); e.voltage = 301; CHECK(!validEnergy(e));
  e = energy(); e.current = 111; CHECK(!validEnergy(e));
  e = energy(); e.power = 33001; CHECK(!validEnergy(e));
  e = energy(); e.frequency = 39.9f; CHECK(!validEnergy(e));
  e = energy(); e.frequency = 70.1f; CHECK(!validEnergy(e));
  e = energy(); e.powerFactor = 1.02f; CHECK(!validEnergy(e));

  auto v = vibration(); CHECK(validVibration(v));
  const uint16_t unusable[] = {WARMUP, TIMING_BAD, CLIPPED, SENSOR_ERROR, FIFO_OVERFLOW};
  for (auto flag : unusable) { v = vibration(); v.h.flags |= flag; CHECK(!validVibration(v)); }
  v = vibration(); v.h.flags = 0; CHECK(!validVibration(v));
  v = vibration(); v.missed = 1; CHECK(!validVibration(v));
  v = vibration(); v.samples = 499; CHECK(!validVibration(v));
  v = vibration(); v.sampleRateHz = 474.9f; CHECK(!validVibration(v));
  v = vibration(); v.sampleRateHz = 525.1f; CHECK(!validVibration(v));
  v = vibration(); v.peak = v.rms - 0.01f; CHECK(!validVibration(v));
  v = vibration(); v.rms = NAN; CHECK(!validVibration(v));
  v = vibration(); v.rmsY = NAN; CHECK(!validVibration(v));
  v = vibration(); v.rmsZ = -0.1f; CHECK(!validVibration(v));
  v = vibration(0); v.crest = 0; CHECK(validVibration(v));
}

static void testBaselineAndStatistics() {
  static_assert(cfg::BASELINE_SAMPLES==10, "E-TWIN requiere 10 muestras por referencia");
  const auto good = reference(); CHECK(baselineValid(good));
  auto b = good; b.samples = cfg::BASELINE_SAMPLES - 1; CHECK(!baselineValid(b));
  b = good; b.schema = 0; CHECK(!baselineValid(b));
  b = good; b.power = 0; CHECK(!baselineValid(b));
  b = good; b.power = cfg::RUNNING_THRESHOLD_W; CHECK(!baselineValid(b));
  b = good; b.power = INFINITY; CHECK(!baselineValid(b));
  b = good; b.vibration = 0; CHECK(!baselineValid(b));
  b = good; b.vibration = cfg::MIN_BASELINE_VIBRATION / 2; CHECK(!baselineValid(b));
  b = good; b.vibration = cfg::MIN_BASELINE_VIBRATION; CHECK(baselineValid(b));
  b = good; b.vibration = NAN; CHECK(!baselineValid(b));

  RunningStats constant;
  for (int i = 0; i < cfg::BASELINE_SAMPLES; ++i) constant.add(100.0);
  CHECK(constant.n == cfg::BASELINE_SAMPLES && near(constant.mean, 100.0) && near(constant.cv(), 0));
  RunningStats known;
  for (double x : {1.0, 2.0, 3.0, 4.0, 5.0}) known.add(x);
  CHECK(known.n == 5 && near(known.mean, 3.0));
  CHECK(near(known.cv(), std::sqrt(2.5) / 3.0));
  RunningStats unstable;
  for (int i = 0; i < cfg::BASELINE_SAMPLES; ++i) unstable.add(i % 2 ? 150.0 : 50.0);
  CHECK(unstable.cv() > cfg::BASELINE_MAX_POWER_CV);
  CHECK(unstable.cv() > cfg::BASELINE_MAX_VIBRATION_CV);
  RunningStats offset;
  for (double x : {1e8 + 1, 1e8 + 2, 1e8 + 3}) offset.add(x);
  CHECK(near(offset.mean, 1e8 + 2));
  CHECK(near(offset.m2, 2.0)); // numerical stability at a large DC offset
}

static void testAnalysis() {
  auto e = sample(energy()); auto v = sample(vibration()); const auto b = reference();
  Sample<EnergyPacket> absentEnergy;
  Sample<VibrationPacket> absentVibration;
  auto a = analyze(absentEnergy, absentVibration, b, 1100);
  CHECK(a.state == MachineState::Wait && !a.energyValid && !a.vibrationValid);
  CHECK(std::isnan(a.apparentVA) && std::isnan(a.powerDeltaPercent));
  a = analyze(absentEnergy, v, b, 1100);
  CHECK(a.state == MachineState::Wait && !a.energyValid && a.vibrationValid && !a.running);
  a = analyze(e, absentVibration, b, 1100);
  CHECK(a.state == MachineState::Partial && a.energyValid && !a.vibrationValid);
  CHECK(std::isnan(a.vibrationRatio));
  a = analyze(e, v, b, 1100);
  CHECK(a.energyValid && a.vibrationValid && a.running && a.referenceValid);
  CHECK(a.state == MachineState::Run && near(a.apparentVA, 115.0));
  CHECK(near(a.powerDeltaPercent, 0) && near(a.vibrationRatio, 1));
  CHECK((a.alarms & ~SETUP) == 0);

  auto noReference = Baseline{};
  a = analyze(e, v, noReference, 1100);
  CHECK(!a.referenceValid && (a.alarms & SETUP));
  CHECK(std::isnan(a.powerDeltaPercent) && std::isnan(a.vibrationRatio));
  CHECK(a.state == MachineState::Run); // missing calibration is explicit, not a fake alert

  e.packet = energy(cfg::RUNNING_THRESHOLD_W - 1); e.packet.powerFactor = 0;
  a = analyze(e, v, b, 1100);
  CHECK(a.state == MachineState::Stop && !a.running);
  CHECK(!(a.alarms & LOW_PF));
  CHECK(std::isnan(a.powerDeltaPercent) && std::isnan(a.vibrationRatio));
  e.packet = energy(cfg::RUNNING_THRESHOLD_W);
  CHECK(analyze(e, v, b, 1100).running);

  e.packet = energy(130); a = analyze(e, v, b, 1100);
  CHECK(near(a.powerDeltaPercent, 30.0, 0.001) && (a.alarms & POWER_DEVIATION));
  CHECK(a.state == MachineState::Warn);
  e.packet = energy(75); a = analyze(e, v, b, 1100);
  CHECK(near(a.powerDeltaPercent, -25.0, 0.001) && !(a.alarms & POWER_DEVIATION));
  e.packet = energy(); e.packet.powerFactor = cfg::LOW_PF_WARNING - 0.1f;
  a = analyze(e, v, b, 1100);
  CHECK((a.alarms & LOW_PF) && a.state == MachineState::Warn);
  e.packet.power = cfg::PF_MIN_POWER_W - 1;
  CHECK(!(analyze(e, v, b, 1100).alarms & LOW_PF));

  e.packet = energy(); v.packet = vibration(b.vibration * cfg::VIBRATION_RATIO_WARNING);
  a = analyze(e, v, b, 1100);
  CHECK((a.alarms & VIBRATION_RATIO) && a.state == MachineState::Warn);
  v.packet = vibration(b.vibration * cfg::VIBRATION_RATIO_ALARM);
  a = analyze(e, v, b, 1100);
  CHECK(a.state == MachineState::Alarm);
  CHECK(analyze(e, v, b, 1100, true).state == MachineState::Alarm);

  // Fresh invalid packets must immediately replace previous good data.
  v.packet.h.flags = SENSOR_ERROR;
  a = analyze(e, v, b, 1100);
  CHECK(!a.vibrationValid && std::isnan(a.vibrationRatio) && a.state == MachineState::Partial);
  e.packet.h.flags = SENSOR_ERROR;
  a = analyze(e, v, b, 1100);
  CHECK(!a.energyValid && !a.running && a.state == MachineState::Wait);
  CHECK(std::isnan(a.apparentVA) && std::isnan(a.powerDeltaPercent));

  e = sample(energy()); v = sample(vibration());
  a = analyze(e, v, b, 1000 + cfg::SENSOR_TTL_MS);
  CHECK(!a.energyValid && !a.vibrationValid && a.state == MachineState::Wait);
  e.receivedMs = 1000 + cfg::SENSOR_TTL_MS;
  a = analyze(e, v, b, e.receivedMs);
  CHECK(a.energyValid && !a.vibrationValid && a.state == MachineState::Partial);
  v.receivedMs = e.receivedMs;
  CHECK(analyze(e, v, b, e.receivedMs, true).state == MachineState::Base);
  CHECK(std::strcmp(stateCode(MachineState::Partial), "PARTIAL") == 0);
  CHECK(std::strcmp(stateCode(MachineState::Base), "BASE") == 0);
}

int main() {
  testWire(); testSequenceAndFreshness(); testMeasurementValidity();
  testBaselineAndStatistics(); testAnalysis();
  std::printf("E-TWIN core: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
