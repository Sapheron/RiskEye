#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_VL53L0X.h>
#include "driver/i2s.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─────────────────────────────────────────
//  OLED Display Config
// ─────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1       // Reset pin (not used, share Arduino reset)
#define OLED_ADDRESS  0x3C     // Most common I2C address for SSD1306

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ─────────────────────────────────────────
//  WiFi esp32
// ─────────────────────────────────────────
#define WIFI_SSID     "athikarath5G"
#define WIFI_PASSWORD "8590084515hafsal"

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
#define IR_PIN      14

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
FirebaseData fbdoPeople;
FirebaseAuth auth;
FirebaseConfig config;

// ─────────────────────────────────────────
//  butoon function   
// ─────────────────────────────────────────
bool emergency        = false;
bool lastReading      = HIGH;
bool buttonState      = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ─────────────────────────────────────────
//  IR People Counter
// ─────────────────────────────────────────
int  peopleCount          = 0;
bool irLastState          = HIGH;
bool irState              = HIGH;
unsigned long irDebounceTime      = 0;
const unsigned long irDebounceDelay = 80;

// ─────────────────────────────────────────
//  Timers
// ─────────────────────────────────────────
unsigned long lastTempSend  = 0;
const unsigned long tempInterval  = 2000;

unsigned long lastSoundSend = 0;
const unsigned long soundInterval = 1000;

