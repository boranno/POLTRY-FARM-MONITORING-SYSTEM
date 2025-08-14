#define BLYNK_TEMPLATE_ID "TMPL6NkYtXFy4"
#define BLYNK_TEMPLATE_NAME "POULTRY FARM MANAGEMENT SYSTEM"
#define BLYNK_AUTH_TOKEN "Ar5-H-c2MrMfcBwxQAe72UBtV_DpQZCd"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// WiFi credentials
char ssid[] = "PFMS";
char pass[] = "22222222";

// Pin assignments
const int DHT_Pin = 47;
const int MQ_Pin = 4;
const int Live_Update_Review = 12;

// LEDs & indicators
const int RED_LED = 38, YELLOW_LED = 37, BLUE_LED = 39;
const int BLYNK_UPLOAD_ONGOING = 10, BLYNK_UPLOAD_STOPPED = 11;

// Actuators
const int Fan = 16, Heater = 20, vantilation = 35, siren = 6;

// Sensor setup
#define DHT_TYPE DHT22
DHT dht(DHT_Pin, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MQ gas sensor constants
#define RL_VALUE 47
#define SLOPE -0.263
#define INTERCEPT 0.42
float RO_CLEAN_AIR_VALUE = 30.0;

// Farm state enum
enum FarmState { OPTIMAL = 1, SUBOPTIMAL = 2, POOR_DANGEROUS = 3 };

// Shared sensor data
volatile float temperature = 0, humidity = 0, ppm = 0;
volatile FarmState currentState = OPTIMAL;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Upload timing
unsigned long lastBlynkUploadTime = 0;
const unsigned long BLYNK_UPLOAD_INTERVAL = 10000;

// Reconnect system
enum ConnPhase { IDLE, WAIT_WIFI, WAIT_BLYNK, CONNECTED };
ConnPhase connPhase = IDLE;
unsigned long phaseStartTime = 0, lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000; // 5 min
const unsigned long WIFI_TIMEOUT = 15000;        // 15 sec
const unsigned long BLYNK_CONN_TIMEOUT = 5000;   // 5 sec
bool wifiConnected = false, blynkConnected = false;
bool prevUploadEnabled = false;

// Function prototypes
FarmState classifyFarmState(float t, float h, float nh3);
String getStateAbbrev(FarmState s);
void startupAnimation();
void connectStep(bool uploadEnabled, unsigned long now);

// Animations
void startupAnimation() {
  String t = "POULTRY FARM MANAGEMENT SYSTEM ";
  String pad = "                ";
  String scrollText = pad + t + pad;
  for (int i = 0; i < scrollText.length() - 15; i++) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(scrollText.substring(i, i + 16));
    delay(250);
  }
  lcd.setCursor(0, 1);
  lcd.print("Starting Up... ");
  delay(2000);
}

// Non-blocking connection handler
void connectStep(bool uploadEnabled, unsigned long now) {
  switch (connPhase) {
    case IDLE:
      if (uploadEnabled) {
        if (!wifiConnected || !blynkConnected) {
          if (lastReconnectAttempt == 0 || now - lastReconnectAttempt >= RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, pass);
            phaseStartTime = now;
            connPhase = WAIT_WIFI;
          }
        } else connPhase = CONNECTED;
      } else {
        wifiConnected = false;
        blynkConnected = false;
      }
      break;

    case WAIT_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Blynk.disconnect();
        Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
        phaseStartTime = now;
        connPhase = WAIT_BLYNK;
      } else if (now - phaseStartTime >= WIFI_TIMEOUT) {
        wifiConnected = false;
        connPhase = IDLE;
      }
      break;

    case WAIT_BLYNK:
      if (Blynk.connected()) {
        blynkConnected = true;
        connPhase = CONNECTED;
      } else if (now - phaseStartTime >= BLYNK_CONN_TIMEOUT) {
        blynkConnected = false;
        connPhase = IDLE;
      } else {
        Blynk.run(); // feed events while waiting
      }
      break;

    case CONNECTED:
      if (!uploadEnabled || WiFi.status() != WL_CONNECTED || !Blynk.connected()) {
        wifiConnected = false;
        blynkConnected = false;
        connPhase = IDLE;
      }
      break;
  }
}

// Sensor reading Core 0 task
void sensorTask(void* pv) {
  for (;;) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (isnan(temp) || isnan(hum)) { temp = 0; hum = 0; }

    int adcValue = analogRead(MQ_Pin);
    float sensorVoltage = (adcValue / 4095.0) * 3.3;
    float Rs = ((5.0 * RL_VALUE) / sensorVoltage) - RL_VALUE;
    float ratio = Rs / RO_CLEAN_AIR_VALUE;
    float newPpm = pow(10, ((log10(ratio) - INTERCEPT) / SLOPE));
    if (newPpm < 0 || isnan(newPpm) || isinf(newPpm)) newPpm = 0;

    FarmState s = classifyFarmState(temp, hum, newPpm);
    portENTER_CRITICAL(&mux);
    temperature = temp; humidity = hum; ppm = newPpm; currentState = s;
    portEXIT_CRITICAL(&mux);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void setup() {
  pinMode(Live_Update_Review, INPUT_PULLUP);
  pinMode(RED_LED, OUTPUT); pinMode(YELLOW_LED, OUTPUT); pinMode(BLUE_LED, OUTPUT);
  pinMode(BLYNK_UPLOAD_ONGOING, OUTPUT); pinMode(BLYNK_UPLOAD_STOPPED, OUTPUT);
  pinMode(Fan, OUTPUT); pinMode(Heater, OUTPUT); pinMode(vantilation, OUTPUT); pinMode(siren, OUTPUT);

  dht.begin();
  lcd.init();
  lcd.backlight();
  analogReadResolution(12);

  startupAnimation();

  if (digitalRead(Live_Update_Review) == HIGH) {
    WiFi.mode(WIFI_STA); WiFi.begin(ssid, pass);
    connPhase = WAIT_WIFI; phaseStartTime = millis(); lastReconnectAttempt = 0;
  }

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 1, NULL, 0);
}

