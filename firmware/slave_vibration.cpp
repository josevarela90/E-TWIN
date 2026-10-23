#include <Arduino.h>
#include <M5Unified.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cmath>

#include "config.h"
#include "etwin/Protocol.h"
#include "etwin/SlaveRadio.h"
#include "etwin/VibrationFilter.h"

namespace {
constexpr uint8_t IMU_ADDRESS = 0x68;
constexpr int IMU_SDA = 38;
constexpr int IMU_SCL = 39;
constexpr uint32_t I2C_HZ = 400000;
constexpr uint8_t EXPECTED_WHOAMI = 0x19;  // Original ATOMS3 / MPU6886, not S3R.
constexpr uint32_t WARMUP_SAMPLES = 1500;
constexpr uint32_t RETRY_MS = 2000;
constexpr uint32_t REPORT_MS = 1000;
constexpr int FIFO_FRAME_BYTES = 8;  // Accel XYZ (6) AND temperature (2).
constexpr int BURST_FRAMES = 16;
constexpr float ACCEL_TO_MS2 = 9.80665f / 4096.0f;  // +-8 g = 4096 LSB/g.
constexpr int RAW_CLIP_LIMIT = 32112;               // 98% of full scale.
constexpr uint16_t BG = 0x0863;
constexpr uint16_t INK = 0xEF7D;
constexpr uint16_t MUTED = 0x9D75;
constexpr uint16_t CYAN = 0x3EFD;
constexpr uint16_t ORANGE = 0xFCA0;

static_assert(cfg::SAMPLE_RATE_HZ == 500, "MPU6886 registers and filter assume 500 Hz");
static_assert(cfg::VIBRATION_WINDOW_SAMPLES == 500, "Protocol requires 500 samples/window");

struct Report {
  etwin::VibrationPacket packet;
  uint32_t measuredAtMs;
};

QueueHandle_t reportQueue = nullptr;
etwin::SlaveRadio radio(etwin::Node::Vibration);
Report displayed{};
bool haveReport = false;
bool taskStarted = false;
uint32_t lastPaintMs = 0;
uint32_t lastFallbackSendMs = 0;

bool readRegisters(uint8_t reg, uint8_t* bytes, size_t length) {
  return M5.In_I2C.readRegister(IMU_ADDRESS, reg, bytes, length, I2C_HZ);
}

bool writeRegister(uint8_t reg, uint8_t value) {
  return M5.In_I2C.writeRegister8(IMU_ADDRESS, reg, value, I2C_HZ);
}

bool readEquals(uint8_t reg, uint8_t expected) {
  uint8_t value = 0;
  return readRegisters(reg, &value, 1) && value == expected;
}

bool initializeImu() {
  if (!readEquals(0x75, EXPECTED_WHOAMI)) return false;
  if (!writeRegister(0x6B, 0x80)) return false;  // Device reset.
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!writeRegister(0x6B, 0x01)) return false;  // Awake, gyro PLL clock.
  vTaskDelay(pdMS_TO_TICKS(50));
  if (!writeRegister(0x6C, 0x00) ||  // Accelerometer/gyro enabled.
      !writeRegister(0x1A, 0x42) ||  // FIFO stops at full; gyro DLPF=92 Hz.
      !writeRegister(0x1B, 0x00) ||  // Gyro FCHOICE_B=0: sample divider effective.
      !writeRegister(0x1C, 0x10) ||  // Accelerometer +-8 g.
      !writeRegister(0x1D, 0x02) ||  // Accel DLPF=99 Hz, 1 kHz internal rate.
      !writeRegister(0x19, 0x01) ||  // 1000 / (1+1) = 500 samples/second.
      !writeRegister(0x69, 0x02) ||  // Disable MPU6886 output-value limiting.
      !writeRegister(0x38, 0x10) ||  // Enable FIFO-overflow status.
      !writeRegister(0x23, 0x00) ||
      !writeRegister(0x6A, 0x00) ||
      !writeRegister(0x6A, 0x04)) return false;  // Reset FIFO while disabled.
  vTaskDelay(pdMS_TO_TICKS(2));
  if (!writeRegister(0x6A, 0x40) ||  // FIFO access enabled.
      !writeRegister(0x23, 0x08)) return false;  // Accel + temperature frames.
  return readEquals(0x1A, 0x42) && readEquals(0x1B, 0x00) &&
         readEquals(0x1C, 0x10) && readEquals(0x1D, 0x02) &&
         readEquals(0x19, 0x01) && readEquals(0x23, 0x08) &&
         readEquals(0x6A, 0x40);
}

