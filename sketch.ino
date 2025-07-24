#define BLYNK_TEMPLATE_ID "TMPL6NkYtXFy4"
#define BLYNK_TEMPLATE_NAME "POULTRY FARM MANAGEMENT SYSTEM"
#define BLYNK_AUTH_TOKEN "Ar5-H-c2MrMfcBwxQAe72UBtV_DpQZCd"

// Include required libraries
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>  // for FreeRTOS functions

// WiFi credentials -- CHANGE TO YOUR NETWORK
char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// Pin definitions
const int DHT_Pin = 26;
const int MQ_Pin = 33;
const int Calibration_Switch = 18; // Make sure this matches your wiring

// LEDs
const int RED_LED = 4;
const int YELLOW_LED = 16;
const int BLUE_LED = 17;
const int BLYNK_UPLOAD_ONGOING = 15;
const int BLYNK_UPLOAD_STOPPED = 2;

#define DHT_TYPE DHT22
DHT dht(DHT_Pin, DHT_TYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

// MQ-137 sensor constants
#define RL_VALUE 47
#define SLOPE -0.263
#define INTERCEPT 0.42

#define CALIBRATION_SAMPLE_TIMES 50
#define CALIBRATION_SAMPLE_INTERVAL 500 // ms

const unsigned long CALIBRATION_HOLD_TIME = 5000;  // ms

// Default calibration value for MQ sensor
float RO_CLEAN_AIR_VALUE = 30.0;

// Farm State enum
enum FarmState {
  OPTIMAL = 1,
  SUBOPTIMAL = 2,
  POOR_DANGEROUS = 3
};

// Shared volatile sensor data variables
volatile float temperature = 0.0;
volatile float humidity = 0.0;
volatile float ppm = 0.0;
volatile FarmState currentState = OPTIMAL;

// Mutex for critical sections
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Calibration button variables
unsigned long buttonPressStartTime = 0;
bool calibrationTriggered = false;
bool showingHoldMessage = false;

// Timing for Blynk data upload
unsigned long lastBlynkUploadTime = 0;
const unsigned long BLYNK_UPLOAD_INTERVAL = 10000;  // 10 seconds

// Forward declarations
FarmState classifyFarmState(float temperature, float humidity, float ammonia_ppm);
String getStateAbbrev(FarmState state);
void calibrate();
void connectWiFiWithAnimation();
void startupAnimation();

// Scrolling startup animation with project name
void startupAnimation() {
  String projectName = "POULTRY FARM MONITOR";
  String padding = "                "; // 16 spaces for LCD width
  String scrollText = padding + projectName + padding;

  int len = scrollText.length();

  for (int i = 0; i < len - 15; i++) { // 16 characters visible at once
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(scrollText.substring(i, i + 16));
    delay(250);
  }

  // Optional steady message on second line
  lcd.setCursor(0, 1);
  lcd.print("Starting Up... ");
  delay(2000);
}

// WiFi connection animation and message
void connectWiFiWithAnimation() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to");
  lcd.setCursor(0, 1);
  lcd.print(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  const unsigned long connectTimeout = 15000;  // 15 seconds
  unsigned long startTime = millis();

  const char animChars[] = {'|', '/', '-', '\\'};
  int animIndex = 0;

  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < connectTimeout) {
    if ((millis() - startTime) % 300 < 50) {
      lcd.setCursor(15, 1);
      lcd.print(animChars[animIndex]);
      animIndex = (animIndex + 1) % 4;
    }
    delay(50);
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    Serial.print("WiFi Connected. IP: ");
    Serial.println(WiFi.localIP());
    delay(2500);
  } else {
    lcd.setCursor(0,0);
    lcd.print("WiFi Failed!");
    Serial.println("WiFi connection failed.");
    delay(2500);
  }
}

