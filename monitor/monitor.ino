#define USE_CURRENT 0
#define USE_TLS 0
#define FIRMWARE_VERSION "3.0.0-adaptive"

const char* WIFI_SSID = "Accio Internet";
const char* WIFI_PASSWORD = "youre a wizard harry";

const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = USE_TLS ? 8883 : 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "";
const char* TOPIC_PREFIX = "fanmon_x7k29q4";
const char* DEVICE_ID = "fan-01";

const char* TELEGRAM_TOKEN = "";
const char* TELEGRAM_CHAT_ID = "";

#include <Arduino.h>
#include <esp_system.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#if USE_CURRENT
#include <Adafruit_ADS1X15.h>
#endif
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "features.h"

constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
constexpr int PIN_ONEWIRE = 4;
constexpr int PIN_RELAY = 26;
constexpr int PIN_LED_RED = 25;
constexpr int PIN_LED_YEL = 33;
constexpr int PIN_LED_GRN = 32;
constexpr int PIN_BUZZER = 27;

constexpr uint8_t MPU6050_ADDR = 0x68;
constexpr uint8_t MPU6050_REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t MPU6050_REG_CONFIG = 0x1A;
constexpr uint8_t MPU6050_REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t MPU6050_REG_ACCEL_XOUT_H = 0x3B;
constexpr float ACCEL_SCALE = 9.80665f / 4096.0f;

#define DEMO_MODE 1

#if DEMO_MODE
constexpr int CRIT_WINDOW_COUNT_REQ = 1;
constexpr int WARN_WINDOW_COUNT_REQ = 1;
constexpr int NORM_RECOVERY_COUNT_REQ = 3;
#else
constexpr int CRIT_WINDOW_COUNT_REQ = 2;
constexpr int WARN_WINDOW_COUNT_REQ = 2;
constexpr int NORM_RECOVERY_COUNT_REQ = 5;
#endif

const float WARN_VIB_SIGMA_MULT = 3.0f;
const float WARN_VIB_RATIO_MIN = 1.5f;
const float WARN_TEMP_RISE_DEG = 8.0f;
const float WARN_BAND_RATIO = 3.0f;
const float CRIT_VIB_RATIO = 3.0f;
const float CRIT_VIB_ABS_MIN = 1.5f;

constexpr int LEARNING_TOTAL_WINDOWS = 140;

enum HealthState {
  STATE_NORMAL = 0,
  STATE_WARNING = 1,
  STATE_CRITICAL = 2,
  STATE_CALIBRATING = 3,
  STATE_SENSOR_ERROR = 4
};
constexpr HealthState STATE_LEARNING = STATE_CALIBRATING;

enum FanMode {
  FAN_MODE_AUTO = 0,
  FAN_MODE_ON = 1,
  FAN_MODE_OFF = 2
};

constexpr int TREND_HISTORY_SIZE = 60;
struct TrendPoint {
  uint32_t timestamp_s;
  float temp_c;
  float vib_rms;
};

struct TelemetrySnapshot {
  uint32_t timestamp_s;
  float temp_c;
  float vib_rms;
  float current_a;
  HealthState state;
  char state_reason[24];
  float reason_val;
  float reason_limit;
  bool latched;
  bool baseline_ready;
  int learning_pct;
  float dominant_freq_hz;
  int rise_band;
  char hint[64];
  float health_score;
  float ttc_min;
  bool fan_on;
  FanMode mode;
  uint32_t free_heap;
  bool backfilled;
};

constexpr int RING_BUFFER_SIZE = 300;
TelemetrySnapshot ringBuffer[RING_BUFFER_SIZE];
int ringHead = 0;
int ringTail = 0;
int ringCount = 0;
portMUX_TYPE ringBufferMux = portMUX_INITIALIZER_UNLOCKED;

void enqueueTelemetry(const TelemetrySnapshot &item) {
  taskENTER_CRITICAL(&ringBufferMux);
  ringBuffer[ringHead] = item;
  ringHead = (ringHead + 1) % RING_BUFFER_SIZE;
  if (ringCount < RING_BUFFER_SIZE) {
    ringCount++;
  } else {
    ringTail = (ringTail + 1) % RING_BUFFER_SIZE;
  }
  taskEXIT_CRITICAL(&ringBufferMux);
}

bool dequeueTelemetry(TelemetrySnapshot &outItem) {
  taskENTER_CRITICAL(&ringBufferMux);
  if (ringCount == 0) {
    taskEXIT_CRITICAL(&ringBufferMux);
    return false;
  }
  outItem = ringBuffer[ringTail];
  ringTail = (ringTail + 1) % RING_BUFFER_SIZE;
  ringCount--;
  taskEXIT_CRITICAL(&ringBufferMux);
  return true;
}

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature tempSensor(&oneWire);
Preferences prefs;
#if USE_TLS
WiFiClientSecure secureEspClient;
#else
WiFiClient plainEspClient;
#endif
PubSubClient mqttClient;

const char* deviceId = DEVICE_ID;
const char* mqttHost = MQTT_HOST;
const int   mqttPort = MQTT_PORT;
const char* mqttUser = MQTT_USER;
const char* mqttPass = MQTT_PASS;
const char* topicPrefix = TOPIC_PREFIX;
const char* telegramToken = TELEGRAM_TOKEN;
const char* telegramChatId = TELEGRAM_CHAT_ID;
bool useTls = (USE_TLS != 0);

float limitMaxTemp = 60.0f;
float limitMaxVibRms = 4.0f;
float limitMaxCurrent = 1.0f;

constexpr int BASELINE_VERSION = 2;
bool baselineReady = false;
float baseVibMean = 0.20f;
float baseVibStd = 0.05f;
float baseTempMean = 30.0f;
float baseTempStd = 1.0f;
float baseBandMean[4] = {0.1f, 0.1f, 0.1f, 0.1f};
float baseBandStd[4] = {0.02f, 0.02f, 0.02f, 0.02f};
float baseDomFreqMean = 30.0f;

volatile bool isLatched = false;
char latchedReason[24] = "none";
float latchedVal = 0.0f;
float latchedLimit = 0.0f;
volatile FanMode fanMode = FAN_MODE_AUTO;
volatile bool buzzerMuted = false;
volatile bool triggerLearning = false;
unsigned long lastCommandTime = 0;
constexpr unsigned long COMMAND_WATCHDOG_TIMEOUT_MS = 60000;

double totalEnergyKwh = 0.0;
float electricityTariff = 0.15f;

SemaphoreHandle_t telemetryMutex;
TelemetrySnapshot latestTelemetry;
TaskHandle_t taskCore1Handle = NULL;
TaskHandle_t taskCore0Handle = NULL;