void loop() {
  unsigned long now = millis();
  bool uploadEnabled = (digitalRead(Live_Update_Review) == HIGH);

  // Immediate connection on OFF->ON
  if (!prevUploadEnabled && uploadEnabled) {
    lastReconnectAttempt = 0;
    connPhase = IDLE;
  }
  prevUploadEnabled = uploadEnabled;

  connectStep(uploadEnabled, now);

  static bool wasOffline = true, showingModeMessage = false;
  static unsigned long msgStart = 0;
  bool offlineNow = !wifiConnected || !blynkConnected;

  if (offlineNow != wasOffline) {
    msgStart = now;
    showingModeMessage = true;
    lcd.clear();
    if (offlineNow) {
      lcd.setCursor(0, 0);
      lcd.print("OFFLINE MODE");
      lcd.setCursor(0, 1);
      lcd.print(uploadEnabled ? "WiFi/Blynk Lost" : "Button OFF");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("ONLINE MODE");
      lcd.setCursor(0, 1);
      lcd.print("Uploading...");
    }
    wasOffline = offlineNow;
  }
  if (showingModeMessage && now - msgStart >= 3000) {
    showingModeMessage = false;
    lcd.clear();
  }

  if (!showingModeMessage) {
    if (!offlineNow && uploadEnabled) {
      if (blynkConnected) Blynk.run();
      digitalWrite(BLYNK_UPLOAD_ONGOING, HIGH); digitalWrite(BLYNK_UPLOAD_STOPPED, LOW);
    } else {
      digitalWrite(BLYNK_UPLOAD_ONGOING, LOW); digitalWrite(BLYNK_UPLOAD_STOPPED, HIGH);
    }

    static unsigned long lastDisplay = 0;
    if (now - lastDisplay >= 2000) {
      lastDisplay = now;
      float t, h, p; FarmState s;
      portENTER_CRITICAL(&mux); t = temperature; h = humidity; p = ppm; s = currentState; portEXIT_CRITICAL(&mux);
      char l1[17], l2[17]; String ab = getStateAbbrev(s);
      snprintf(l1, sizeof(l1), "T:%5.1fC H:%2.0f%%", t, h);
      snprintf(l2, sizeof(l2), "NH3:%3.0fPPM S%d:%-2s", p, (int)s, ab.c_str());
      lcd.setCursor(0, 0); lcd.print(l1);
      lcd.setCursor(0, 1); lcd.print(l2);

      // Actuator logic
      switch (s) {
        case OPTIMAL:
          digitalWrite(BLUE_LED, HIGH); digitalWrite(YELLOW_LED, LOW); digitalWrite(RED_LED, LOW);
          digitalWrite(Fan, LOW); digitalWrite(Heater, LOW); digitalWrite(vantilation, LOW); digitalWrite(siren, LOW);
          break;
        case SUBOPTIMAL:
          digitalWrite(BLUE_LED, LOW); digitalWrite(YELLOW_LED, HIGH); digitalWrite(RED_LED, LOW);
          if (t > 30) digitalWrite(Fan, HIGH);
          else if (t < 20 && t >= 15) { digitalWrite(Fan, LOW); digitalWrite(Heater, HIGH); }
          else { digitalWrite(Fan, LOW); digitalWrite(Heater, LOW); }
          if ((h > 70 && h <= 80) || (p > 10 && p < 25)) digitalWrite(vantilation, HIGH);
          else digitalWrite(vantilation, LOW);
          digitalWrite(siren, LOW);
          break;
        case POOR_DANGEROUS:
          digitalWrite(BLUE_LED, LOW); digitalWrite(YELLOW_LED, LOW); digitalWrite(RED_LED, HIGH);
          digitalWrite(Fan, (t > 35) ? HIGH : LOW);
          digitalWrite(vantilation, (t > 35 || h > 80 || p > 25) ? HIGH : LOW);
          digitalWrite(Heater, (t < 15) ? HIGH : LOW);
          digitalWrite(siren, HIGH);
          break;
      }
    }

    if (uploadEnabled && !offlineNow && blynkConnected && now - lastBlynkUploadTime >= BLYNK_UPLOAD_INTERVAL) {
      lastBlynkUploadTime = now;
      float t, h, p; FarmState s;
      portENTER_CRITICAL(&mux); t = temperature; h = humidity; p = ppm; s = currentState; portEXIT_CRITICAL(&mux);
      Blynk.virtualWrite(V0, t);
      Blynk.virtualWrite(V1, h);
      Blynk.virtualWrite(V2, p);
      Blynk.virtualWrite(V3, (int)s);
    }
  }
  delay(10);
}

// Utilities


FarmState classifyFarmState(float t, float h, float nh3) {
  if ((t >= 20 && t <= 30) && (h >= 50 && h <= 70) && (nh3 < 10)) return OPTIMAL;
  else if ((t > 35 || t < 15) || (h > 80 || h < 30) || (nh3 > 25)) return POOR_DANGEROUS;
  else return SUBOPTIMAL;
}
String getStateAbbrev(FarmState s) {
  switch (s) {
    case OPTIMAL: return "OK";
    case SUBOPTIMAL: return "!!";
    case POOR_DANGEROUS: return "XX";
    default: return "??";
  }
}
