#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_ADS1X15.h>

#define USE_CURRENT 0

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

constexpr uint32_t SAMPLE_INTERVAL_US = 10000;
constexpr uint16_t WINDOW_SIZE = 128;

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature tempSensor(&oneWire);

#if USE_CURRENT
Adafruit_ADS1115 ads;
bool adsDetected = false;

constexpr float DIVIDER_RATIO_INV = 32.0f / 10.0f;
constexpr float ACS712_SENSITIVITY = 0.185f;
float currentZeroVolts = 2.50f;
#endif

volatile int currentLabel = 0;
volatile bool isPaused = false;
uint32_t windowIndex = 0;
uint16_t sampleInWindow = 0;
char sessionId[32] = "sess_001";
unsigned long lastSampleMicros = 0;

float latestTempC = 25.0f;
float latestCurrentA = 0.0f;
bool mpuInitialized = false;

bool writeMpuRegister(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(data);
  return (Wire.endTransmission() == 0);
}

bool initMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  if (Wire.endTransmission() != 0) return false;

  if (!writeMpuRegister(MPU6050_REG_PWR_MGMT_1, 0x00)) return false;
  delay(10);

  if (!writeMpuRegister(MPU6050_REG_CONFIG, 0x04)) return false;

  if (!writeMpuRegister(MPU6050_REG_ACCEL_CONFIG, 0x10)) return false;

  return true;
}

bool readAccel(float &ax, float &ay, float &az) {
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

void updateLeds() {
  if (isPaused) {
    digitalWrite(PIN_LED_GRN, LOW);
    digitalWrite(PIN_LED_YEL, LOW);
    digitalWrite(PIN_LED_RED, LOW);
    return;
  }

  digitalWrite(PIN_LED_GRN, (currentLabel == 0) ? HIGH : LOW);
  digitalWrite(PIN_LED_YEL, (currentLabel == 1) ? HIGH : LOW);
  digitalWrite(PIN_LED_RED, (currentLabel == 2) ? HIGH : LOW);
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '0') {
      currentLabel = 0;
      updateLeds();
      Serial.println(F("# Label set to 0 (NORMAL)"));
    } else if (c == '1') {
      currentLabel = 1;
      updateLeds();
      Serial.println(F("# Label set to 1 (WARNING)"));
    } else if (c == '2') {
      currentLabel = 2;
      updateLeds();
      Serial.println(F("# Label set to 2 (CRITICAL)"));
    } else if (c == 's' || c == 'S') {
      isPaused = !isPaused;
      updateLeds();
      if (isPaused) {
        Serial.println(F("# Logging PAUSED"));
      } else {
        Serial.println(F("# Logging RESUMED"));
        lastSampleMicros = micros();
      }
    } else if (c == 'i' || c == 'I') {

      String sid = Serial.readStringUntil('\n');
      sid.trim();
      if (sid.length() > 0) {
        sid.toCharArray(sessionId, sizeof(sessionId));
        windowIndex = 0;
        Serial.print(F("# Session ID updated to: "));
        Serial.println(sessionId);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YEL, OUTPUT);
  pinMode(PIN_LED_GRN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  updateLeds();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  mpuInitialized = initMPU6050();
  if (!mpuInitialized) {
    Serial.println(F("# ERROR: MPU6050 not detected at 0x68!"));
  } else {
    Serial.println(F("# MPU6050 initialized successfully (+/-8g, DLPF ~21Hz)"));
  }

  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  tempSensor.requestTemperatures();

#if USE_CURRENT
  adsDetected = ads.begin(0x48);
  if (adsDetected) {
    ads.setGain(GAIN_ONE);
    Serial.println(F("# ADS1115 detected at 0x48 for ACS712"));
  } else {
    Serial.println(F("# WARNING: ADS1115 not detected at 0x48"));
  }
#endif

  Serial.println(F("# TinyML Data Logger Ready"));
  Serial.println(F("# Commands: '0'=Normal, '1'=Warning, '2'=Critical, 's'=Pause/Resume, 'i:<name>'=Set Session"));

  Serial.println(F("session_id,timestamp_ms,window_id,sample_idx,ax,ay,az,temp_c,current_a,label"));

  lastSampleMicros = micros();
}

void loop() {
  handleSerialCommands();

  if (isPaused) {
    delay(10);
    return;
  }

  uint32_t nowMicros = micros();
  if ((nowMicros - lastSampleMicros) < SAMPLE_INTERVAL_US) {
    return;
  }
  lastSampleMicros += SAMPLE_INTERVAL_US;
  if ((nowMicros - lastSampleMicros) > (SAMPLE_INTERVAL_US * 2)) {

    lastSampleMicros = nowMicros;
  }

  if (sampleInWindow == 0) {
    float t = tempSensor.getTempCByIndex(0);
    if (t > -50.0f && t < 125.0f) {
      latestTempC = t;
    }

    tempSensor.requestTemperatures();

#if USE_CURRENT
    if (adsDetected) {
      int16_t rawAdc = ads.readADC_SingleEnded(0);
      float vDivided = ads.computeVolts(rawAdc);
      float vSensor = vDivided * DIVIDER_RATIO_INV;
      latestCurrentA = (vSensor - currentZeroVolts) / ACS712_SENSITIVITY;
      if (latestCurrentA < 0.0f) latestCurrentA = 0.0f;
    }
#else
    latestCurrentA = 0.0f;
#endif
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  if (mpuInitialized) {
    readAccel(ax, ay, az);
  }

  Serial.print(sessionId);
  Serial.print(',');
  Serial.print(millis());
  Serial.print(',');
  Serial.print(windowIndex);
  Serial.print(',');
  Serial.print(sampleInWindow);
  Serial.print(',');
  Serial.print(ax, 4);
  Serial.print(',');
  Serial.print(ay, 4);
  Serial.print(',');
  Serial.print(az, 4);
  Serial.print(',');
  Serial.print(latestTempC, 2);
  Serial.print(',');
#if USE_CURRENT
  Serial.print(latestCurrentA, 3);
#else
  Serial.print(F("null"));
#endif
  Serial.print(',');
  Serial.println(currentLabel);

  sampleInWindow++;
  if (sampleInWindow >= WINDOW_SIZE) {
    sampleInWindow = 0;
    windowIndex++;
  }
}