static const char* getResetReasonMeaning(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "Power-on event";
    case ESP_RST_EXT: return "External pin reset";
    case ESP_RST_SW: return "Software reset (esp_restart)";
    case ESP_RST_PANIC: return "Software exception/panic";
    case ESP_RST_INT_WDT: return "Interrupt watchdog reset";
    case ESP_RST_TASK_WDT: return "Task watchdog reset";
    case ESP_RST_WDT: return "Other watchdog reset";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT: return "Brownout reset (voltage drop)";
    case ESP_RST_SDIO: return "SDIO reset";
    default: return "Unknown reset reason";
  }
}

static const char* getMqttStateMeaning(int state) {
  switch (state) {
    case -4: return "connection timeout";
    case -3: return "connection lost";
    case -2: return "connect failed";
    case -1: return "disconnected";
    case 0: return "connected";
    case 1: return "bad protocol";
    case 2: return "bad client id";
    case 3: return "unavailable";
    case 4: return "bad credentials";
    case 5: return "unauthorized";
    default: return "unknown";
  }
}

HealthState lastAlertedState = STATE_NORMAL;
unsigned long lastTelegramAlertMs = 0;
constexpr unsigned long TELEGRAM_COOLDOWN_MS = 60000;

static bool writeMpuRegister(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(data);
  return (Wire.endTransmission() == 0);
}

static bool initMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  if (Wire.endTransmission() != 0) return false;
  if (!writeMpuRegister(MPU6050_REG_PWR_MGMT_1, 0x00)) return false;
  delay(10);
  if (!writeMpuRegister(MPU6050_REG_CONFIG, 0x04)) return false;
  if (!writeMpuRegister(MPU6050_REG_ACCEL_CONFIG, 0x10)) return false;
  return true;
}

static bool readMpuAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t bytesReceived = Wire.requestFrom((int)MPU6050_ADDR, 6);
  if (bytesReceived != 6) return false;

  int16_t rawX = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rawY = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rawZ = (int16_t)((Wire.read() << 8) | Wire.read());

  ax = (float)rawX * ACCEL_SCALE;
  ay = (float)rawY * ACCEL_SCALE;
  az = (float)rawZ * ACCEL_SCALE;
  return true;
}

static float calculateTimeToCritical(const TrendPoint* history, int count, float limit, bool isTemp) {
  if (count < 10) return -1.0f;

  double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
  for (int i = 0; i < count; i++) {
    double x = (double)(history[i].timestamp_s - history[0].timestamp_s);
    double y = (double)(isTemp ? history[i].temp_c : history[i].vib_rms);
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
    sumY2 += y * y;
  }

  double n = (double)count;
  double denom = (n * sumX2 - sumX * sumX);
  if (fabs(denom) < 1e-6) return -1.0f;

  double slope = (n * sumXY - sumX * sumY) / denom;
  double intercept = (sumY - slope * sumX) / n;

  double ssTotal = sumY2 - (sumY * sumY) / n;
  double ssRes = 0.0;
  for (int i = 0; i < count; i++) {
    double x = (double)(history[i].timestamp_s - history[0].timestamp_s);
    double y = (double)(isTemp ? history[i].temp_c : history[i].vib_rms);
    double yPred = slope * x + intercept;
    ssRes += (y - yPred) * (y - yPred);
  }
  double r2 = (ssTotal > 1e-6) ? (1.0 - (ssRes / ssTotal)) : 0.0;

  if (slope > 0.0005 && r2 > 0.60) {
    double currentVal = (double)(isTemp ? history[count - 1].temp_c : history[count - 1].vib_rms);
    if (currentVal < limit) {
      double secondsRemaining = (limit - currentVal) / slope;
      float minutesRemaining = (float)(secondsRemaining / 60.0);
      if (minutesRemaining >= 0.5f && minutesRemaining <= 240.0f) {
        return minutesRemaining;
      }
    }
  }
  return -1.0f;
}

void printBaseline(const char* tag) {
  Serial.printf("[%s BASELINE] Vib RMS: mean=%.4f, std=%.4f\n", tag, baseVibMean, baseVibStd);
  Serial.printf("[%s BASELINE] Temp: mean=%.2f, std=%.2f\n", tag, baseTempMean, baseTempStd);
  for (int k = 0; k < 4; k++) {
    Serial.printf("[%s BASELINE] Band %d: mean=%.4f, std=%.4f\n", tag, k, baseBandMean[k], baseBandStd[k]);
  }
  Serial.printf("[%s BASELINE] Dom Freq: mean=%.2f Hz\n", tag, baseDomFreqMean);
}

void loadBaseline() {
  prefs.begin("fan_base", false);
  baselineReady = prefs.getBool("ready", false);
  int savedVer = prefs.getInt("ver", 0);
  float savedVibM = prefs.getFloat("vib_m", 0.20f);

  if (baselineReady && (savedVer != BASELINE_VERSION || savedVibM > 0.5f)) {
    Serial.println("Old baseline discarded, recalibrating");
    baselineReady = false;
    prefs.putBool("ready", false);
  }

  if (baselineReady) {
    baseVibMean = savedVibM;
    baseVibStd = prefs.getFloat("vib_s", 0.05f);
    baseTempMean = prefs.getFloat("tmp_m", 30.0f);
    baseTempStd = prefs.getFloat("tmp_s", 1.0f);
    baseBandMean[0] = prefs.getFloat("b0_m", 0.1f);
    baseBandStd[0] = prefs.getFloat("b0_s", 0.02f);
    baseBandMean[1] = prefs.getFloat("b1_m", 0.1f);
    baseBandStd[1] = prefs.getFloat("b1_s", 0.02f);
    baseBandMean[2] = prefs.getFloat("b2_m", 0.1f);
    baseBandStd[2] = prefs.getFloat("b2_s", 0.02f);
    baseBandMean[3] = prefs.getFloat("b3_m", 0.1f);
    baseBandStd[3] = prefs.getFloat("b3_s", 0.02f);
    baseDomFreqMean = prefs.getFloat("freq_m", 30.0f);

    baseVibStd = max(baseVibStd, max(0.05f * baseVibMean, 0.02f));
    baseTempStd = max(baseTempStd, max(0.05f * baseTempMean, 0.5f));
    for (int k = 0; k < 4; k++) {
      baseBandStd[k] = max(baseBandStd[k], max(0.05f * baseBandMean[k], 0.01f));
    }
    prefs.end();
    printBaseline("BOOT");
  } else {
    prefs.end();
    Serial.println("[BOOT BASELINE] Incomplete or uncalibrated baseline; entering calibration mode");
  }
}