// ─────────────────────────────────────────
//  OLED Helper — print a centred status msg
// ─────────────────────────────────────────
void oledStatus(const char* line1,
                const char* line2 = "",
                const char* line3 = "",
                const char* line4 = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Line 1 — large (size 2)
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(line1);

  // Lines 2-4 — small (size 1)
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line2);
  display.setCursor(0, 32);
  display.println(line3);
  display.setCursor(0, 44);
  display.println(line4);

  display.display();
}

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

  // ── OLED Init ─────────────────────────
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 OLED not found!");
    // Continue anyway — display is non-critical
  } else {
    display.clearDisplay();
    display.display();
    Serial.println("OLED Ready!");
  }

  dht.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(IR_PIN, INPUT);

  setupI2SMic();

  Serial.println("System Started!");

  if (!lox.begin()) {
    Serial.println("VL53L0X not found!");
    oledStatus("ERROR", "VL53L0X", "not found!");
    while (1);
  }
  Serial.println("VL53L0X Ready!");

  // ── WiFi ──────────────────────────────
  oledStatus("WiFi", "Connecting...");        // ← "Connecting"

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi Connected!");
  oledStatus("WiFi", "Connected!", WiFi.localIP().toString().c_str());
  delay(1000);

  // ── Firebase ──────────────────────────
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email     = USER_EMAIL;
  auth.user.password  = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  oledStatus("Firebase", "Connecting...");   // ← "Firebase Connected" (pending)

  // Wait until Firebase is authenticated
  Serial.print("Waiting for Firebase");
  unsigned long fbWait = millis();
  while (!Firebase.ready() && millis() - fbWait < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  oledStatus("Firebase", "Connected!");      // ← "Firebase Connected"
  delay(1000);

  oledStatus("Firebase", "Waiting for", "auth token..."); // ← "Waiting for Firebase"
  delay(800);

  // Restore emergency state
  if (Firebase.RTDB.getBool(&fbdo, "/sensors/emergencyBtn")) {
    emergency = fbdo.boolData();
    Serial.print("Initial emergency: ");
    Serial.println(emergency);
  }

  // Load persisted people count
  if (Firebase.RTDB.getInt(&fbdoPeople, "/sensors/people")) {
    peopleCount = fbdoPeople.intData();
    Serial.print("People count restored: ");
    Serial.println(peopleCount);
  } else {
    peopleCount = 0;
    Firebase.RTDB.setInt(&fbdoPeople, "/sensors/people", peopleCount);
    Serial.println("People count initialised to 0");
  }

  oledStatus("System", "Ready!", "Data sending...");  // ← "Data Sending"
  delay(1000);
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
        oledStatus("EMERGENCY", "Button", "Pressed!");
        if (Firebase.ready()) {
          Firebase.RTDB.setBool(&fbdo, "/sensors/emergencyBtn", emergency);
        }
      }
    }
  }
  lastReading = reading;

  // ── IR People Counter (debounced) ─────
  bool irReading = digitalRead(IR_PIN);
  if (irReading != irLastState) irDebounceTime = millis();

  if ((millis() - irDebounceTime) > irDebounceDelay) {
    if (irReading != irState) {
      irState = irReading;
      if (irState == LOW) {
        peopleCount++;
        Serial.print("Person detected! Count: ");
        Serial.println(peopleCount);

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
  
  if (Firebase.ready()) {
  if (Firebase.RTDB.getInt(&fbdoPeople, "/sensors/people")) {
    int firebaseCount = fbdoPeople.intData();

    if (firebaseCount != peopleCount) {
      Serial.println("Syncing from Firebase...");
      peopleCount = firebaseCount;
    }
  }
}

  // ── VL53L0X Distance / Motion ─────────
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  float  distanceCM   = 0;
  String motionStatus = "---";

  if (measure.RangeStatus != 4) {
    distanceCM   = measure.RangeMilliMeter / 10.0;
    motionStatus = (distanceCM > 30.0) ? "HIGH" : "LOW";

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
  static float lastTemp = 0;
  if (millis() - lastTempSend > tempInterval) {
    lastTempSend = millis();

    float temp = dht.readTemperature();
    if (!isnan(temp)) {
      lastTemp = temp;
      Serial.print("Temperature: "); Serial.println(temp);
      if (Firebase.ready()) {
        Firebase.RTDB.setFloat(&fbdo, "/sensors/machineTemp", temp);
      }
    } else {
      Serial.println("DHT11 read failed!");
    }
  }

  // ── I2S MEMS Microphone (RMS) ─────────
  static String lastSoundStatus = "LOW";
  if (millis() - lastSoundSend > soundInterval) {
    lastSoundSend = millis();

    float  rms         = readSoundRMS();
    bool   isAbnormal  = (rms > RMS_THRESHOLD);
    lastSoundStatus    = isAbnormal ? "HIGH" : "LOW";

    digitalWrite(BUZZER_PIN, isAbnormal ? HIGH : LOW);

    Serial.print("Sound RMS: "); Serial.print(rms);
    Serial.print(" | Status: "); Serial.println(lastSoundStatus);

    if (Firebase.ready()) {
      Firebase.RTDB.setFloat(&fbdo,  "/sensors/soundLevel",  rms);
      Firebase.RTDB.setString(&fbdo, "/sensors/soundStatus", lastSoundStatus);
    }

    if (isAbnormal) {
      emergency = true;
      if (Firebase.ready()) {
        Firebase.RTDB.setBool(&fbdo, "/sensors/emergencyBtn", emergency);
      }
    }

    // ── OLED Live Data Display ──────────
    display.clearDisplay();

    // Header
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("  Safety System v1 ");
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // Temp
    display.setCursor(0, 13);
    display.print("Temp : ");
    display.print(lastTemp, 1);
    display.println(" C");

    // Distance
    display.setCursor(0, 24);
    display.print("Dist : ");
    if (measure.RangeStatus != 4) {
      display.print(distanceCM, 1);
      display.println(" cm");
    } else {
      display.println("Out of range");
    }

    // Motion
    display.setCursor(0, 35);
    display.print("Motion: ");
    display.println(motionStatus);

    // Sound
    display.setCursor(0, 46);
    display.print("Sound : ");
    display.println(lastSoundStatus);

    // People + Emergency
    display.setCursor(0, 57);
    display.print("People:");
    display.print(peopleCount);
    display.print("  ");
    if (emergency) {
      display.print("! EMERGENCY !");
    } else {
      display.print("  OK");
    }

    display.display();
  }

  delay(200);
}