etwin::VibrationPacket invalidPacket(uint16_t flags, uint16_t samples = 0,
                                     uint16_t missed = 0) {
  etwin::VibrationPacket packet{};
  packet.h.flags = flags;
  packet.rmsX = packet.rmsY = packet.rmsZ = packet.rms =
      packet.peak = packet.crest = NAN;
  packet.sampleRateHz = cfg::SAMPLE_RATE_HZ;
  packet.samples = samples;
  packet.missed = missed;
  return packet;
}

void publishReport(const etwin::VibrationPacket& packet, uint32_t& lastReportMs) {
  Report report{packet, millis()};
  xQueueOverwrite(reportQueue, &report);
  lastReportMs = report.measuredAtMs;
}

int16_t signed16(const uint8_t* data) {
  return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

// Only this task touches the IMU/I2C bus. TFT rendering and radio discovery run
// separately. The FIFO preserves evenly timed samples even while the UI draws.
void samplingTask(void*) {
  etwin::VibrationFilter filter(cfg::SAMPLE_RATE_HZ, 6.0f, 75.0f);
  bool ready = false;
  uint16_t pendingFault = etwin::SENSOR_ERROR;
  uint32_t warmupRemaining = WARMUP_SAMPLES;
  uint32_t lastTryMs = millis() - RETRY_MS;
  uint32_t lastDataMs = millis();
  uint32_t lastReportMs = millis() - REPORT_MS;
  uint16_t windowFlags = 0;
  int64_t firstSampleUs = 0;
  uint8_t burst[FIFO_FRAME_BYTES * BURST_FRAMES];
  constexpr int64_t SAMPLE_US = 1000000 / cfg::SAMPLE_RATE_HZ;

  for (;;) {
    const uint32_t now = millis();
    if (!ready) {
      if (static_cast<uint32_t>(now - lastTryMs) >= RETRY_MS) {
        lastTryMs = now;
        ready = initializeImu();
        if (ready) {
          filter.reset();
          warmupRemaining = WARMUP_SAMPLES;
          windowFlags = 0;
          lastDataMs = millis();
          pendingFault = etwin::WARMUP;
        } else {
          pendingFault = etwin::SENSOR_ERROR;
        }
      }
    } else {
      uint8_t status = 0;
      uint8_t countBytes[2] = {0, 0};
      if (!readRegisters(0x3A, &status, 1) ||
          !readRegisters(0x72, countBytes, 2)) {
        ready = false;
        pendingFault = etwin::SENSOR_ERROR;
      } else {
        const uint16_t byteCount = (static_cast<uint16_t>(countBytes[0]) << 8) | countBytes[1];
        if ((status & 0x10) || byteCount >= 1024) {
          ready = false;
          pendingFault = etwin::FIFO_OVERFLOW | etwin::TIMING_BAD;
        } else {
          // Read only complete 8-byte frames. A partial frame, if observed as
          // hardware appends data, remains in FIFO for the next poll.
          const int frameCount = byteCount / FIFO_FRAME_BYTES;
          const int64_t observedAtUs = esp_timer_get_time();
          int processed = 0;
          while (processed < frameCount && ready) {
            const int remaining = frameCount - processed;
            const int batch = remaining < BURST_FRAMES ? remaining : BURST_FRAMES;
            if (!readRegisters(0x74, burst, batch * FIFO_FRAME_BYTES)) {
              ready = false;
              pendingFault = etwin::SENSOR_ERROR | etwin::TIMING_BAD;
              break;
            }
            lastDataMs = millis();
            for (int i = 0; i < batch; ++i) {
              const uint8_t* frame = burst + i * FIFO_FRAME_BYTES;
              const int16_t rawX = signed16(frame);
              const int16_t rawY = signed16(frame + 2);
              const int16_t rawZ = signed16(frame + 4);
              const int64_t sampleUs = observedAtUs -
                  static_cast<int64_t>(frameCount - processed - i - 1) * SAMPLE_US;
              const bool warming = warmupRemaining != 0;
              if (!warming && filter.count() == 0) firstSampleUs = sampleUs;
              filter.process(rawX * ACCEL_TO_MS2, rawY * ACCEL_TO_MS2,
                             rawZ * ACCEL_TO_MS2, !warming);
              if (warming) {
                --warmupRemaining;
                if (warmupRemaining == 0)
                  publishReport(invalidPacket(etwin::WARMUP), lastReportMs);
                continue;
              }
              if (rawX >= RAW_CLIP_LIMIT || rawX <= -RAW_CLIP_LIMIT ||
                  rawY >= RAW_CLIP_LIMIT || rawY <= -RAW_CLIP_LIMIT ||
                  rawZ >= RAW_CLIP_LIMIT || rawZ <= -RAW_CLIP_LIMIT) {
                windowFlags |= etwin::CLIPPED;
              }
              if (filter.count() == cfg::VIBRATION_WINDOW_SAMPLES) {
                const etwin::VibrationSummary stats = filter.summary();
                const int64_t durationUs = sampleUs - firstSampleUs;
                etwin::VibrationPacket packet{};
                packet.rmsX = stats.rmsX;
                packet.rmsY = stats.rmsY;
                packet.rmsZ = stats.rmsZ;
                packet.rms = stats.rms;
                packet.peak = stats.peak;
                packet.crest = stats.crest;
                packet.samples = stats.samples;
                packet.sampleRateHz = durationUs > 0
                    ? static_cast<float>(stats.samples - 1) * 1000000.0f / durationUs
                    : 0.0f;
                if (packet.sampleRateHz < 475 || packet.sampleRateHz > 525)
                  windowFlags |= etwin::TIMING_BAD;
                packet.h.flags = windowFlags ? windowFlags : etwin::VALID;
                publishReport(packet, lastReportMs);
                filter.resetWindow();
                windowFlags = 0;
                pendingFault = 0;
              }
            }
            processed += batch;
          }
          if (static_cast<uint32_t>(millis() - lastDataMs) > 500) {
            ready = false;
            pendingFault = etwin::SENSOR_ERROR | etwin::TIMING_BAD;
          }
        }
      }

      if (!ready) {
        // A partial/overflowed window is never promoted to a valid reading.
        const uint16_t missed = (pendingFault & etwin::FIFO_OVERFLOW) ? 65535 : 1;
        publishReport(invalidPacket(pendingFault, filter.count(), missed), lastReportMs);
        filter.reset();
        warmupRemaining = WARMUP_SAMPLES;
        windowFlags = 0;
        lastTryMs = millis();
      }
    }

    // A hardware sample clock can differ slightly from exactly 500.000 Hz.
    // Do not inject an error heartbeat just before an otherwise complete window.
    const uint32_t heartbeatMs = ready && !warmupRemaining ? 1500 : REPORT_MS;
    if (static_cast<uint32_t>(millis() - lastReportMs) >= heartbeatMs) {
      const uint16_t flags = ready
          ? (warmupRemaining ? etwin::WARMUP : etwin::TIMING_BAD)
          : pendingFault;
      publishReport(invalidPacket(flags, filter.count()), lastReportMs);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

const char* qualityLabel(uint16_t flags) {
  if (flags & etwin::FIFO_OVERFLOW) return "FIFO LLENO";
  if (flags & etwin::SENSOR_ERROR) return "ERROR IMU";
  if (flags & etwin::CLIPPED) return "SATURACION";
  if (flags & etwin::WARMUP) return "ESTABILIZANDO";
  if (flags & etwin::TIMING_BAD) return "TIEMPO INVALIDO";
  return "SIN DATOS";
}

void paintDisplay() {
  const bool fresh = haveReport &&
      static_cast<uint32_t>(millis() - displayed.measuredAtMs) < 2500;
  const bool valid = fresh && etwin::validVibration(displayed.packet);
  M5.Display.startWrite();
  M5.Display.fillScreen(BG);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(CYAN, BG);
  M5.Display.setCursor(6, 5);
  M5.Display.print("E-TWIN  /  NODO 2");
  M5.Display.drawFastHLine(6, 17, 116, 0x220A);
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(6, 24);
  M5.Display.print("VIBRACION RMS");
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(valid ? INK : ORANGE, BG);
  M5.Display.setCursor(6, 39);
  if (valid) M5.Display.printf("%.3f", displayed.packet.rms);
  else M5.Display.print("--.---");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(6, 59);
  M5.Display.print("m/s2 | filtro 6-75");
  M5.Display.setTextColor(INK, BG);
  M5.Display.setCursor(6, 74);
  if (valid) M5.Display.printf("Pico: %.3f m/s2", displayed.packet.peak);
  else M5.Display.print(fresh ? qualityLabel(displayed.packet.h.flags) : "SIN DATOS RECIENTES");
  M5.Display.setTextColor(MUTED, BG);
  M5.Display.setCursor(6, 89);
  if (valid) M5.Display.printf("%.0f Hz  /  %u datos", displayed.packet.sampleRateHz,
                               displayed.packet.samples);
  else M5.Display.print("MPU6886  +/-8 g");
  M5.Display.drawFastHLine(6, 104, 116, 0x220A);
  const bool ackRecent = radio.linked() && radio.lastAckMs() != 0 &&
      static_cast<uint32_t>(millis() - radio.lastAckMs()) < 3000;
  M5.Display.setTextColor(ackRecent ? CYAN : ORANGE, BG);
  M5.Display.setCursor(6, 113);
  M5.Display.printf("%s / ch %u", ackRecent ? "ACK OK" :
                   radio.linked() ? "SIN ACK" : "BUSCANDO", radio.channel());
  M5.Display.endWrite();
}
}  // namespace

void setup() {
  auto m5cfg = M5.config();
  m5cfg.serial_baudrate = 115200;
  m5cfg.fallback_board = m5::board_t::board_M5AtomS3;
  m5cfg.internal_imu = false;  // Exclusive raw/FIFO access in samplingTask.
  m5cfg.internal_rtc = false;
  m5cfg.internal_mic = false;
  m5cfg.internal_spk = false;
  m5cfg.external_imu = false;
  m5cfg.external_rtc = false;
  m5cfg.external_display_value = 0;
  M5.begin(m5cfg);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(100);
  M5.Display.setTextWrap(false);
  M5.In_I2C.begin(I2C_NUM_1, IMU_SDA, IMU_SCL);
  Serial.println("\nE-TWIN / esclavo 2 / ATOMS3 original / MPU6886");
  Serial.println("FIFO500Hz; HP6Hz 4o + LP75Hz 4o; DLPF99Hz; ventana500; unidades m/s2.");
  Serial.println("Montaje rigido. No son mm/s, ni diagnostico ISO, ni eficiencia.");
  if (!radio.begin()) Serial.println("ESP-NOW no iniciado: revisar MAC/configuracion.");
  reportQueue = xQueueCreate(1, sizeof(Report));
  if (reportQueue) {
    taskStarted = xTaskCreatePinnedToCore(samplingTask, "vibration", 6144,
                                          nullptr, 3, nullptr, 1) == pdPASS;
  }
  if (!taskStarted) Serial.println("No se pudo crear tarea/cola de muestreo.");
  paintDisplay();
}

void loop() {
  M5.update();
  radio.update();
  Report report{};
  if (taskStarted && xQueueReceive(reportQueue, &report, 0) == pdTRUE) {
    displayed = report;
    haveReport = true;
    radio.send(report.packet);
    if (etwin::validVibration(report.packet)) {
      Serial.printf("RMS=%.4f pico=%.4f m/s2; X=%.4f Y=%.4f Z=%.4f; fs=%.2f n=%u ch=%u\n",
                    report.packet.rms, report.packet.peak, report.packet.rmsX,
                    report.packet.rmsY, report.packet.rmsZ,
                    report.packet.sampleRateHz, report.packet.samples, radio.channel());
    } else {
      Serial.printf("Vibracion invalida: flags=0x%02X %s n=%u\n",
                    report.packet.h.flags, qualityLabel(report.packet.h.flags), report.packet.samples);
    }
  }
  if (!taskStarted && static_cast<uint32_t>(millis() - lastFallbackSendMs) >= REPORT_MS) {
    lastFallbackSendMs = millis();
    report.packet = invalidPacket(etwin::SENSOR_ERROR);
    report.measuredAtMs = millis();
    displayed = report;
    haveReport = true;
    radio.send(report.packet);
  }
  if (static_cast<uint32_t>(millis() - lastPaintMs) >= 250) {
    lastPaintMs = millis();
    paintDisplay();
  }
  delay(2);
}