void saveBaseline() {
  prefs.begin("fan_base", false);
  prefs.putBool("ready", true);
  prefs.putInt("ver", BASELINE_VERSION);
  prefs.putFloat("vib_m", baseVibMean);
  prefs.putFloat("vib_s", baseVibStd);
  prefs.putFloat("tmp_m", baseTempMean);
  prefs.putFloat("tmp_s", baseTempStd);
  prefs.putFloat("b0_m", baseBandMean[0]);
  prefs.putFloat("b0_s", baseBandStd[0]);
  prefs.putFloat("b1_m", baseBandMean[1]);
  prefs.putFloat("b1_s", baseBandStd[1]);
  prefs.putFloat("b2_m", baseBandMean[2]);
  prefs.putFloat("b2_s", baseBandStd[2]);
  prefs.putFloat("b3_m", baseBandMean[3]);
  prefs.putFloat("b3_s", baseBandStd[3]);
  prefs.putFloat("freq_m", baseDomFreqMean);
  prefs.end();
  baselineReady = true;
  printBaseline("CALIBRATED");
}

#define RELAY_RELEASE_FOR_OFF 0

void setRelay(bool on) {
  if (on) {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    Serial.println("RELAY ON");
  } else {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH);
#if RELAY_RELEASE_FOR_OFF
    pinMode(PIN_RELAY, INPUT);
#endif
    Serial.println("RELAY OFF");
  }
}

void connectMqtt();
void publishTelemetry(const TelemetrySnapshot &snap);
void sendTelegramAlert(HealthState newState, const TelemetrySnapshot &snap);
void mqttCallback(char* topic, byte* payload, unsigned int length);