// Sensor reading task executed on core 0
void sensorTask(void* parameter) {
  (void) parameter;

  for (;;) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
      temp = 0;
      hum = 0;
    }

    int adcValue = analogRead(MQ_Pin);
    float sensorVoltage = (adcValue / 4095.0) * 3.3;

    float Rs = ((5.0 * RL_VALUE) / sensorVoltage) - RL_VALUE;
    float ratio = Rs / RO_CLEAN_AIR_VALUE;
    float newPpm = pow(10, ((log10(ratio) - INTERCEPT) / SLOPE));

    if (newPpm < 0 || isnan(newPpm) || isinf(newPpm)) newPpm = 0;

    FarmState stateNew = classifyFarmState(temp, hum, newPpm);

    portENTER_CRITICAL(&mux);
    temperature = temp;
    humidity = hum;
    ppm = newPpm;
    currentState = stateNew;
    portEXIT_CRITICAL(&mux);

    vTaskDelay(pdMS_TO_TICKS(2000));  // Update every 2 seconds
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize all pins and LEDs
  pinMode(Calibration_Switch, INPUT_PULLUP);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BLYNK_UPLOAD_ONGOING, OUTPUT);
  pinMode(BLYNK_UPLOAD_STOPPED, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(BLYNK_UPLOAD_ONGOING, LOW);
  digitalWrite(BLYNK_UPLOAD_STOPPED, LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();
  analogReadResolution(12);

  startupAnimation();
  connectWiFiWithAnimation();

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("No WiFi Conn");
    lcd.setCursor(0, 1);
    lcd.print("Local Mode Only");
    Serial.println("No WiFi connection. Skipping Blynk startup.");
  }

  xTaskCreatePinnedToCore(
    sensorTask,
    "SensorTask",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Poultry Farm");
  lcd.setCursor(0, 1);
  lcd.print("Monitor System");
  delay(2000);

  Serial.println("=== POULTRY FARM ENVIRONMENTAL MONITOR ===");
  Serial.println("Hold calibration button for 5 seconds to calibrate");
  Serial.println("==========================================");
}

void loop() {
  Blynk.run();

  if (Blynk.connected() && WiFi.status() == WL_CONNECTED) {
    digitalWrite(BLYNK_UPLOAD_ONGOING, HIGH);
    digitalWrite(BLYNK_UPLOAD_STOPPED, LOW);
  } else {
    digitalWrite(BLYNK_UPLOAD_ONGOING, LOW);
    digitalWrite(BLYNK_UPLOAD_STOPPED, HIGH);
  }

  int buttonState = digitalRead(Calibration_Switch);

  if (buttonState == LOW && !calibrationTriggered) {
    if (buttonPressStartTime == 0) {
      buttonPressStartTime = millis();
      showingHoldMessage = false;
      Serial.println("Calibration button pressed - hold for 5 seconds...");
    }
    unsigned long holdDuration = millis() - buttonPressStartTime;

    if (holdDuration > 1000 && !showingHoldMessage) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Hold for calib.");
      lcd.setCursor(0, 1);
      lcd.print("Time: ");
      lcd.print((CALIBRATION_HOLD_TIME - holdDuration) / 1000);
      lcd.print(" sec");
      showingHoldMessage = true;
    }

    if (showingHoldMessage && holdDuration < CALIBRATION_HOLD_TIME) {
      lcd.setCursor(6, 1);
      lcd.print((CALIBRATION_HOLD_TIME - holdDuration) / 1000);
      lcd.print(" sec  ");
    }

    if (holdDuration >= CALIBRATION_HOLD_TIME) {
      Serial.println("5 seconds reached - starting calibration!");
      calibrate();
      calibrationTriggered = true;
      showingHoldMessage = false;
    }
  } else if (buttonState == HIGH) {
    if (buttonPressStartTime != 0 && !calibrationTriggered) {
      Serial.println("Button released before 5 seconds - calibration cancelled");
    }
    buttonPressStartTime = 0;
    calibrationTriggered = false;
    showingHoldMessage = false;
  }

  static unsigned long lastDisplayUpdate = 0;
  unsigned long now = millis();

  if (!showingHoldMessage && (now - lastDisplayUpdate >= 2000)) {
    lastDisplayUpdate = now;

    float tempLocal, humLocal, ppmLocal;
    FarmState stateLocal;

    portENTER_CRITICAL(&mux);
    tempLocal = temperature;
    humLocal = humidity;
    ppmLocal = ppm;
    stateLocal = currentState;
    portEXIT_CRITICAL(&mux);

    String abbrev = getStateAbbrev(stateLocal);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:");
    if (tempLocal < 10) lcd.print(" ");
    lcd.print(tempLocal, 1);
    lcd.print("C    H:");
    if (humLocal < 10) lcd.print(" ");
    lcd.print(humLocal, 0);
    lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print("NH3:");
    if (ppmLocal < 10) lcd.print(" ");
    lcd.print(ppmLocal, 0);
    lcd.print("PPM  S");
    lcd.print((int)stateLocal);
    lcd.print(":");
    lcd.print(abbrev);

    switch (stateLocal) {
      case OPTIMAL:
        digitalWrite(BLUE_LED, HIGH);
        digitalWrite(YELLOW_LED, LOW);
        digitalWrite(RED_LED, LOW);
        break;
      case SUBOPTIMAL:
        digitalWrite(BLUE_LED, LOW);
        digitalWrite(YELLOW_LED, HIGH);
        digitalWrite(RED_LED, LOW);
        break;
      case POOR_DANGEROUS:
        digitalWrite(BLUE_LED, LOW);
        digitalWrite(YELLOW_LED, LOW);
        digitalWrite(RED_LED, HIGH);
        break;
      default:
        digitalWrite(BLUE_LED, LOW);
        digitalWrite(YELLOW_LED, LOW);
        digitalWrite(RED_LED, LOW);
        break;
    }

    Serial.print("Env Status - Temp: ");
    Serial.print(tempLocal);
    Serial.print("C, Humidity: ");
    Serial.print(humLocal);
    Serial.print("%, NH3: ");
    Serial.print(ppmLocal, 1);
    Serial.print("ppm | STATE ");
    Serial.print((int)stateLocal);
    Serial.print(" (");
    Serial.print(abbrev);
    Serial.println(")");
  }

  if ((now - lastBlynkUploadTime >= BLYNK_UPLOAD_INTERVAL) &&
      (WiFi.status() == WL_CONNECTED && Blynk.connected())) {
    lastBlynkUploadTime = now;

    float tempLocal, humLocal, ppmLocal;
    FarmState stateLocal;

    portENTER_CRITICAL(&mux);
    tempLocal = temperature;
    humLocal = humidity;
    ppmLocal = ppm;
    stateLocal = currentState;
    portEXIT_CRITICAL(&mux);

    Blynk.virtualWrite(V0, tempLocal);
    Blynk.virtualWrite(V1, humLocal);
    Blynk.virtualWrite(V2, ppmLocal);
    Blynk.virtualWrite(V3, (int)stateLocal);  // Send enum integer directly
  }

  delay(10);
}

