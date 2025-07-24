#define BLYNK_TEMPLATE_ID "TMPL6NkYtXFy4"
#define BLYNK_TEMPLATE_NAME "POULTRY FARM MANAGEMENT SYSTEM"
#define BLYNK_AUTH_TOKEN "Ar5-H-c2MrMfcBwxQAe72UBtV_DpQZCd"

// Include necessary libraries
#include <WiFi.h>                 // WiFi library for ESP32
#include <BlynkSimpleEsp32.h>     // Blynk library for ESP32
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// Replace with your network credentials
char ssid[] = "Wokwi-GUEST";       // <-- change to your WiFi SSID
char pass[] = "";   // <-- change to your WiFi password

// Sensor and pin setup
const int DHT_Pin = 26;
const int MQ_Pin = 33;
const int Calibration_Switch = 35;
const int RED_LED = 4;
const int YELLOW_LED = 16;
const int BLUE_LED = 17;
const int BLYNK_UPLOAD_ONGING = 15;
const int BLYNK_UPLOAD_STOPED = 2;

#define DHT_TYPE DHT22
DHT dht(DHT_Pin, DHT_TYPE);

// LCD setup (I2C address 0x27, 16x2 display)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MQ-137 calibration constants
#define RL_VALUE 47
#define SLOPE -0.263
#define INTERCEPT 0.42

#define CALIBRATION_SAMPLE_TIMES 50
#define CALIBRATION_SAMPLE_INTERVAL 500  // milliseconds

// Long press calibration parameters
const unsigned long CALIBRATION_HOLD_TIME = 5000; // 5 seconds
unsigned long buttonPressStartTime = 0;
bool calibrationTriggered = false;
bool showingHoldMessage = false;

// Default RO value for MQ sensor before calibration
float RO_CLEAN_AIR_VALUE = 30.0;

// Environmental state enum
enum FarmState {
  OPTIMAL = 1,
  SUBOPTIMAL = 2,
  POOR_DANGEROUS = 3
};

// Function declarations
FarmState classifyFarmState(float temperature, float humidity, float ammonia_ppm);
String getStateAbbrev(FarmState state);
String getStateString(FarmState state);
void calibrate();

void setup() {
  Serial.begin(115200);

  pinMode(Calibration_Switch, INPUT_PULLUP);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BLYNK_UPLOAD_ONGING, OUTPUT);
  pinMode(BLYNK_UPLOAD_STOPED, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(BLUE_LED, LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();
  analogReadResolution(12);

  // Connect to Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Welcome message
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

  //Checking is blynk Upload onging or not 

  if (Blynk.connected()) {
  digitalWrite(BLYNK_UPLOAD_ONGING, HIGH);
  digitalWrite(BLYNK_UPLOAD_STOPED, LOW);
} else {
  digitalWrite(BLYNK_UPLOAD_ONGING, LOW);
  digitalWrite(BLYNK_UPLOAD_STOPED, HIGH);
}

  // Read calibration button
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
  }
  else if (buttonState == HIGH) {
    if (buttonPressStartTime != 0 && !calibrationTriggered) {
      Serial.println("Button released before 5 seconds - calibration cancelled");
    }
    buttonPressStartTime = 0;
    calibrationTriggered = false;
    showingHoldMessage = false;
  }

  if (!showingHoldMessage) {
    // Read sensors
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      temperature = 0;
      humidity = 0;
    }

    int adcValue = analogRead(MQ_Pin);
    float sensorVoltage = (adcValue / 4095.0) * 3.3;

    float Rs = ((5.0 * RL_VALUE) / sensorVoltage) - RL_VALUE;
    float ratio = Rs / RO_CLEAN_AIR_VALUE;
    float ppm = pow(10, ((log10(ratio) - INTERCEPT) / SLOPE));

    if (ppm < 0 || isnan(ppm) || isinf(ppm)) {
      ppm = 0;
    }

    FarmState currentState = classifyFarmState(temperature, humidity, ppm);
    String stateAbbrev = getStateAbbrev(currentState);
    String stateStr = getStateString(currentState);

    // Send data to Blynk (temperature, humidity, ppm, state string)
    Blynk.virtualWrite(V0, temperature);
    Blynk.virtualWrite(V1, humidity);
    Blynk.virtualWrite(V2, ppm);
    Blynk.virtualWrite(V3, stateStr);

    // Display on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:");
    if (temperature < 10) lcd.print(" ");
    lcd.print(temperature, 1);
    lcd.print("C    H:");
    if (humidity < 10) lcd.print(" ");
    lcd.print(humidity, 0);
    lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print("NH3:");
    if (ppm < 10) lcd.print(" ");
    lcd.print(ppm, 0);
    lcd.print("PPM  S");
    lcd.print((int)currentState);
    lcd.print(":");
    lcd.print(stateAbbrev);

    // LED status based on state
    switch(currentState) {
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
    }

    Serial.print("Environmental Status - Temp: ");
    Serial.print(temperature);
    Serial.print("°C, Humidity: ");
    Serial.print(humidity);
    Serial.print("%, NH3: ");
    Serial.print(ppm, 1);
    Serial.print("ppm | STATE ");
    Serial.print((int)currentState);
    Serial.print(" (");
    Serial.print(stateAbbrev);
    Serial.println(")");
  }

  delay(10000);
}

// ======= Function Definitions =======

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
  switch(state) {
    case OPTIMAL:
      return "OK";
    case SUBOPTIMAL:
      return "!!";
    case POOR_DANGEROUS:
      return "XX";
    default:
      return "??";
  }
}

String getStateString(FarmState state) {
  switch(state) {
    case OPTIMAL:
      return "optimal";
    case SUBOPTIMAL:
      return "suboptimal";
    case POOR_DANGEROUS:
      return "poor dangerous";
    default:
      return "unknown";
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
  } else {
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