void TaskCore1_SamplingAndDecision(void *pvParameters) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  bool mpuOk = initMPU6050();
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  tempSensor.requestTemperatures();

  loadBaseline();

  float rawAx[128], rawAy[128], rawAz[128];
  float bandEnergies[4];
  float dominantFreq = 0.0f;
  float vibRms = 0.0f;
  bool firstWindowProcessed = false;

  bool inLearningMode = !baselineReady;
  int learningWindowCount = 0;
  double sumVib = 0, sumVibSq = 0;
  double sumTemp = 0, sumTempSq = 0;
  double sumBands[4] = {0, 0, 0, 0};
  double sumBandsSq[4] = {0, 0, 0, 0};
  double sumFreq = 0;

  HealthState currentState = inLearningMode ? STATE_CALIBRATING : STATE_NORMAL;
  int consecutiveCritCount = 0;
  int consecutiveWarnCount = 0;
  int consecutiveNormCount = 0;
  float smoothedHealthScore = 100.0f;

  TrendPoint trendHistory[TREND_HISTORY_SIZE];
  int trendCount = 0;
  float lastTempC = 25.0f;
  float currentTempC = 25.0f;
  char stateReason[24] = "none";
  int learningPct = 0;
  float peakVibSinceStatus = 0.0f;
  uint16_t sampleIdx = 0;
  uint32_t lastSampleMicros = micros();
  uint32_t lastStatusPrintMs = 0;
  uint32_t lastRelaySwitchMs = millis();

  for (;;) {
    uint32_t nowMicros = micros();
    uint32_t elapsed = nowMicros - lastSampleMicros;
    if (elapsed < 10000) {
      uint32_t remaining = 10000 - elapsed;
      if (remaining > 2000) {
        vTaskDelay(pdMS_TO_TICKS(1));
      } else {
        taskYIELD();
      }
      continue;
    }
    lastSampleMicros += 10000;
    if ((nowMicros - lastSampleMicros) > 20000) {
      lastSampleMicros = nowMicros;
    }

    float ax = 0, ay = 0, az = 0;
    bool sampleOk = mpuOk && readMpuAccel(ax, ay, az);
    if (!sampleOk) {
      Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
      mpuOk = initMPU6050();
    }

    rawAx[sampleIdx] = ax;
    rawAy[sampleIdx] = ay;
    rawAz[sampleIdx] = az;
    sampleIdx++;

    if (sampleIdx >= 128) {
      sampleIdx = 0;

      float tempC = tempSensor.getTempCByIndex(0);
      if (tempC < -50.0f || tempC > 125.0f || !isfinite(tempC)) {
        tempC = lastTempC;
      }
      tempSensor.requestTemperatures();
      currentTempC = tempC;

      float currentA = 0.0f;
      bool sensorsHealthy = mpuOk && isfinite(tempC);

      compute_vibration_metrics(rawAx, rawAy, rawAz, &vibRms, bandEnergies, &dominantFreq);
      firstWindowProcessed = true;
      lastTempC = tempC;
      if (vibRms > peakVibSinceStatus) {
        peakVibSinceStatus = vibRms;
      }

      if (triggerLearning) {
        triggerLearning = false;
        inLearningMode = true;
        baselineReady = false;
        prefs.begin("fan_base", false);
        prefs.putBool("ready", false);
        prefs.end();
        learningWindowCount = 0;
        sumVib = sumVibSq = 0;
        sumTemp = sumTempSq = 0;
        sumFreq = 0;
        for (int k = 0; k < 4; k++) sumBands[k] = sumBandsSq[k] = 0;
        currentState = STATE_CALIBRATING;
        Serial.println("[CALIBRATING] Starting 140-window baseline acquisition, NVS ready set to false");
      }

      learningPct = 100;
      if (inLearningMode) {
        learningWindowCount++;
        learningPct = (learningWindowCount * 100) / LEARNING_TOTAL_WINDOWS;
        sumVib += vibRms;
        sumVibSq += (vibRms * vibRms);
        sumTemp += tempC;
        sumTempSq += (tempC * tempC);
        sumFreq += dominantFreq;
        for (int k = 0; k < 4; k++) {
          sumBands[k] += bandEnergies[k];
          sumBandsSq[k] += (bandEnergies[k] * bandEnergies[k]);
        }

        if (learningWindowCount >= LEARNING_TOTAL_WINDOWS) {
          inLearningMode = false;
          double n = (double)LEARNING_TOTAL_WINDOWS;
          baseVibMean = (float)(sumVib / n);
          baseVibStd = sqrtf(max(0.0f, (float)((sumVibSq / n) - (baseVibMean * baseVibMean))));
          baseVibStd = max(baseVibStd, max(0.05f * baseVibMean, 0.02f));

          baseTempMean = (float)(sumTemp / n);
          baseTempStd = sqrtf(max(0.0f, (float)((sumTempSq / n) - (baseTempMean * baseTempMean))));
          baseTempStd = max(baseTempStd, max(0.05f * baseTempMean, 0.5f));

          baseDomFreqMean = (float)(sumFreq / n);

          for (int k = 0; k < 4; k++) {
            baseBandMean[k] = (float)(sumBands[k] / n);
            baseBandStd[k] = sqrtf(max(0.0f, (float)((sumBandsSq[k] / n) - (baseBandMean[k] * baseBandMean[k]))));
            baseBandStd[k] = max(baseBandStd[k], max(0.05f * baseBandMean[k], 0.01f));
          }

          saveBaseline();
          currentState = STATE_NORMAL;
        }
      }

      int riseBand = 0;
      float maxBandRatio = 0.0f;
      float bandRatios[4];
      for (int k = 0; k < 4; k++) {
        float r = bandEnergies[k] / (baseBandMean[k] + 1e-4f);
        bandRatios[k] = r;
        if (r > maxBandRatio) {
          maxBandRatio = r;
          riseBand = k;
        }
      }

      char hintText[64] = "nominal baseline vibration";
      if (dominantFreq >= 24.0f && dominantFreq <= 38.0f && maxBandRatio > 1.4f) {
        snprintf(hintText, sizeof(hintText), "energy rising at rotation frequency: possible imbalance");
      } else if ((riseBand == 0 || dominantFreq < 15.0f) && maxBandRatio > 1.4f) {
        snprintf(hintText, sizeof(hintText), "low frequency rise: possible loose mounting");
      } else if ((riseBand == 3 || dominantFreq > 40.0f) && maxBandRatio > 1.4f) {
        snprintf(hintText, sizeof(hintText), "high frequency increase: possible bearing wear or turbulence");
      } else if (bandRatios[0] > 1.3f && bandRatios[1] > 1.3f && bandRatios[2] > 1.3f) {
        snprintf(hintText, sizeof(hintText), "broadband increase: possible loose mounting");
      }

      bool hardSafetyViolated = false;
      char hardLimitReason[24] = "none";
      float hardLimitVal = 0.0f;
      float hardLimitThresh = 0.0f;

      if (tempC >= limitMaxTemp) {
        hardSafetyViolated = true;
        strncpy(hardLimitReason, "hard_limit", sizeof(hardLimitReason));
        hardLimitVal = tempC;
        hardLimitThresh = limitMaxTemp;
      } else if (vibRms >= limitMaxVibRms) {
        hardSafetyViolated = true;
        strncpy(hardLimitReason, "hard_limit", sizeof(hardLimitReason));
        hardLimitVal = vibRms;
        hardLimitThresh = limitMaxVibRms;
      }
#if USE_CURRENT
      else if (currentA >= limitMaxCurrent) {
        hardSafetyViolated = true;
        strncpy(hardLimitReason, "hard_limit", sizeof(hardLimitReason));
        hardLimitVal = currentA;
        hardLimitThresh = limitMaxCurrent;
      }
#endif

      bool warnCondition = false;
      bool critCondition = false;
      char evalReason[24] = "none";
      float evalVal = 0.0f;
      float evalThresh = 0.0f;

      bool suppressStatistical = (millis() < 10000) || (millis() - lastRelaySwitchMs < 3000);

      if (hardSafetyViolated) {
        critCondition = true;
        strncpy(evalReason, hardLimitReason, sizeof(evalReason));
        evalVal = hardLimitVal;
        evalThresh = hardLimitThresh;
      } else if (!inLearningMode && baselineReady && !suppressStatistical) {
#if DEMO_MODE
        float demoCritLevel = fminf(0.30f, baseVibMean + 0.25f);
        float demoWarnLevel = fminf(baseVibMean + 0.12f, demoCritLevel - 0.08f);
        if (vibRms >= demoCritLevel) {
          critCondition = true;
          strncpy(evalReason, "crit_vib_demo", sizeof(evalReason));
          evalVal = vibRms;
          evalThresh = demoCritLevel;
        } else if (vibRms >= demoWarnLevel) {
          warnCondition = true;
          strncpy(evalReason, "vib_rise_demo", sizeof(evalReason));
          evalVal = vibRms;
          evalThresh = demoWarnLevel;
        } else {
          for (int k = 0; k < 4; k++) {
            if (baseBandMean[k] >= 0.05f && bandEnergies[k] >= 0.15f) {
              float r = bandEnergies[k] / baseBandMean[k];
              if (r > WARN_BAND_RATIO) {
                warnCondition = true;
                snprintf(evalReason, sizeof(evalReason), "band%d_ratio", k);
                evalVal = r;
                evalThresh = WARN_BAND_RATIO;
                break;
              }
            }
          }
          if (!warnCondition) {
            float tempWarnLimit = baseTempMean + WARN_TEMP_RISE_DEG;
            if (tempC > tempWarnLimit) {
              warnCondition = true;
              strncpy(evalReason, "temp_rise", sizeof(evalReason));
              evalVal = tempC;
              evalThresh = tempWarnLimit;
            }
          }
        }
#else
        float critVibThresh = CRIT_VIB_RATIO * baseVibMean;
        if (vibRms > critVibThresh && vibRms >= CRIT_VIB_ABS_MIN) {
          critCondition = true;
          strncpy(evalReason, "crit_vib_ratio", sizeof(evalReason));
          evalVal = vibRms;
          evalThresh = critVibThresh;
        } else {
          float vibWarnSigmaLimit = baseVibMean + (WARN_VIB_SIGMA_MULT * baseVibStd);
          if ((vibRms > vibWarnSigmaLimit) && ((vibRms - baseVibMean) >= 0.15f)) {
            warnCondition = true;
            strncpy(evalReason, "vib_sigma", sizeof(evalReason));
            evalVal = vibRms;
            evalThresh = vibWarnSigmaLimit;
          } else {
            for (int k = 0; k < 4; k++) {
              if (baseBandMean[k] >= 0.05f && bandEnergies[k] >= 0.15f) {
                float r = bandEnergies[k] / baseBandMean[k];
                if (r > WARN_BAND_RATIO) {
                  warnCondition = true;
                  snprintf(evalReason, sizeof(evalReason), "band%d_ratio", k);
                  evalVal = r;
                  evalThresh = WARN_BAND_RATIO;
                  break;
                }
              }
            }
            if (!warnCondition) {
              float tempWarnLimit = baseTempMean + WARN_TEMP_RISE_DEG;
              if (tempC > tempWarnLimit) {
                warnCondition = true;
                strncpy(evalReason, "temp_rise", sizeof(evalReason));
                evalVal = tempC;
                evalThresh = tempWarnLimit;
              }
            }
          }
        }
#endif
      }

      HealthState previousState = currentState;
      strncpy(stateReason, "none", sizeof(stateReason));
      float reasonVal = 0.0f;
      float reasonLimit = 0.0f;

      if (!sensorsHealthy) {
        currentState = STATE_SENSOR_ERROR;
        strncpy(stateReason, "sensor_error", sizeof(stateReason));
        reasonVal = 0.0f;
        reasonLimit = 0.0f;
      } else if (isLatched) {
        currentState = STATE_CRITICAL;
        strncpy(stateReason, latchedReason, sizeof(stateReason));
        reasonVal = latchedVal;
        reasonLimit = latchedLimit;
      } else if (critCondition) {
        bool shouldLatch = false;
        if (hardSafetyViolated) {
          shouldLatch = true;
        } else {
          consecutiveCritCount++;
#if DEMO_MODE
          if (consecutiveCritCount >= 2 || (consecutiveCritCount >= 1 && vibRms >= 0.60f)) {
            shouldLatch = true;
          }
#else
          if (consecutiveCritCount >= CRIT_WINDOW_COUNT_REQ) {
            shouldLatch = true;
          }
#endif
        }

        if (shouldLatch) {
          isLatched = true;
          currentState = STATE_CRITICAL;
          strncpy(latchedReason, evalReason, sizeof(latchedReason));
          latchedVal = evalVal;
          latchedLimit = evalThresh;
          strncpy(stateReason, latchedReason, sizeof(stateReason));
          reasonVal = latchedVal;
          reasonLimit = latchedLimit;
        } else {
          currentState = previousState;
        }
      } else if (inLearningMode) {
        currentState = STATE_CALIBRATING;
        strncpy(stateReason, "none", sizeof(stateReason));
        reasonVal = (float)learningPct;
        reasonLimit = 100.0f;
      } else if (warnCondition) {
        consecutiveCritCount = 0;
        consecutiveWarnCount++;
        consecutiveNormCount = 0;
        if (consecutiveWarnCount >= WARN_WINDOW_COUNT_REQ) {
          currentState = STATE_WARNING;
          strncpy(stateReason, evalReason, sizeof(stateReason));
          reasonVal = evalVal;
          reasonLimit = evalThresh;
        } else {
          currentState = previousState;
          if (currentState == STATE_WARNING) {
            strncpy(stateReason, evalReason, sizeof(stateReason));
            reasonVal = evalVal;
            reasonLimit = evalThresh;
          }
        }
      } else {
        consecutiveCritCount = 0;
        consecutiveWarnCount = 0;
        consecutiveNormCount++;
        if ((currentState == STATE_CRITICAL || currentState == STATE_SENSOR_ERROR) && !isLatched && consecutiveNormCount >= NORM_RECOVERY_COUNT_REQ) {
          currentState = STATE_NORMAL;
          strncpy(stateReason, "none", sizeof(stateReason));
          reasonVal = 0.0f;
          reasonLimit = 0.0f;
        } else if (consecutiveNormCount >= NORM_RECOVERY_COUNT_REQ && currentState == STATE_WARNING) {
          currentState = STATE_NORMAL;
          strncpy(stateReason, "none", sizeof(stateReason));
          reasonVal = 0.0f;
          reasonLimit = 0.0f;
        } else if (currentState == STATE_NORMAL) {
          strncpy(stateReason, "none", sizeof(stateReason));
          reasonVal = 0.0f;
          reasonLimit = 0.0f;
        } else if (currentState == STATE_WARNING) {
          strncpy(stateReason, evalReason, sizeof(stateReason));
          reasonVal = evalVal;
          reasonLimit = evalThresh;
        }
      }

      if (currentState != previousState) {
        const char* stateNames[] = {"NORMAL", "WARNING", "CRITICAL", "CALIBRATING", "SENSOR_ERROR"};
        Serial.printf("STATE: %s -> %s (%s, val=%.2f, limit=%.2f)\n",
          stateNames[previousState], stateNames[currentState], stateReason, reasonVal, reasonLimit);
      }

      bool relayEnergized = false;
      if (isLatched || currentState == STATE_CRITICAL || currentState == STATE_SENSOR_ERROR) {
        relayEnergized = false;
      } else if (fanMode == FAN_MODE_OFF) {
        relayEnergized = false;
      } else {
        relayEnergized = true;
      }

      static bool relayApplied = false;
      static bool relayNotAppliedYet = true;

      if (relayNotAppliedYet || (relayEnergized != relayApplied)) {
        setRelay(relayEnergized);
        relayApplied = relayEnergized;
        relayNotAppliedYet = false;
        lastRelaySwitchMs = millis();
      }

      switch (currentState) {
        case STATE_NORMAL:
        case STATE_CALIBRATING:
          digitalWrite(PIN_LED_GRN, HIGH);
          digitalWrite(PIN_LED_YEL, LOW);
          digitalWrite(PIN_LED_RED, LOW);
          digitalWrite(PIN_BUZZER, LOW);
          break;
          case STATE_WARNING:
            digitalWrite(PIN_LED_GRN, LOW);
            digitalWrite(PIN_LED_YEL, HIGH);
            digitalWrite(PIN_LED_RED, LOW);
            digitalWrite(PIN_BUZZER, LOW);
            break;
          case STATE_CRITICAL:
            digitalWrite(PIN_LED_GRN, LOW);
            digitalWrite(PIN_LED_YEL, LOW);
            digitalWrite(PIN_LED_RED, HIGH);
            digitalWrite(PIN_BUZZER, buzzerMuted ? LOW : HIGH);
            break;
          case STATE_SENSOR_ERROR:
          default:
            digitalWrite(PIN_LED_GRN, LOW);
            digitalWrite(PIN_LED_YEL, LOW);
            digitalWrite(PIN_LED_RED, HIGH);
            digitalWrite(PIN_BUZZER, LOW);
            break;
        }

      float vibRatio = vibRms / (baseVibMean + 1e-4f);
      float tempRise = max(0.0f, tempC - baseTempMean);
      float rawHealth = 100.0f;
      if (currentState == STATE_CRITICAL || isLatched) {
        rawHealth = 0.0f;
      } else if (inLearningMode) {
        rawHealth = 100.0f;
      } else {
        float pen = 0.0f;
        if (vibRatio > 1.0f) pen += (vibRatio - 1.0f) * 30.0f;
        if (tempRise > 2.0f) pen += (tempRise - 2.0f) * 6.0f;
        if (maxBandRatio > 1.4f) pen += (maxBandRatio - 1.4f) * 15.0f;
        rawHealth = constrain(100.0f - pen, 0.0f, 100.0f);
      }
      smoothedHealthScore = (0.25f * rawHealth) + (0.75f * smoothedHealthScore);

      uint32_t nowSec = (uint32_t)(millis() / 1000);
      if (trendCount < TREND_HISTORY_SIZE) {
        trendHistory[trendCount++] = {nowSec, tempC, vibRms};
      } else {
        memmove(&trendHistory[0], &trendHistory[1], sizeof(TrendPoint) * (TREND_HISTORY_SIZE - 1));
        trendHistory[TREND_HISTORY_SIZE - 1] = {nowSec, tempC, vibRms};
      }
      float ttcTemp = calculateTimeToCritical(trendHistory, trendCount, limitMaxTemp, true);
      float ttcVib = calculateTimeToCritical(trendHistory, trendCount, limitMaxVibRms, false);
      float ttcMin = -1.0f;
      if (ttcTemp > 0 && ttcVib > 0) ttcMin = min(ttcTemp, ttcVib);
      else if (ttcTemp > 0) ttcMin = ttcTemp;
      else if (ttcVib > 0) ttcMin = ttcVib;

      if (fanMode != FAN_MODE_AUTO && (millis() - lastCommandTime > COMMAND_WATCHDOG_TIMEOUT_MS)) {
        fanMode = FAN_MODE_AUTO;
      }

      TelemetrySnapshot snap;
      snap.timestamp_s = nowSec;
      snap.temp_c = tempC;
      snap.vib_rms = vibRms;
      snap.current_a = currentA;
      snap.state = currentState;
      strncpy(snap.state_reason, stateReason, sizeof(snap.state_reason));
      snap.reason_val = reasonVal;
      snap.reason_limit = reasonLimit;
      snap.latched = isLatched;
      snap.baseline_ready = baselineReady;
      snap.learning_pct = learningPct;
      snap.dominant_freq_hz = dominantFreq;
      snap.rise_band = riseBand;
      strncpy(snap.hint, hintText, sizeof(snap.hint));
      snap.health_score = smoothedHealthScore;
      snap.ttc_min = ttcMin;
      snap.fan_on = relayEnergized;
      snap.mode = fanMode;
      snap.free_heap = ESP.getFreeHeap();
      snap.backfilled = false;

      if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        latestTelemetry = snap;
        xSemaphoreGive(telemetryMutex);
      }
    }

    uint32_t nowMs = millis();
    if (firstWindowProcessed && (nowMs - lastStatusPrintMs >= 2000)) {
      lastStatusPrintMs = nowMs;

      const char* stateStr = "NORMAL";
      switch (currentState) {
        case STATE_NORMAL:       stateStr = "NORMAL"; break;
        case STATE_CALIBRATING:  stateStr = "CALIBRATING"; break;
        case STATE_WARNING:      stateStr = "WARNING"; break;
        case STATE_CRITICAL:     stateStr = "CRITICAL"; break;
        case STATE_SENSOR_ERROR: stateStr = "SENSOR_ERROR"; break;
        default:                 stateStr = "SENSOR_ERROR"; break;
      }
      int healthVal = constrain((int)round(smoothedHealthScore), 0, 100);

      if (currentState == STATE_CALIBRATING) {
        Serial.printf("[STATUS] state=%s vib=%.2f m/s2 temp=%.1f C health=%d calib=%d\n",
          stateStr, vibRms, currentTempC, healthVal, learningPct);
      } else {
        Serial.printf("[STATUS] state=%s vib=%.2f m/s2 temp=%.1f C health=%d\n",
          stateStr, vibRms, currentTempC, healthVal);
      }
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  static StaticJsonDocument<256> doc;
  doc.clear();
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;

  lastCommandTime = millis();
  static char ackTopic[96];
  snprintf(ackTopic, sizeof(ackTopic), "%s/%s/ack", topicPrefix, deviceId);

  static StaticJsonDocument<256> ackDoc;
  ackDoc.clear();
  ackDoc["device_id"] = deviceId;
  ackDoc["ts"] = (uint32_t)(millis() / 1000);

  if (doc.containsKey("reset") && doc["reset"] == true) {
    if (latestTelemetry.temp_c < limitMaxTemp && latestTelemetry.vib_rms < limitMaxVibRms) {
      isLatched = false;
      strncpy(latchedReason, "none", sizeof(latchedReason));
      latchedVal = 0.0f;
      latchedLimit = 0.0f;
      ackDoc["latched"] = false;
      ackDoc["message"] = "Latch cleared successfully";
    } else {
      ackDoc["latched"] = true;
      ackDoc["error"] = "Cannot reset while conditions exceed critical limits";
    }
  }

  if (doc.containsKey("calibrate") && doc["calibrate"] == true) {
    triggerLearning = true;
    ackDoc["learning_triggered"] = true;
  }

  if (doc.containsKey("mode")) {
    const char* m = doc["mode"];
    if (isLatched) {
      ackDoc["warning"] = "Cannot change fan mode while device is latched";
    } else {
      if (strcmp(m, "auto") == 0) fanMode = FAN_MODE_AUTO;
      else if (strcmp(m, "on") == 0) fanMode = FAN_MODE_ON;
      else if (strcmp(m, "off") == 0) fanMode = FAN_MODE_OFF;
      ackDoc["mode"] = (fanMode == FAN_MODE_AUTO) ? "auto" : ((fanMode == FAN_MODE_ON) ? "on" : "off");
    }
  }

  if (doc.containsKey("limit_temp")) {
    limitMaxTemp = constrain((float)doc["limit_temp"], 35.0f, 90.0f);
    prefs.begin("fan_settings", false);
    prefs.putFloat("lim_temp", limitMaxTemp);
    prefs.end();
    ackDoc["limit_temp"] = limitMaxTemp;
  }
  if (doc.containsKey("limit_vib")) {
    limitMaxVibRms = constrain((float)doc["limit_vib"], 1.0f, 15.0f);
    prefs.begin("fan_settings", false);
    prefs.putFloat("lim_vib", limitMaxVibRms);
    prefs.end();
    ackDoc["limit_vib"] = limitMaxVibRms;
  }
  if (doc.containsKey("limit_curr")) {
    limitMaxCurrent = constrain((float)doc["limit_curr"], 0.2f, 5.0f);
    prefs.begin("fan_settings", false);
    prefs.putFloat("lim_curr", limitMaxCurrent);
    prefs.end();
    ackDoc["limit_curr"] = limitMaxCurrent;
  }

  if (doc.containsKey("mute_buzzer")) {
    buzzerMuted = doc["mute_buzzer"];
    ackDoc["mute_buzzer"] = buzzerMuted;
  }

  ackDoc["status"] = "OK";
  static char ackBuffer[256];
  serializeJson(ackDoc, ackBuffer);
  if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    mqttClient.publish(ackTopic, ackBuffer);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected() || strlen(mqttHost) == 0) return;
  static unsigned long lastMqttAttempt = 0;
  if (millis() - lastMqttAttempt < 10000) return;
  lastMqttAttempt = millis();

  static char willTopic[96];
  snprintf(willTopic, sizeof(willTopic), "%s/%s/status", topicPrefix, deviceId);

  Serial.printf("MQTT connecting to %s:%d\n", mqttHost, mqttPort);
  vTaskDelay(pdMS_TO_TICKS(10));

  bool ok = false;
  if (strlen(mqttUser) > 0) {
    ok = mqttClient.connect(deviceId, mqttUser, mqttPass, willTopic, 1, true, "offline");
  } else {
    ok = mqttClient.connect(deviceId, willTopic, 1, true, "offline");
  }

  if (ok) {
    Serial.println("MQTT connected");
    mqttClient.publish(willTopic, "online", true);
    vTaskDelay(pdMS_TO_TICKS(10));
    static char cmdTopic[96];
    snprintf(cmdTopic, sizeof(cmdTopic), "%s/%s/command", topicPrefix, deviceId);
    mqttClient.subscribe(cmdTopic);

    static TelemetrySnapshot backfillSnap;
    int backfilledCount = 0;
    while (dequeueTelemetry(backfillSnap) && backfilledCount < 50) {
      if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) break;
      backfillSnap.backfilled = true;
      static char telemTopic[96];
      snprintf(telemTopic, sizeof(telemTopic), "%s/%s/telemetry", topicPrefix, deviceId);
      static StaticJsonDocument<768> backfillDoc;
      backfillDoc.clear();
      backfillDoc["device_id"] = deviceId;
      backfillDoc["ts"] = backfillSnap.timestamp_s;
      backfillDoc["temp_c"] = round(backfillSnap.temp_c * 100.0) / 100.0;
      backfillDoc["vib_rms"] = round(backfillSnap.vib_rms * 1000.0) / 1000.0;
      backfillDoc["state"] = (backfillSnap.state == STATE_NORMAL) ? "NORMAL" :
                             (backfillSnap.state == STATE_WARNING) ? "WARNING" :
                             (backfillSnap.state == STATE_CRITICAL) ? "CRITICAL" :
                             (backfillSnap.state == STATE_CALIBRATING) ? "CALIBRATING" : "SENSOR_ERROR";
      backfillDoc["state_reason"] = backfillSnap.state_reason;
      backfillDoc["reason_val"] = round(backfillSnap.reason_val * 1000.0) / 1000.0;
      backfillDoc["reason_limit"] = round(backfillSnap.reason_limit * 1000.0) / 1000.0;
      backfillDoc["latched"] = backfillSnap.latched;
      backfillDoc["baseline_ready"] = backfillSnap.baseline_ready;
      backfillDoc["learning_pct"] = backfillSnap.learning_pct;
      backfillDoc["dominant_freq_hz"] = round(backfillSnap.dominant_freq_hz * 10.0) / 10.0;
      backfillDoc["rise_band"] = backfillSnap.rise_band;
      backfillDoc["hint"] = backfillSnap.hint;
      backfillDoc["health_score"] = (int)round(backfillSnap.health_score);
      if (backfillSnap.ttc_min > 0) backfillDoc["ttc_min"] = round(backfillSnap.ttc_min * 10.0) / 10.0;
      else backfillDoc["ttc_min"] = (char*)NULL;
      backfillDoc["fan_on"] = backfillSnap.fan_on;
      backfillDoc["mode"] = (backfillSnap.mode == FAN_MODE_AUTO) ? "auto" : ((backfillSnap.mode == FAN_MODE_ON) ? "on" : "off");
      backfillDoc["rssi"] = WiFi.RSSI();
      backfillDoc["uptime_s"] = backfillSnap.timestamp_s;
      backfillDoc["fw_version"] = FIRMWARE_VERSION;
      backfillDoc["backfilled"] = true;
      static char backfillBuffer[768];
      serializeJson(backfillDoc, backfillBuffer);
      mqttClient.publish(telemTopic, backfillBuffer);
      backfilledCount++;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  } else {
    int state = mqttClient.state();
    Serial.printf("MQTT failed, state: %d (%s)\n", state, getMqttStateMeaning(state));
  }
}