FarmState classifyFarmState(float temperature, float humidity, float ammonia_ppm) {
  if ((20 <= temperature && temperature <= 30) &&
      (50 <= humidity && humidity <= 70) &&
      (ammonia_ppm < 10)) {
    return OPTIMAL;
  }
  else if ((temperature > 35 || temperature < 15) ||
           (humidity > 80 || humidity < 30) ||
           (ammonia_ppm > 25)) {
    return POOR_DANGEROUS;
  }
  else {
    return SUBOPTIMAL;
  }
}

String getStateAbbrev(FarmState state) {
  switch (state) {
    case OPTIMAL: return "OK";
    case SUBOPTIMAL: return "!!";
    case POOR_DANGEROUS: return "XX";
    default: return "??";
  }
}

void calibrate() {
  float val = 0;
  int validSamples = 0;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrating...");
  lcd.setCursor(0, 1);
  lcd.print("Keep in clean air");

  Serial.println("=== MQ-137 Calibration Started ===");
  Serial.println("Ensure sensor is in clean air environment");
  Serial.println("Taking 50 samples over 25 seconds...");

  for (int i = 0; i < CALIBRATION_SAMPLE_TIMES; i++) {
    int adcValue = analogRead(MQ_Pin);
    float sensorVoltage = (adcValue / 4095.0) * 3.3;

    if (sensorVoltage > 0.1) {
      float Rs = ((5.0 * RL_VALUE) / sensorVoltage) - RL_VALUE;
      if (Rs > 0) {
        val += Rs;
        validSamples++;
      }
    }

    lcd.setCursor(0, 1);
    lcd.print("Sample: ");
    lcd.print(i + 1);
    lcd.print("/50   ");

    delay(CALIBRATION_SAMPLE_INTERVAL);
  }

  if (validSamples > 0) {
    RO_CLEAN_AIR_VALUE = val / validSamples;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibration Done");
    lcd.setCursor(0, 1);
    lcd.print("R0: ");
    lcd.print(RO_CLEAN_AIR_VALUE, 1);
    lcd.print(" kOhm");

    Serial.println("=== Calibration Complete ===");
    Serial.print("Valid samples: ");
    Serial.println(validSamples);
    Serial.print("New R0 value: ");
    Serial.print(RO_CLEAN_AIR_VALUE, 2);
    Serial.println(" kΩ");

    delay(3000);
  }
  else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibration");
    lcd.setCursor(0, 1);
    lcd.print("FAILED!");

    Serial.println("=== Calibration FAILED ===");
    Serial.println("No valid samples obtained");

    delay(2000);
  }
}
