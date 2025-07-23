#include <DHT.h>
#include <LiquidCrystal_I2C.h>

/* 
 * POULTRY FARM ENVIRONMENTAL MONITORING SYSTEM
 * 
 * This system monitors three critical environmental factors for poultry health:
 * 1. TEMPERATURE: Affects bird comfort, feed conversion, and mortality
 * 2. HUMIDITY: Controls pathogen growth and respiratory health  
 * 3. AMMONIA (NH3): Impacts respiratory system and immune response
 * 
 * Based on research from poultry science documentation showing that
 * environmental conditions directly affect bird productivity and welfare.
 */

// Sensor and pins
const int DHT_Pin = 26;
const int MQ_Pin = 33;
const int Calibration_Switch = 35;
const int RED_LED = 4;
const int YELLOW_LED = 16;
const int BLUE_LED = 17;

// Long press calibration variables
const unsigned long CALIBRATION_HOLD_TIME = 5000; // 5 seconds in milliseconds
unsigned long buttonPressStartTime = 0;
bool calibrationTriggered = false;
bool showingHoldMessage = false;

// DHT sensor setup
#define DHT_TYPE DHT22
DHT dht(DHT_Pin, DHT_TYPE);

// LCD setup (I2C address 0x27, 16x2 display)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MQ-137 calibration constants
#define RL_VALUE 47
#define SLOPE -0.263
#define INTERCEPT 0.42

// Calibration parameters
#define CALIBRATION_SAMPLE_TIMES 50
#define CALIBRATION_SAMPLE_INTERVAL 500

// Global variable for R0
float RO_CLEAN_AIR_VALUE = 30.0;

// Environmental state classification (same as before)
enum FarmState {
  OPTIMAL = 1,
  SUBOPTIMAL = 2,
  POOR_DANGEROUS = 3
};

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

void calibrate() {
  float val = 0;
  int validSamples = 0;
  
  // Display calibration start message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrating...");
  lcd.setCursor(0, 1);
  lcd.print("Keep in clean air");
  
  Serial.println("=== MQ-137 Calibration Started ===");
  Serial.println("Ensure sensor is in clean air environment");
  Serial.println("Taking 50 samples over 25 seconds...");
  
  // Take multiple samples for accuracy
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

void setup() {
  Serial.begin(115200);
  
  pinMode(Calibration_Switch, INPUT_PULLUP);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  
  // Initialize all LEDs to OFF
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  
  dht.begin();
  lcd.init();
  lcd.backlight();
  analogReadResolution(12);
  
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
  // NEW LONG PRESS CALIBRATION LOGIC
  int buttonState = digitalRead(Calibration_Switch);
  
  if (buttonState == LOW && !calibrationTriggered) {
    // Button is being pressed
    if (buttonPressStartTime == 0) {
      // First time detecting button press
      buttonPressStartTime = millis();
      showingHoldMessage = false;
      Serial.println("Calibration button pressed - hold for 5 seconds...");
    }
    
    unsigned long holdDuration = millis() - buttonPressStartTime;
    
    // Show "Hold for calibration" message after 1 second
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
    
    // Update countdown display
    if (showingHoldMessage && holdDuration < CALIBRATION_HOLD_TIME) {
      lcd.setCursor(6, 1);
      lcd.print((CALIBRATION_HOLD_TIME - holdDuration) / 1000);
      lcd.print(" sec  ");
    }
    
    // Trigger calibration after 5 seconds
    if (holdDuration >= CALIBRATION_HOLD_TIME) {
      Serial.println("5 seconds reached - starting calibration!");
      calibrate();
      calibrationTriggered = true;
      showingHoldMessage = false;
    }
  } 
  else if (buttonState == HIGH) {
    // Button released - reset timing
    if (buttonPressStartTime != 0 && !calibrationTriggered) {
      Serial.println("Button released before 5 seconds - calibration cancelled");
    }
    buttonPressStartTime = 0;
    calibrationTriggered = false;
    showingHoldMessage = false;
  }
  
  // Only proceed with normal operation if not showing hold message
  if (!showingHoldMessage) {
    // Read DHT sensor for temperature and humidity
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      temperature = 0;
      humidity = 0;
    }
    
    // Read MQ-137 sensor for ammonia detection
    int adcValue = analogRead(MQ_Pin);
    float sensorVoltage = (adcValue / 4095.0) * 3.3;
    
    float Rs = ((5.0 * RL_VALUE) / sensorVoltage) - RL_VALUE;
    float ratio = Rs / RO_CLEAN_AIR_VALUE;
    float ppm = pow(10, ((log10(ratio) - INTERCEPT) / SLOPE));
    
    if (ppm < 0 || isnan(ppm) || isinf(ppm)) {
      ppm = 0;
    }
    
    // Classify farm environmental state
    FarmState currentState = classifyFarmState(temperature, humidity, ppm);
    String stateAbbrev = getStateAbbrev(currentState);
    
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
    lcd.print(currentState);
    lcd.print(":");
    lcd.print(stateAbbrev);
    
    // Status LED control
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
    
    // Serial output
    Serial.print("Environmental Status - Temp: ");
    Serial.print(temperature);
    Serial.print("°C, Humidity: ");
    Serial.print(humidity);
    Serial.print("%, NH3: ");
    Serial.print(ppm, 1);
    Serial.print("ppm | STATE ");
    Serial.print(currentState);
    Serial.print(" (");
    Serial.print(stateAbbrev);
    Serial.println(")");
  }
  
  delay(2000); // Reduced delay for better button responsiveness
}