void publishTelemetry(const TelemetrySnapshot &snap) {
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;

  static char telemTopic[96];
  snprintf(telemTopic, sizeof(telemTopic), "%s/%s/telemetry", topicPrefix, deviceId);

  static StaticJsonDocument<768> doc;
  doc.clear();
  doc["device_id"] = deviceId;
  doc["ts"] = snap.timestamp_s;
  doc["temp_c"] = round(snap.temp_c * 100.0) / 100.0;
  doc["vib_rms"] = round(snap.vib_rms * 1000.0) / 1000.0;
#if USE_CURRENT
  doc["current_a"] = round(snap.current_a * 1000.0) / 1000.0;
#else
  doc["current_a"] = (char*)NULL;
#endif

  switch (snap.state) {
    case STATE_NORMAL: doc["state"] = "NORMAL"; break;
    case STATE_WARNING: doc["state"] = "WARNING"; break;
    case STATE_CRITICAL: doc["state"] = "CRITICAL"; break;
    case STATE_CALIBRATING: doc["state"] = "CALIBRATING"; break;
    default: doc["state"] = "SENSOR_ERROR"; break;
  }

  doc["state_reason"] = snap.state_reason;
  doc["reason_val"] = round(snap.reason_val * 1000.0) / 1000.0;
  doc["reason_limit"] = round(snap.reason_limit * 1000.0) / 1000.0;
  doc["latched"] = snap.latched;
  doc["baseline_ready"] = snap.baseline_ready;
  doc["learning_pct"] = snap.learning_pct;
  doc["dominant_freq_hz"] = round(snap.dominant_freq_hz * 10.0) / 10.0;
  doc["rise_band"] = snap.rise_band;
  doc["hint"] = snap.hint;
  doc["health_score"] = (int)round(snap.health_score);
  if (snap.ttc_min > 0) doc["ttc_min"] = round(snap.ttc_min * 10.0) / 10.0;
  else doc["ttc_min"] = (char*)NULL;

  doc["fan_on"] = snap.fan_on;
  doc["mode"] = (snap.mode == FAN_MODE_AUTO) ? "auto" : ((snap.mode == FAN_MODE_ON) ? "on" : "off");
  doc["rssi"] = WiFi.RSSI();
  doc["uptime_s"] = (uint32_t)(millis() / 1000);
  doc["fw_version"] = FIRMWARE_VERSION;

  static char buffer[768];
  serializeJson(doc, buffer);
  mqttClient.publish(telemTopic, buffer);
  vTaskDelay(pdMS_TO_TICKS(10));
}

