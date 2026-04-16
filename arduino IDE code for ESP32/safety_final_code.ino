#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_VL53L0X.h>
#include "driver/i2s.h"

// ─────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────
#define WIFI_SSID     "TechnoTaLim"
#define WIFI_PASSWORD "TechnoTaLim@2026"

// ─────────────────────────────────────────
//  Firebase
// ─────────────────────────────────────────
#define API_KEY       "AIzaSyB3H6ShscIbFVr9uAqCX4QsEXI8ANsXjLg"
#define DATABASE_URL  "https://safety-system-5a647-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL    "test@iot.com"
#define USER_PASSWORD "12345678"

// ─────────────────────────────────────────
//  Pin Definitions
// ─────────────────────────────────────────
#define DHTPIN      15
#define DHTTYPE     DHT11
#define BUTTON_PIN  4
#define BUZZER_PIN  27
#define IR_PIN      14    // IR sensor OUT pin → GPIO 14 (change if needed)

// I2S MEMS Mic
#define I2S_WS   25
#define I2S_BCLK 26
#define I2S_SD   33

// ─────────────────────────────────────────
//  Sound Config
// ─────────────────────────────────────────
#define I2S_BUFFER_SIZE 1024
int16_t i2sBuffer[I2S_BUFFER_SIZE];
const float RMS_THRESHOLD = 25.0;

// ─────────────────────────────────────────
//  Objects
// ─────────────────────────────────────────
DHT dht(DHTPIN, DHTTYPE);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

FirebaseData fbdo;
FirebaseData fbdoPeople;   // separate FirebaseData for people to avoid collision
FirebaseAuth auth;
FirebaseConfig config;

// ─────────────────────────────────────────
//  Button Debounce
// ─────────────────────────────────────────
bool emergency        = false;
bool lastReading      = HIGH;
bool buttonState      = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ─────────────────────────────────────────
//  IR People Counter
// ─────────────────────────────────────────
int  peopleCount          = 0;     // loaded from Firebase on boot
bool irLastState          = HIGH;  // IR OUT is HIGH when no object (active LOW)
bool irState              = HIGH;
unsigned long irDebounceTime      = 0;
const unsigned long irDebounceDelay = 80;  // ms — filters glitches

// ─────────────────────────────────────────
//  Timers
// ─────────────────────────────────────────
unsigned long lastTempSend  = 0;
const unsigned long tempInterval  = 2000;

unsigned long lastSoundSend = 0;
const unsigned long soundInterval = 1000;

