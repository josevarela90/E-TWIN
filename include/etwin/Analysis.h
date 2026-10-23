#pragma once
#include "etwin/Protocol.h"
#include "config.h"

namespace etwin {
template<class T> struct Sample {
  T packet{};
  uint32_t receivedMs=0;
  bool seen=false;
  bool fresh(uint32_t now) const { return seen && now-receivedMs<cfg::SENSOR_TTL_MS; }
};
struct Baseline {
  uint32_t schema=0x45544231;
  float power=0, vibration=0;
  uint32_t samples=0, capturedEpoch=0;
};
inline bool baselineValid(const Baseline& b) {
  return b.schema==0x45544231 && b.samples>=cfg::BASELINE_SAMPLES
    && isfinite(b.power) && b.power>cfg::RUNNING_THRESHOLD_W
    && isfinite(b.vibration) && b.vibration>=cfg::MIN_BASELINE_VIBRATION;
}
enum class MachineState : uint8_t { Wait, Stop, Run, Warn, Alarm, Partial, Base };
enum AlarmBits : uint16_t { VOLTAGE=1, LOW_PF=2, POWER_DEVIATION=4, VIBRATION_RATIO=8, VIBRATION_ABSOLUTE=16, SETUP=32 };
struct Analysis {
  MachineState state=MachineState::Wait;
  bool energyValid=false,vibrationValid=false,running=false,referenceValid=false;
  uint16_t alarms=0;
  float apparentVA=NAN,powerDeltaPercent=NAN,vibrationRatio=NAN;
};
inline const char* stateCode(MachineState s) {
  switch(s) {
    case MachineState::Stop:return "STOP";
    case MachineState::Run:return "RUN";
    case MachineState::Warn:return "WARN";
    case MachineState::Alarm:return "ALARM";
    case MachineState::Partial:return "PARTIAL";
    case MachineState::Base:return "BASE";
    default:return "WAIT";
  }
}
inline const char* stateLabel(MachineState s) {
  switch(s) {
    case MachineState::Stop:return "REPOSO ELECTRICO";
    case MachineState::Run:return "EN MONITOREO";
    case MachineState::Warn:return "REVISAR TENDENCIA";
    case MachineState::Alarm:return "ALERTA ALTA";
    case MachineState::Partial:return "DATOS PARCIALES";
    case MachineState::Base:return "CAPTURANDO REFERENCIA";
    default:return "SIN DATO ELECTRICO";
  }
}
inline Analysis analyze(const Sample<EnergyPacket>& e,const Sample<VibrationPacket>& v,
                         const Baseline& b,uint32_t now,bool capturing=false) {
  Analysis a;
  a.energyValid=e.fresh(now) && validEnergy(e.packet);
  a.vibrationValid=v.fresh(now) && validVibration(v.packet);
  a.referenceValid=baselineValid(b);
  if(cfg::NOMINAL_VOLTAGE<=0 || !a.referenceValid) a.alarms|=SETUP;
  if(!a.energyValid) return a;
  a.apparentVA=e.packet.voltage*e.packet.current;
  a.running=e.packet.power>=cfg::RUNNING_THRESHOLD_W;
  if(cfg::NOMINAL_VOLTAGE>0 && fabsf(e.packet.voltage/cfg::NOMINAL_VOLTAGE-1)*100>cfg::VOLTAGE_TOLERANCE_PERCENT)
    a.alarms|=VOLTAGE;
  if(a.running && e.packet.power>=cfg::PF_MIN_POWER_W && e.packet.powerFactor<cfg::LOW_PF_WARNING)
    a.alarms|=LOW_PF;
  if(a.running && a.referenceValid) {
    a.powerDeltaPercent=(e.packet.power/b.power-1)*100;
    if(a.powerDeltaPercent>cfg::POWER_DEVIATION_WARNING_PERCENT) a.alarms|=POWER_DEVIATION;
    if(a.vibrationValid) {
      a.vibrationRatio=v.packet.rms/b.vibration;
      if(a.vibrationRatio>=cfg::VIBRATION_RATIO_WARNING) a.alarms|=VIBRATION_RATIO;
    }
  }
  if(a.vibrationValid && cfg::VIBRATION_ABSOLUTE_ALARM>0 && v.packet.rms>=cfg::VIBRATION_ABSOLUTE_ALARM)
    a.alarms|=VIBRATION_ABSOLUTE;
  if((a.alarms&VIBRATION_ABSOLUTE) || (isfinite(a.vibrationRatio)&&a.vibrationRatio>=cfg::VIBRATION_RATIO_ALARM)) a.state=MachineState::Alarm;
  else if(capturing) a.state=MachineState::Base;
  else if(a.alarms&~SETUP) a.state=MachineState::Warn;
  else if(!a.vibrationValid) a.state=MachineState::Partial;
  else if(!a.running) a.state=MachineState::Stop;
  else a.state=MachineState::Run;
  return a;
}

// Welford: varianza estable para rechazar una referencia tomada durante arranque.
struct RunningStats {
  uint32_t n=0; double mean=0,m2=0;
  void add(double x) { ++n; const double d=x-mean; mean+=d/n; m2+=d*(x-mean); }
  double cv() const { return n>1 && mean>0 ? sqrt(m2/(n-1))/mean : 0; }
};
} // namespace etwin