void sendTelegramAlert(HealthState newState, const TelemetrySnapshot &snap) {
#if !USE_TLS
  (void)newState;
  (void)snap;
  return;
#else
  if (WiFi.status() != WL_CONNECTED) return;
  if (strlen(telegramToken) == 0 || strlen(telegramChatId) == 0) return;
  if (millis() - lastTelegramAlertMs < TELEGRAM_COOLDOWN_MS) return;

  static WiFiClientSecure tgClient;
  tgClient.setTimeout(10);
  tgClient.setInsecure();
  if (!tgClient.connect("api.telegram.org", 443)) return;

  String msg = (newState == STATE_CRITICAL) ? "CRITICAL ALERT: Appliance Fault Detected! Power Cut Latched.\n" : "RECOVERED: Appliance Returned to NORMAL.\n";
  msg += "Device: " + String(deviceId) + "\n";
  msg += "Health Score: " + String((int)snap.health_score) + "/100\n";
  msg += "Vibration RMS: " + String(snap.vib_rms, 3) + " m/s2\n";
  msg += "Temperature: " + String(snap.temp_c, 1) + " C\n";
  msg += "Hint: " + String(snap.hint);

  String url = "/bot" + String(telegramToken) + "/sendMessage";
  static StaticJsonDocument<256> doc;
  doc.clear();
  doc["chat_id"] = telegramChatId;
  doc["text"] = msg;
  String body;
  serializeJson(doc, body);

  tgClient.println("POST " + url + " HTTP/1.1");
  tgClient.println("Host: api.telegram.org");
  tgClient.println("Content-Type: application/json");
  tgClient.println("Content-Length: " + String(body.length()));
  tgClient.println("Connection: close");
  tgClient.println();
  tgClient.println(body);

  lastTelegramAlertMs = millis();
  lastAlertedState = newState;
#endif
}

void onWiFiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info) {
  uint8_t r = info.wifi_sta_disconnected.reason;
  if (r == 2 || r == 15 || r == 202) {
    Serial.printf("WiFi disconnect reason: %d (wrong password or auth failed)\n", (int)r);
  } else if (r == 201) {
    Serial.printf("WiFi disconnect reason: %d (network not found)\n", (int)r);
  } else if (r == 205) {
    Serial.printf("WiFi disconnect reason: %d (connection failed)\n", (int)r);
  } else if (r == 8) {
    Serial.printf("WiFi disconnect reason: %d (left network)\n", (int)r);
  } else {
    Serial.printf("WiFi disconnect reason: %d\n", (int)r);
  }
}

void TaskCore0_Networking(void *pvParameters) {
  prefs.begin("fan_settings", false);
  limitMaxTemp = prefs.getFloat("lim_temp", 60.0f);
  limitMaxVibRms = prefs.getFloat("lim_vib", 4.0f);
  limitMaxCurrent = prefs.getFloat("lim_curr", 1.0f);
  prefs.end();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  bool wifiConnected = false;
  bool wifiConnecting = true;
  unsigned long wifiAttemptStart = millis();

  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  ArduinoOTA.setHostname(deviceId);
  ArduinoOTA.setPassword("apex2026");
  ArduinoOTA.begin();

#if USE_TLS
  secureEspClient.setTimeout(10);
  secureEspClient.setInsecure();
  mqttClient.setClient(secureEspClient);
#else
  plainEspClient.setTimeout(10);
  mqttClient.setClient(plainEspClient);
#endif
  mqttClient.setSocketTimeout(10);
  mqttClient.setServer(mqttHost, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(768);

  unsigned long lastTelemetryPublish = 0;

  for (;;) {
    ArduinoOTA.handle();

    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiConnected) {
        wifiConnected = true;
        wifiConnecting = false;
        Serial.printf("Connected to WiFi, IP %s\n", WiFi.localIP().toString().c_str());
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (!mqttClient.connected()) {
        connectMqtt();
      } else {
        mqttClient.loop();
      }
    } else {
      if (wifiConnected) {
        wifiConnected = false;
        wifiConnecting = true;
        Serial.printf("WiFi failed, status %d\n", (int)WiFi.status());
        Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
        WiFi.disconnect();
        delay(100);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiAttemptStart = millis();
      } else if (wifiConnecting) {
        if (millis() - wifiAttemptStart >= 15000) {
          Serial.printf("WiFi failed, status %d\n", (int)WiFi.status());
          Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
          WiFi.disconnect();
          delay(100);
          WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
          wifiAttemptStart = millis();
        }
      } else {
        wifiConnecting = true;
        Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiAttemptStart = millis();
      }
    }

    unsigned long now = millis();

    if (now - lastTelemetryPublish >= 2000) {
      lastTelemetryPublish = now;

      static TelemetrySnapshot currentSnap;
      bool hasSnap = false;
      if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        currentSnap = latestTelemetry;
        hasSnap = true;
        xSemaphoreGive(telemetryMutex);
      }

      if (hasSnap) {
        if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
          publishTelemetry(currentSnap);
        } else {
          enqueueTelemetry(currentSnap);
        }

        if ((currentSnap.state == STATE_CRITICAL && lastAlertedState != STATE_CRITICAL) ||
            (currentSnap.state == STATE_NORMAL && lastAlertedState == STATE_CRITICAL)) {
          sendTelegramAlert(currentSnap.state, currentSnap);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("ESP32 Reset Reason: %d (%s)\n", (int)resetReason, getResetReasonMeaning(resetReason));

  setRelay(true);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YEL, OUTPUT);
  pinMode(PIN_LED_GRN, OUTPUT);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_YEL, LOW);
  digitalWrite(PIN_LED_GRN, LOW);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  ringHead = 0;
  ringTail = 0;
  ringCount = 0;
  telemetryMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    TaskCore1_SamplingAndDecision,
    "SamplingDecisionCore1",
    12288,
    NULL,
    2,
    &taskCore1Handle,
    1
  );

  xTaskCreatePinnedToCore(
    TaskCore0_Networking,
    "NetworkingCore0",
    20480,
    NULL,
    1,
    &taskCore0Handle,
    0
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