// ─────────────────────────────────────────
//  I2S Setup
// ─────────────────────────────────────────
void setupI2SMic() {
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = 44100,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags     = 0,
    .dma_buf_count        = 4,
    .dma_buf_len          = I2S_BUFFER_SIZE,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ─────────────────────────────────────────
//  Read RMS Sound Level
// ─────────────────────────────────────────
float readSoundRMS() {
  size_t bytesRead = 0;

  esp_err_t result = i2s_read(
    I2S_NUM_0,
    (void*)i2sBuffer,
    I2S_BUFFER_SIZE * sizeof(int16_t),
    &bytesRead,
    portMAX_DELAY
  );

  if (result != ESP_OK || bytesRead == 0) return 0.0;

  int   samples = bytesRead / sizeof(int16_t);
  float sum     = 0;
  for (int i = 0; i < samples; i++) {
    float s = (float)i2sBuffer[i];
    sum += s * s;
  }
  return sqrt(sum / samples);
}

// ─────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  dht.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // IR sensor — most modules: OUT = LOW when object detected, HIGH when clear
  pinMode(IR_PIN, INPUT);

  Wire.begin();
  setupI2SMic();

  Serial.println("System Started!");

  if (!lox.begin()) {
    Serial.println("VL53L0X not found!");
    while (1);
  }
  Serial.println("VL53L0X Ready!");

  // ── WiFi ──────────────────────────────
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi Connected!");

  // ── Firebase ──────────────────────────
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email     = USER_EMAIL;
  auth.user.password  = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Wait until Firebase is authenticated
  Serial.print("Waiting for Firebase");
  unsigned long fbWait = millis();
  while (!Firebase.ready() && millis() - fbWait < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  // Restore emergency state
  if (Firebase.RTDB.getBool(&fbdo, "/sensors/emergencyBtn")) {
    emergency = fbdo.boolData();
    Serial.print("Initial emergency: ");
    Serial.println(emergency);
  }

  // ── Load persisted people count from Firebase ──
  // Count is NEVER reset — it keeps growing even after reboots
  if (Firebase.RTDB.getInt(&fbdoPeople, "/sensors/people")) {
    peopleCount = fbdoPeople.intData();
    Serial.print("People count restored: ");
    Serial.println(peopleCount);
  } else {
    // First boot — key doesn't exist yet, initialise to 0
    peopleCount = 0;
    Firebase.RTDB.setInt(&fbdoPeople, "/sensors/people", peopleCount);
    Serial.println("People count initialised to 0");
  }
}

// ─────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────
void loop() {

  // ── Emergency Button (debounced) ──────
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastReading) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        emergency = true;
        Serial.println("Button pressed -> Emergency TRUE");
        if (Firebase.ready()) {
          Firebase.RTDB.setBool(&fbdo, "/sensors/emergencyBtn", emergency);
        }
      }
    }
  }
  lastReading = reading;

  // ── IR People Counter (debounced) ─────
  // Detects falling edge: HIGH → LOW means someone crossed the beam
  bool irReading = digitalRead(IR_PIN);

  if (irReading != irLastState) {
    irDebounceTime = millis();
  }

  if ((millis() - irDebounceTime) > irDebounceDelay) {
    if (irReading != irState) {
      irState = irReading;

      if (irState == LOW) {          // Falling edge = person detected
        peopleCount++;
        Serial.print("Person detected! Count: ");
        Serial.println(peopleCount);

        // Short beep on every person detection
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);

        if (Firebase.ready()) {
          if (Firebase.RTDB.setInt(&fbdoPeople, "/sensors/people", peopleCount)) {
            Serial.println("Firebase people updated");
          } else {
            Serial.print("Firebase error: ");
            Serial.println(fbdoPeople.errorReason());
          }
        }
      }
    }
  }
  irLastState = irReading;

  // ── VL53L0X Distance / Motion ─────────
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    float  distanceCM   = measure.RangeMilliMeter / 10.0;
    String motionStatus = (distanceCM > 30.0) ? "HIGH" : "LOW";

    Serial.print("Distance: "); Serial.print(distanceCM); Serial.println(" cm");
    Serial.print("Motion: ");   Serial.println(motionStatus);

    if (Firebase.ready()) {
      Firebase.RTDB.setFloat(&fbdo,  "/sensors/distance", distanceCM);
      Firebase.RTDB.setString(&fbdo, "/sensors/motion",   motionStatus);
    }
  } else {
    Serial.println("Distance: Out of range");
  }

  // ── DHT11 Temperature ─────────────────
  if (millis() - lastTempSend > tempInterval) {
    lastTempSend = millis();

    float temp = dht.readTemperature();
    if (!isnan(temp)) {
      Serial.print("Temperature: "); Serial.println(temp);
      if (Firebase.ready()) {
        Firebase.RTDB.setFloat(&fbdo, "/sensors/machineTemp", temp);
      }
    } else {
      Serial.println("DHT11 read failed!");
    }
  }

  // ── I2S MEMS Microphone (RMS) ─────────
  if (millis() - lastSoundSend > soundInterval) {
    lastSoundSend = millis();

    float  rms         = readSoundRMS();
    bool   isAbnormal  = (rms > RMS_THRESHOLD);
    String soundStatus = isAbnormal ? "HIGH" : "LOW";

    digitalWrite(BUZZER_PIN, isAbnormal ? HIGH : LOW);

    Serial.print("Sound RMS: "); Serial.print(rms);
    Serial.print(" | Status: "); Serial.println(soundStatus);

    if (Firebase.ready()) {
      Firebase.RTDB.setFloat(&fbdo,  "/sensors/soundLevel",  rms);
      Firebase.RTDB.setString(&fbdo, "/sensors/soundStatus", soundStatus);
    }

    if (isAbnormal) {
      emergency = true;
      if (Firebase.ready()) {
        Firebase.RTDB.setBool(&fbdo, "/sensors/emergencyBtn", emergency);
      }
    }
  }

  delay(200);
}
