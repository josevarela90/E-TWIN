#pragma once

#include <cmath>
#include <cstdint>

namespace etwin {

// E-TWIN 2.0: Butterworth 4o orden pasaaltos (6 Hz) + 4o orden pasabajos
// (75 Hz), sobre los ejes FIRMADOS de aceleracion a 500 Hz.
// La IMU tambien aplica filtro HW; NO se realizan 'correcciones' de amplitud
// ni se eliminan impactos reales usando clamps/medianas.
class Biquad {
 public:
  void configure(float fs, float cutoff, bool highPass, double q) {
    const double w = 6.2831853071795864769 * cutoff / fs;
    const double c = std::cos(w);
    const double a = std::sin(w) / (2.0 * q);
    const double den = 1.0 + a;
    b0_ = float((highPass ? 1.0 + c : 1.0 - c) / (2.0 * den));
    b1_ = highPass ? -2 * b0_ : 2 * b0_;
    b2_ = b0_;
    a1_ = float((-2.0 * c) / den);
    a2_ = float((1.0 - a) / den);
    reset();
  }
  float process(float x) {
    const float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
  }
  void reset() { z1_ = z2_ = 0; }
 private:
  float b0_=0,b1_=0,b2_=0,a1_=0,a2_=0,z1_=0,z2_=0;
};

struct VibrationSummary {
  float rmsX=0,rmsY=0,rmsZ=0,rms=0,peak=0,crest=0;
  uint16_t samples=0;
};

class VibrationFilter {
 public:
  explicit VibrationFilter(float fs=500.0f, float highHz=6.0f, float lowHz=75.0f) {
    // Secciones Q de un Butterworth de cuarto orden. 2 etapas por flanco.
    static constexpr double q[2] = {0.541196100146197, 1.306562964876377};
    for (unsigned axis=0;axis<3;++axis)
      for (unsigned step=0;step<2;++step) {
        hp_[axis][step].configure(fs,highHz,true,q[step]);
        lp_[axis][step].configure(fs,lowHz,false,q[step]);
      }
    resetWindow();
  }
  void reset() {
    for (unsigned a=0;a<3;++a) for (unsigned j=0;j<2;++j) { hp_[a][j].reset(); lp_[a][j].reset(); }
    resetWindow();
  }
  void resetWindow() { sums_[0]=sums_[1]=sums_[2]=0; peakSquared_=0; count_=0; }
  void process(float ax,float ay,float az,bool accumulate=true) {
    const float input[3]={ax,ay,az};
    double magnitude2=0;
    for (unsigned a=0;a<3;++a) {
      float v=input[a];
      for (unsigned j=0;j<2;++j) v=hp_[a][j].process(v);
      for (unsigned j=0;j<2;++j) v=lp_[a][j].process(v);
      const double square=double(v)*v;
      if (accumulate) sums_[a]+=square;
      magnitude2+=square;
    }
    if (accumulate) { if(magnitude2>peakSquared_)peakSquared_=magnitude2; ++count_; }
  }
  uint16_t count() const { return count_; }
  VibrationSummary summary() const {
    VibrationSummary r; r.samples=count_;
    if (!count_) return r;
    r.rmsX=float(std::sqrt(sums_[0]/count_));
    r.rmsY=float(std::sqrt(sums_[1]/count_));
    r.rmsZ=float(std::sqrt(sums_[2]/count_));
    r.rms=float(std::sqrt((sums_[0]+sums_[1]+sums_[2])/count_));
    r.peak=float(std::sqrt(peakSquared_));
    if (r.peak<r.rms) r.peak=r.rms;
    r.crest=r.rms>1.e-7f ? r.peak/r.rms : 0;
    return r;
  }
 private:
  Biquad hp_[3][2],lp_[3][2];
  double sums_[3]={0,0,0},peakSquared_=0;
  uint16_t count_=0;
};
}
