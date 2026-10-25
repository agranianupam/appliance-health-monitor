/*
  APEX Appliance Health Monitor — ESP32 upload starter
  Libraries to install in Arduino IDE:
    - Adafruit MPU6050
    - Adafruit Unified Sensor
    - Adafruit ADS1X15
    - OneWire
    - DallasTemperature

  IMPORTANT:
  - This sketch is a sensor/network integration starter, not a validated TinyML model.
  - It sends health_state="Unknown" and confidence=null until a trained model is integrated.
  - Current measurement needs calibration for your specific ACS712 board and load.
  - Confirm the ESP32 ADC/input voltage limits and wiring before powering the circuit.
  - Relay safety logic is intentionally NOT implemented here; add and test it locally only
    after confirming your relay module's active level and fail-safe behaviour.
*/
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
// Use the laptop's IPv4 address from ipconfig, not localhost.
const char* API_URL = "http://192.168.1.25:5000/api/readings";
const char* DEVICE_ID = "ESP32-FAN-01";

// Change these pins to match your actual wiring.
constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;
constexpr int DS18B20_PIN = 4;
constexpr int ACS712_ADC_CHANNEL = 0; // ADS1115 A0
constexpr unsigned long UPLOAD_INTERVAL_MS = 2000;
constexpr unsigned long CURRENT_WINDOW_MS = 250;

Adafruit_MPU6050 mpu;
Adafruit_ADS1115 ads;
OneWire oneWire(DS18B20_PIN);
DallasTemperature tempSensor(&oneWire);
bool mpuReady = false;
bool adsReady = false;
unsigned long lastUpload = 0;

// Set this only after calibrating the ACS712 + ADS1115 with a known load.
// This placeholder deliberately prevents fake current values from being presented as calibrated.
constexpr float ADS_VOLTS_PER_AMP = 0.100f; // EXAMPLE ONLY: replace with your calibrated value.
float currentZeroVolts = 1.65f;            // EXAMPLE ONLY: measure your sensor's zero-current output.

bool readTemperature(float &temperatureC) {
  tempSensor.requestTemperatures();
  float value = tempSensor.getTempCByIndex(0);
  if (value == DEVICE_DISCONNECTED_C || !isfinite(value) || value < -55 || value > 125) return false;
  temperatureC = value;
  return true;
}

bool readVibrationRms(float &rmsG) {
  if (!mpuReady) return false;
  const int samples = 100;
  double sumSquares = 0.0;
  for (int i = 0; i < samples; i++) {
    sensors_event_t accel, gyro, temp;
    if (!mpu.getEvent(&accel, &gyro, &temp)) return false;
    // Adafruit library reports m/s^2. Convert magnitude to g and remove 1 g gravity approximately.
    float magnitudeG = sqrtf(accel.acceleration.x * accel.acceleration.x +
                             accel.acceleration.y * accel.acceleration.y +
                             accel.acceleration.z * accel.acceleration.z) / 9.80665f;
    float dynamicG = magnitudeG - 1.0f;
    sumSquares += dynamicG * dynamicG;
    delay(5);
  }
  rmsG = sqrtf(sumSquares / samples);
  return isfinite(rmsG);
}

bool readCurrentRms(float &currentA) {
  if (!adsReady || ADS_VOLTS_PER_AMP <= 0) return false;
  // This sample window is a simple prototype estimate. Calibrate for your sensor/load;
  // for AC waveform RMS, verify ADS1115 sampling rate is sufficient for the signal.
  unsigned long started = millis();
  double sumSquares = 0.0;
  unsigned long count = 0;
  while (millis() - started < CURRENT_WINDOW_MS) {
    int16_t raw = ads.readADC_SingleEnded(ACS712_ADC_CHANNEL);
    float volts = ads.computeVolts(raw);
    float amps = (volts - currentZeroVolts) / ADS_VOLTS_PER_AMP;
    sumSquares += amps * amps;
    count++;
    delay(1);
  }
  if (count == 0) return false;
  currentA = sqrtf(sumSquares / count);
  return isfinite(currentA) && currentA >= 0;
}

void sendReading(float temperatureC, float currentA, float vibrationG) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected; reading not uploaded.");
    return;
  }
  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2500);

  // Unknown is honest until the trained TinyML classifier is integrated.
  String payload = "{";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"temperature_c\":" + String(temperatureC, 2) + ",";
  payload += "\"current_rms_a\":" + String(currentA, 3) + ",";
  payload += "\"vibration_rms_g\":" + String(vibrationG, 3) + ",";
  payload += "\"health_state\":\"Unknown\",";
  payload += "\"confidence\":null,";
  payload += "\"relay_state\":\"UNKNOWN\"";
  payload += "}";

  int code = http.POST(payload);
  Serial.printf("Upload HTTP status: %d\n", code);
  if (code > 0) Serial.println(http.getString());
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  tempSensor.begin();
  mpuReady = mpu.begin();
  if (mpuReady) {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else Serial.println("WARNING: MPU6050 not detected.");

  adsReady = ads.begin();
  if (adsReady) ads.setGain(GAIN_ONE);
  else Serial.println("WARNING: ADS1115 not detected.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500); Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ESP32 IP: "); Serial.println(WiFi.localIP());
  } else Serial.println("Wi-Fi not connected. It will be retried in loop().");
  Serial.println("Sensor monitor ready. Confirm calibration before relying on readings.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  if (millis() - lastUpload < UPLOAD_INTERVAL_MS) { delay(20); return; }
  lastUpload = millis();

  float temperatureC, currentA, vibrationG;
  bool tempOk = readTemperature(temperatureC);
  bool currentOk = readCurrentRms(currentA);
  bool vibrationOk = readVibrationRms(vibrationG);

  if (!tempOk || !currentOk || !vibrationOk) {
    Serial.printf("Sensor fault: temperature=%d current=%d vibration=%d. Packet skipped.\n",
                  tempOk, currentOk, vibrationOk);
    return;
  }
  Serial.printf("Temp %.2f C | Current RMS %.3f A | Vibration RMS %.3f g\n",
                temperatureC, currentA, vibrationG);
  sendReading(temperatureC, currentA, vibrationG);
}
