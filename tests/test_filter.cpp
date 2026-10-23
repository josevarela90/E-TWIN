#include "etwin/VibrationFilter.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
constexpr double PI = 3.14159265358979323846;

float sinusoidRms(float frequency) {
  etwin::VibrationFilter filter;
  // 1 m/s^2 peak on X; gravity on Z must disappear after warm-up.
  for (int i = 0; i < 1000; ++i)
    filter.process(std::sin(2 * PI * frequency * i / 500), 0, 9.80665f, false);
  for (int i = 1000; i < 1500; ++i)
    filter.process(std::sin(2 * PI * frequency * i / 500), 0, 9.80665f);
  const auto result = filter.summary();
  assert(result.samples == 500);
  assert(std::abs(result.rms - result.rmsX) < 1e-5);
  assert(result.rmsZ < 0.001f);
  assert(result.peak >= result.rms);
  std::printf("sine %6.1f Hz rms %.6f peak %.6f crest %.4f z %.8f\n",
              frequency, result.rms, result.peak, result.crest, result.rmsZ);
  return result.rms;
}
}  // namespace

int main() {
  etwin::VibrationFilter filter;
  for (int i = 0; i < 1000; ++i) filter.process(1, 2, 9.80665f, false);
  assert(filter.count() == 0);  // Warm-up never counts as a measurement.
  for (int i = 0; i < 500; ++i) filter.process(1, 2, 9.80665f);
  auto result = filter.summary();
  assert(result.samples == 500);
  assert(result.rms < 0.001f);  // Gravity/DC removed on all axes.
  std::printf("DC residual %.9f m/s2\n", result.rms);

  // resetWindow must preserve the settled filter state.
  filter.resetWindow();
  for (int i = 0; i < 500; ++i) filter.process(1, 2, 9.80665f);
  assert(filter.summary().rms < 0.001f);

  const float at1 = sinusoidRms(1);
  const float at6 = sinusoidRms(6);
  const float at20 = sinusoidRms(20);
  const float at60 = sinusoidRms(60);
  const float at75 = sinusoidRms(75);
  const float at200 = sinusoidRms(200);
  assert(at1 < 0.01f);                       // Below high-pass cutoff.
  assert(at6 > 0.45f && at6 < 0.53f);       // -3 dB cerca 6 Hz.
  assert(at20 > 0.69f && at20 < 0.72f);      // Approximately 1/sqrt(2) RMS.
  assert(at60 > 0.64f && at60 < 0.71f);
  assert(at75 > 0.45f && at75 < 0.53f);     // -3 dB cerca 75 Hz.
  assert(at200 < 0.008f);                     // Above low-pass cutoff.

  // Circular motion has constant vector magnitude. This detects incorrect
  // magnitude-minus-gravity approaches, which would lose transverse motion.
  etwin::VibrationFilter vectorFilter;
  for (int i = 0; i < 1500; ++i) {
    const float angle = static_cast<float>(2 * PI * 20 * i / 500);
    vectorFilter.process(std::sin(angle), std::cos(angle), 9.80665f, i >= 1000);
  }
  const auto vector = vectorFilter.summary();
  assert(vector.rms > 0.98f && vector.rms < 1.02f);
  assert(vector.crest > 0.99f && vector.crest < 1.02f);

  filter.reset();
  assert(filter.count() == 0);
  filter.process(0, 0, 0, false);
  assert(filter.count() == 0);
  filter.process(0, 0, 0);
  result = filter.summary();
  assert(result.rms == 0 && result.peak == 0 && result.crest == 0);
  std::puts("PASS: DC rejection, frequency response, vector RMS, finite zero, windows, reset.");
}
