#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <FirebaseClient.h>

#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// TensorFlow Lite Micro
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"

#include "poultry_model_int8.h"


// ============================================================
// FIREBASE CONFIGURATION
// ============================================================

#define FIREBASE_API_KEY      "AIzaSyB2cWXPm2ROHxdSdS2bQelDQSq832lj72M"
#define FIREBASE_DATABASE_URL "https://poultry-farm-management-2abc5-default-rtdb.firebaseio.com"


// ============================================================
// WIFI
// ============================================================

#define WIFI_SSID     "PFMS"
#define WIFI_PASSWORD "11111111"


// ============================================================
// FIREBASE OBJECTS
// ============================================================

WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);

FirebaseApp app;
RealtimeDatabase Database;

UserAccount firebaseUser(FIREBASE_API_KEY);

AsyncResult firebaseResult;


// ============================================================
// FIREBASE TIMING
// ============================================================

unsigned long lastFirebaseUpload = 0;

const unsigned long FIREBASE_UPLOAD_INTERVAL = 60000;


// ============================================================
// PIN CONFIGURATION
// ============================================================

// Sensors
#define DHTPIN      5
#define DHTTYPE     DHT22

#define MQ135_PIN   6

#define LCD_SDA     8
#define LCD_SCL     9


// Online/offline switch
#define LIVE_UPDATE_REVIEW 12


// LEDs
#define RED_LED      38
#define YELLOW_LED   39
#define BLUE_LED     40

#define FIREBASE_UPLOAD_ONGOING 10
#define FIREBASE_UPLOAD_STOPPED 11


// Actuators
#define FAN_PIN          16
#define HEATER_PIN       17
#define VENTILATION_PIN  18
#define SIREN_PIN         7


// ============================================================
// SENSOR OBJECTS
// ============================================================

DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);


// ============================================================
// MQ135 CONFIGURATION
// ============================================================

#define RLOAD 22.0f

float RZERO = 76.63f;

const float NH3_CURVE_A = 102.2f;
const float NH3_CURVE_B = -2.473f;


// ============================================================
// TINYML SCALER
// ============================================================
//
// IMPORTANT:
// Keep these values exactly matching the preprocessing used
// during model training.
//
// Your current working TinyML version uses raw values:
//
// mean = 0
// scale = 1
//
// Do NOT change these unless the model was trained using
// different scaler values.
// ============================================================

float FEATURE_MEAN[3]  = {
    0.0f,
    0.0f,
    0.0f
};

float FEATURE_SCALE[3] = {
    1.0f,
    1.0f,
    1.0f
};


// TinyML class order
const char* STATE_LABELS[3] = {
    "Dangerous",
    "Optimal",
    "Suboptimal"
};


// ============================================================
// FARM STATE
// ============================================================
//
// Keep the old PFMS state numbering so LCD / website can use:
//
// 1 = Optimal
// 2 = Suboptimal
// 3 = Dangerous
// ============================================================

enum FarmState {

    OPTIMAL = 1,
    SUBOPTIMAL = 2,
    POOR_DANGEROUS = 3

};


// ============================================================
// SHARED SENSOR / ML DATA
// ============================================================

volatile float temperature = 0.0f;
volatile float humidity = 0.0f;
volatile float ammonia = 0.0f;

volatile FarmState currentState = OPTIMAL;

volatile float mlConfidence = 0.0f;
volatile unsigned long mlInferenceTime = 0;

volatile float mlScores[3] = {
    0.0f,
    0.0f,
    0.0f
};


portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;


// ============================================================
// TENSOR ARENA
// ============================================================

constexpr int kTensorArenaSize = 20 * 1024;

static uint8_t tensor_arena[kTensorArenaSize];


namespace {

    const tflite::Model* model = nullptr;

    tflite::MicroInterpreter* interpreter = nullptr;

    TfLiteTensor* input = nullptr;

    TfLiteTensor* output = nullptr;

    tflite::ErrorReporter* error_reporter = nullptr;

}


// ============================================================
// FIREBASE STATUS
// ============================================================

bool firebaseStarted = false;


// ============================================================
// WIFI CONTROL
// ============================================================

bool wifiConnected = false;

unsigned long lastWiFiAttempt = 0;

const unsigned long WIFI_RETRY_INTERVAL = 15000;


// ============================================================
// MQ135
// ============================================================

float readMQ135Resistance() {

    int raw_val = analogRead(MQ135_PIN);

    float voltage = raw_val * (3.3f / 4095.0f);

    if (voltage <= 0.01f)
        voltage = 0.01f;

    float rs =
        RLOAD *
        (3.3f - voltage) /
        voltage;

    return rs;
}


float readAmmoniaPPM() {

    float rs = readMQ135Resistance();

    float ratio = rs / RZERO;

    float ppm =
        NH3_CURVE_A *
        pow(ratio, NH3_CURVE_B);

    if (ppm < 0 || isnan(ppm) || isinf(ppm))
        ppm = 0;

    return ppm;
}


// ============================================================
// MAP TINYML RESULT TO PFMS STATE
// ============================================================

FarmState mapMLState(int bestIndex) {

    switch (bestIndex) {

        case 0:
            return POOR_DANGEROUS;

        case 1:
            return OPTIMAL;

        case 2:
            return SUBOPTIMAL;

        default:
            return SUBOPTIMAL;
    }
}


// ============================================================
// STATE ABBREVIATION
// ============================================================

String getStateAbbrev(FarmState state) {

    switch (state) {

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


// ============================================================
// TINYML INFERENCE
// ============================================================

void runInference(
    float temp,
    float hum,
    float nh3
) {

    float raw_data[3];

    raw_data[0] = temp;
    raw_data[1] = hum;
    raw_data[2] = nh3;


    // ----------------------------
    // Standardization
    // ----------------------------

    float scaled_data[3];

    for (int i = 0; i < 3; i++) {

        scaled_data[i] =
            (raw_data[i] - FEATURE_MEAN[i])
            /
            FEATURE_SCALE[i];
    }


    // ----------------------------
    // INT8 quantization
    // ----------------------------

    float in_scale =
        input->params.scale;

    int in_zero =
        input->params.zero_point;


    for (int i = 0; i < 3; i++) {

        int32_t q =
            (int32_t)roundf(
                scaled_data[i] /
                in_scale
            )
            + in_zero;


        if (q < -128)
            q = -128;

        if (q > 127)
            q = 127;


        input->data.int8[i] =
            (int8_t)q;
    }


    // ----------------------------
    // Run model
    // ----------------------------

    unsigned long startTime =
        micros();


    TfLiteStatus status =
        interpreter->Invoke();


    unsigned long endTime =
        micros();


    if (status != kTfLiteOk) {

        Serial.println(
            "ERROR: TinyML Invoke() failed."
        );

        return;
    }


    // ----------------------------
    // Dequantize output
    // ----------------------------

    float rawOutput[3];

    float outputScale =
        output->params.scale;

    int outputZero =
        output->params.zero_point;


    for (int i = 0; i < 3; i++) {

        rawOutput[i] =
            (
                output->data.int8[i]
                -
                outputZero
            )
            *
            outputScale;
    }


    // ----------------------------
    // Determine whether the model
    // already outputs probabilities
    // ----------------------------

    float sum =
        rawOutput[0]
        +
        rawOutput[1]
        +
        rawOutput[2];


    float probabilities[3];


    bool looksLikeProbability =
        true;


    for (int i = 0; i < 3; i++) {

        if (
            rawOutput[i] < 0.0f ||
            rawOutput[i] > 1.0f
        ) {

            looksLikeProbability = false;
        }
    }


    if (
        fabs(sum - 1.0f) > 0.05f
    ) {

        looksLikeProbability = false;
    }


    // ----------------------------
    // Softmax if necessary
    // ----------------------------

    if (looksLikeProbability) {

        for (int i = 0; i < 3; i++) {

            probabilities[i] =
                rawOutput[i];
        }

    }

    else {

        float maxValue =
            rawOutput[0];

        for (int i = 1; i < 3; i++) {

            if (rawOutput[i] > maxValue)
                maxValue = rawOutput[i];
        }


        float expSum = 0.0f;

        for (int i = 0; i < 3; i++) {

            probabilities[i] =
                expf(
                    rawOutput[i] -
                    maxValue
                );

            expSum += probabilities[i];
        }


        for (int i = 0; i < 3; i++) {

            probabilities[i] /=
                expSum;
        }
    }


    // ----------------------------
    // Find best class
    // ----------------------------

    int bestIndex = 0;

    float bestProbability =
        probabilities[0];


    for (int i = 1; i < 3; i++) {

        if (
            probabilities[i]
            >
            bestProbability
        ) {

            bestProbability =
                probabilities[i];

            bestIndex = i;
        }
    }


    FarmState predictedState =
        mapMLState(bestIndex);


    // ----------------------------
    // Store shared results
    // ----------------------------

    portENTER_CRITICAL(&mux);

    currentState =
        predictedState;

    mlConfidence =
        bestProbability * 100.0f;

    mlInferenceTime =
        endTime - startTime;


    for (int i = 0; i < 3; i++) {

        mlScores[i] =
            probabilities[i];
    }

    portEXIT_CRITICAL(&mux);


    // ----------------------------
    // Serial output
    // ----------------------------

    Serial.println();
    Serial.println(
        "========== TinyML =========="
    );


    Serial.print("Temperature: ");
    Serial.print(temp, 1);
    Serial.println(" C");


    Serial.print("Humidity: ");
    Serial.print(hum, 1);
    Serial.println(" %");


    Serial.print("Ammonia: ");
    Serial.print(nh3, 1);
    Serial.println(" ppm");


    Serial.println(
        "Probabilities:"
    );


    for (int i = 0; i < 3; i++) {

        Serial.print(
            STATE_LABELS[i]
        );

        Serial.print(": ");

        Serial.print(
            probabilities[i] * 100.0f,
            2
        );

        Serial.println("%");
    }


    Serial.print(
        "Predicted State: "
    );

    Serial.println(
        STATE_LABELS[bestIndex]
    );


    Serial.print(
        "Confidence: "
    );

    Serial.print(
        bestProbability * 100.0f,
        2
    );

    Serial.println("%");


    Serial.print(
        "Inference time: "
    );

    Serial.print(
        endTime - startTime
    );

    Serial.println(" us");


    Serial.println(
        "============================"
    );
}


// ============================================================
// SENSOR TASK
// ============================================================

void sensorTask(void* pv) {

    for (;;) {

        float temp =
            dht.readTemperature();

        float hum =
            dht.readHumidity();


        if (
            isnan(temp) ||
            isnan(hum)
        ) {

            Serial.println(
                "Failed to read DHT22!"
            );

            vTaskDelay(
                pdMS_TO_TICKS(2500)
            );

            continue;
        }


        float nh3 =
            readAmmoniaPPM();


        runInference(
            temp,
            hum,
            nh3
        );


        portENTER_CRITICAL(&mux);

        temperature = temp;

        humidity = hum;

        ammonia = nh3;

        portEXIT_CRITICAL(&mux);


        vTaskDelay(
            pdMS_TO_TICKS(5000)
        );
    }
}


// ============================================================
// ACTUATORS
// ============================================================

void updateActuators(
    float temp,
    float hum,
    float nh3,
    FarmState state
) {

    switch (state) {


        // ------------------------
        // OPTIMAL
        // ------------------------

        case OPTIMAL:

            digitalWrite(
                BLUE_LED,
                HIGH
            );

            digitalWrite(
                YELLOW_LED,
                LOW
            );

            digitalWrite(
                RED_LED,
                LOW
            );


            digitalWrite(
                FAN_PIN,
                LOW
            );

            digitalWrite(
                HEATER_PIN,
                LOW
            );

            digitalWrite(
                VENTILATION_PIN,
                LOW
            );

            digitalWrite(
                SIREN_PIN,
                LOW
            );

            break;


        // ------------------------
        // SUBOPTIMAL
        // ------------------------

        case SUBOPTIMAL:

            digitalWrite(
                BLUE_LED,
                LOW
            );

            digitalWrite(
                YELLOW_LED,
                HIGH
            );

            digitalWrite(
                RED_LED,
                LOW
            );


            if (temp > 30) {

                digitalWrite(
                    FAN_PIN,
                    HIGH
                );

            }

            else if (
                temp < 20 &&
                temp >= 15
            ) {

                digitalWrite(
                    FAN_PIN,
                    LOW
                );

                digitalWrite(
                    HEATER_PIN,
                    HIGH
                );

            }

            else {

                digitalWrite(
                    FAN_PIN,
                    LOW
                );

                digitalWrite(
                    HEATER_PIN,
                    LOW
                );
            }


            if (
                (hum > 70 && hum <= 80)
                ||
                (nh3 > 10 && nh3 < 25)
            ) {

                digitalWrite(
                    VENTILATION_PIN,
                    HIGH
                );

            }

            else {

                digitalWrite(
                    VENTILATION_PIN,
                    LOW
                );
            }


            digitalWrite(
                SIREN_PIN,
                LOW
            );

            break;


        // ------------------------
        // DANGEROUS
        // ------------------------

        case POOR_DANGEROUS:

            digitalWrite(
                BLUE_LED,
                LOW
            );

            digitalWrite(
                YELLOW_LED,
                LOW
            );

            digitalWrite(
                RED_LED,
                HIGH
            );


            digitalWrite(
                FAN_PIN,
                temp > 35
                    ? HIGH
                    : LOW
            );


            digitalWrite(
                VENTILATION_PIN,
                (
                    temp > 35
                    ||
                    hum > 80
                    ||
                    nh3 > 25
                )
                    ? HIGH
                    : LOW
            );


            digitalWrite(
                HEATER_PIN,
                temp < 15
                    ? HIGH
                    : LOW
            );


            digitalWrite(
                SIREN_PIN,
                HIGH
            );

            break;
    }
}


// ============================================================
// FIREBASE AUTH DEBUG
// ============================================================

void auth_debug_print(
    AsyncResult &result
) {

    if (result.isEvent()) {

        Firebase.printf(
            "Auth Event: %s | %s | code=%d\n",
            result.uid().c_str(),
            result.eventLog().message().c_str(),
            result.eventLog().code()
        );
    }


    if (result.isDebug()) {

        Firebase.printf(
            "Auth Debug: %s\n",
            result.debug().c_str()
        );
    }


    if (result.isError()) {

        Firebase.printf(
            "Auth Error: %s | code=%d\n",
            result.error().message().c_str(),
            result.error().code()
        );
    }
}


// ============================================================
// FIREBASE RESULT PROCESSOR
// ============================================================

void processFirebaseResult(
    AsyncResult &result
) {

    if (!result.isResult())
        return;

    // A completed AsyncResult remains available until it is reused. Since
    // loop() runs continuously, print each task only once.
    static String lastCurrentTask;
    static String lastHistoryTask;
    String taskId = result.uid();
    String &lastTask = (&result == &firebaseResult)
        ? lastCurrentTask
        : lastHistoryTask;

    if (taskId.length() > 0) {
        if (taskId == lastTask)
            return;

        lastTask = taskId;
    }


    if (result.isEvent()) {

        Firebase.printf(
            "Firebase Event: %s | %s | code=%d\n",
            result.uid().c_str(),
            result.eventLog().message().c_str(),
            result.eventLog().code()
        );
    }


    if (result.isDebug()) {

        Firebase.printf(
            "Firebase Debug: %s\n",
            result.debug().c_str()
        );
    }


    if (result.isError()) {

        Firebase.printf(
            "Firebase Error: %s | code=%d\n",
            result.error().message().c_str(),
            result.error().code()
        );
    }


}


// ============================================================
// FIREBASE START
// ============================================================

void startFirebase() {

    Serial.println();
    Serial.println(
        "Starting Firebase..."
    );


    Firebase.printf(
        "Firebase Client v%s\n",
        FIREBASE_CLIENT_VERSION
    );


    ssl_client.setInsecure();

    ssl_client.setHandshakeTimeout(
        5
    );


    // Anonymous authentication
    signup(
        aClient,
        app,
        getAuth(firebaseUser),
        auth_debug_print,
        "anonymousSignup"
    );


    app.getApp<RealtimeDatabase>(
        Database
    );


    Database.url(
        FIREBASE_DATABASE_URL
    );


    firebaseStarted = true;


    Serial.println(
        "Firebase initialization started."
    );
}


// ============================================================
// WIFI
// ============================================================

void startWiFi() {

    Serial.println();

    Serial.print(
        "Connecting to WiFi: "
    );

    Serial.println(
        WIFI_SSID
    );


    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    unsigned long start =
        millis();


    while (
        WiFi.status() != WL_CONNECTED
        &&
        millis() - start < 15000
    ) {

        Serial.print(".");

        delay(300);
    }


    Serial.println();


    if (
        WiFi.status()
        ==
        WL_CONNECTED
    ) {

        wifiConnected = true;


        Serial.println(
            "WiFi connected!"
        );


        Serial.print(
            "IP: "
        );

        Serial.println(
            WiFi.localIP()
        );


        configTime(
            0,
            0,
            "pool.ntp.org",
            "time.nist.gov"
        );


        startFirebase();

    }

    else {

        wifiConnected = false;

        Serial.println(
            "WiFi connection failed."
        );
    }
}


// ============================================================
// FIREBASE JSON
// ============================================================

String createFirebaseJSON(
    float temp,
    float hum,
    float nh3,
    FarmState state,
    float confidence,
    unsigned long inferenceTime
) {
    String stateText;


    switch (state) {

        case OPTIMAL:
            stateText = "Optimal";
            break;

        case SUBOPTIMAL:
            stateText = "Suboptimal";
            break;

        case POOR_DANGEROUS:
            stateText = "Dangerous";
            break;

        default:
            stateText = "Unknown";
    }


    String json = "{\"temperature\":";
    json += String(temp, 1);
    json += ",\"humidity\":";
    json += String(hum, 1);
    json += ",\"ammonia\":";
    json += String(nh3, 1);
    json += ",\"state\":\"";
    json += stateText;
    json += "\",\"timestamp\":{\".sv\":\"timestamp\"}}";


    return json;
}


String createActuatorsJSON() {

    String json = "{\"fan\":";
    json += digitalRead(FAN_PIN) ? "true" : "false";
    json += ",\"heater\":";
    json += digitalRead(HEATER_PIN) ? "true" : "false";
    json += ",\"ventilation\":";
    json += digitalRead(VENTILATION_PIN) ? "true" : "false";
    json += ",\"siren\":";
    json += digitalRead(SIREN_PIN) ? "true" : "false";
    json += "}";

    return json;
}


// ============================================================
// FIREBASE UPLOAD
// ============================================================

void uploadToFirebase() {

    if (!wifiConnected)
        return;


    if (!firebaseStarted)
        return;


    if (!app.ready())
        return;


    // Prevent queue from filling
    if (aClient.taskCount() >= 8) {

        Serial.println(
            "Firebase queue busy - skipping upload."
        );

        return;
    }


    float temp;
    float hum;
    float nh3;

    FarmState state;

    float confidence;

    unsigned long inferenceTime;


    portENTER_CRITICAL(&mux);

    temp = temperature;

    hum = humidity;

    nh3 = ammonia;

    state = currentState;

    confidence = mlConfidence;

    inferenceTime = mlInferenceTime;

    portEXIT_CRITICAL(&mux);


    String json =
        createFirebaseJSON(
            temp,
            hum,
            nh3,
            state,
            confidence,
            inferenceTime
        );


    Serial.println();
    Serial.println(
        "Uploading to Firebase..."
    );


    Serial.println(json);


    // ---------------------------------
    // Current data
    // ---------------------------------

    Database.set<object_t>(
        aClient,
        "/pfms/current",
        object_t(json),
        firebaseResult
    );

    Database.push<object_t>(
        aClient,
        "/pfms/history",
        object_t(json),
        firebaseResult
    );

    Database.set<object_t>(
        aClient,
        "/pfms/actuators",
        object_t(createActuatorsJSON()),
        firebaseResult
    );


}


// ============================================================
// STARTUP LCD
// ============================================================

void startupAnimation() {

    String text =
        "POULTRY FARM MANAGEMENT SYSTEM ";

    String padding =
        "                ";

    String scrollText =
        padding + text + padding;


    for (
        int i = 0;
        i < scrollText.length() - 15;
        i++
    ) {

        lcd.clear();

        lcd.setCursor(
            0,
            0
        );

        lcd.print(
            scrollText.substring(
                i,
                i + 16
            )
        );

        delay(250);
    }


    lcd.setCursor(
        0,
        1
    );

    lcd.print(
        "Starting Up..."
    );


    delay(2000);
}


// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);


    delay(1000);


    Serial.println();
    Serial.println(
        "======================================"
    );

    Serial.println(
        " PFMS - TinyML + Firebase"
    );

    Serial.println(
        "======================================"
    );


    // ----------------------------
    // GPIO
    // ----------------------------

    pinMode(
        LIVE_UPDATE_REVIEW,
        INPUT_PULLUP
    );


    pinMode(
        RED_LED,
        OUTPUT
    );

    pinMode(
        YELLOW_LED,
        OUTPUT
    );

    pinMode(
        BLUE_LED,
        OUTPUT
    );


    pinMode(
        FIREBASE_UPLOAD_ONGOING,
        OUTPUT
    );

    pinMode(
        FIREBASE_UPLOAD_STOPPED,
        OUTPUT
    );


    pinMode(
        FAN_PIN,
        OUTPUT
    );

    pinMode(
        HEATER_PIN,
        OUTPUT
    );

    pinMode(
        VENTILATION_PIN,
        OUTPUT
    );

    pinMode(
        SIREN_PIN,
        OUTPUT
    );


    // Safe initial state

    digitalWrite(
        RED_LED,
        LOW
    );

    digitalWrite(
        YELLOW_LED,
        LOW
    );

    digitalWrite(
        BLUE_LED,
        HIGH
    );


    digitalWrite(
        FAN_PIN,
        LOW
    );

    digitalWrite(
        HEATER_PIN,
        LOW
    );

    digitalWrite(
        VENTILATION_PIN,
        LOW
    );

    digitalWrite(
        SIREN_PIN,
        LOW
    );


    // ----------------------------
    // DHT
    // ----------------------------

    dht.begin();


    // ----------------------------
    // LCD
    // ----------------------------

    Wire.setPins(LCD_SDA, LCD_SCL);
    lcd.init();

    lcd.backlight();


    // ----------------------------
    // ADC
    // ----------------------------

    analogReadResolution(12);

    analogSetPinAttenuation(
        MQ135_PIN,
        ADC_11db
    );


    startupAnimation();


    // ========================================================
    // TINYML INITIALIZATION
    // ========================================================

    Serial.println(
        "Initializing TinyML..."
    );


    model =
        tflite::GetModel(
            poultry_model_int8_tflite
        );


    if (
        model->version()
        !=
        TFLITE_SCHEMA_VERSION
    ) {

        Serial.println(
            "ERROR: Model schema mismatch!"
        );

        while (true) {

            delay(1000);
        }
    }


    static tflite::AllOpsResolver resolver;


    static tflite::MicroErrorReporter
        micro_error_reporter;


    error_reporter =
        &micro_error_reporter;


    static tflite::MicroInterpreter
        static_interpreter(
            model,
            resolver,
            tensor_arena,
            kTensorArenaSize,
            error_reporter
        );


    interpreter =
        &static_interpreter;


    if (
        interpreter->AllocateTensors()
        !=
        kTfLiteOk
    ) {

        Serial.println(
            "ERROR: AllocateTensors failed!"
        );


        while (true) {

            delay(1000);
        }
    }


    input =
        interpreter->input(0);


    output =
        interpreter->output(0);


    Serial.print(
        "TinyML input type: "
    );


    Serial.println(
        input->type == kTfLiteInt8
            ? "INT8"
            : "NOT INT8"
    );


    // ========================================================
    // MQ135 WARMUP
    // ========================================================

    Serial.println(
        "Warming up MQ135..."
    );


    for (
        int i = 0;
        i < 180;
        i++
    ) {

        delay(1000);


        if (
            i % 30 == 0 &&
            i > 0
        ) {

            Serial.printf(
                "Warmup: %d / 180 seconds\n",
                i
            );
        }
    }


    Serial.println(
        "MQ135 warmup complete!"
    );


    // ========================================================
    // SENSOR TASK
    // ========================================================

    xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        8192,
        NULL,
        1,
        NULL,
        0
    );


    // ========================================================
    // INITIAL WIFI
    // ========================================================

    if (
        digitalRead(
            LIVE_UPDATE_REVIEW
        )
        ==
        HIGH
    ) {

        startWiFi();
    }


    Serial.println();
    Serial.println(
        "PFMS setup complete."
    );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

    unsigned long now =
        millis();


    bool uploadEnabled =
        digitalRead(
            LIVE_UPDATE_REVIEW
        )
        ==
        HIGH;


    // ========================================================
    // FIREBASE / WIFI
    // ========================================================

    if (uploadEnabled) {


        if (
            WiFi.status()
            !=
            WL_CONNECTED
        ) {

            wifiConnected = false;


            if (
                now - lastWiFiAttempt
                >=
                WIFI_RETRY_INTERVAL
            ) {

                lastWiFiAttempt =
                    now;

                startWiFi();
            }
        }

        else {

            wifiConnected = true;
        }


        // Maintain Firebase authentication

        if (firebaseStarted) {

            app.loop();
        }


        // Upload

        if (
            wifiConnected
            &&
            firebaseStarted
            &&
            app.ready()
            &&
            now - lastFirebaseUpload
            >=
            FIREBASE_UPLOAD_INTERVAL
        ) {

            lastFirebaseUpload =
                now;


            uploadToFirebase();
        }


        digitalWrite(
            FIREBASE_UPLOAD_ONGOING,
            HIGH
        );

        digitalWrite(
            FIREBASE_UPLOAD_STOPPED,
            LOW
        );

    }

    else {


        digitalWrite(
            FIREBASE_UPLOAD_ONGOING,
            LOW
        );

        digitalWrite(
            FIREBASE_UPLOAD_STOPPED,
            HIGH
        );
    }


    // ========================================================
    // PROCESS FIREBASE RESULTS
    // ========================================================

    processFirebaseResult(
        firebaseResult
    );


    // ========================================================
    // GET SENSOR DATA
    // ========================================================

    float temp;
    float hum;
    float nh3;

    FarmState state;


    float confidence;


    portENTER_CRITICAL(&mux);

    temp =
        temperature;

    hum =
        humidity;

    nh3 =
        ammonia;

    state =
        currentState;

    confidence =
        mlConfidence;

    portEXIT_CRITICAL(&mux);


    // ========================================================
    // ACTUATORS
    // ========================================================

    updateActuators(
        temp,
        hum,
        nh3,
        state
    );


    // ========================================================
    // LCD
    // ========================================================

    static unsigned long lastLCD =
        0;


    if (
        now - lastLCD >=
        2000
    ) {

        lastLCD =
            now;


        char line1[17];

        char line2[17];


        String abbreviation =
            getStateAbbrev(
                state
            );


        snprintf(
            line1,
            sizeof(line1),
            "T:%4.1fC H:%2.0f%%",
            temp,
            hum
        );


        snprintf(
            line2,
            sizeof(line2),
            "NH3:%3.0f S%d:%-2s",
            nh3,
            (int)state,
            abbreviation.c_str()
        );


        lcd.setCursor(
            0,
            0
        );

        lcd.print(
            "                "
        );

        lcd.setCursor(
            0,
            0
        );

        lcd.print(
            line1
        );


        lcd.setCursor(
            0,
            1
        );

        lcd.print(
            "                "
        );

        lcd.setCursor(
            0,
            1
        );

        lcd.print(
            line2
        );
    }


    // ========================================================
    // SERIAL SUMMARY
    // ========================================================

    static unsigned long lastSummary =
        0;


    if (
        now - lastSummary >=
        10000
    ) {

        lastSummary =
            now;


        Serial.println();
        Serial.println(
            "========== PFMS =========="
        );


        Serial.print(
            "Temperature: "
        );

        Serial.print(
            temp,
            1
        );

        Serial.println(
            " C"
        );


        Serial.print(
            "Humidity: "
        );

        Serial.print(
            hum,
            1
        );

        Serial.println(
            " %"
        );


        Serial.print(
            "Ammonia: "
        );

        Serial.print(
            nh3,
            1
        );

        Serial.println(
            " ppm"
        );


        Serial.print(
            "AI State: "
        );

        Serial.println(
            state == OPTIMAL
                ? "Optimal"
                :
            state == SUBOPTIMAL
                ? "Suboptimal"
                :
                "Dangerous"
        );


        Serial.print(
            "AI Confidence: "
        );

        Serial.print(
            confidence,
            2
        );

        Serial.println(
            "%"
        );


        Serial.print(
            "WiFi: "
        );

        Serial.println(
            wifiConnected
                ? "CONNECTED"
                : "OFFLINE"
        );


        Serial.print(
            "Firebase: "
        );

        Serial.println(
            (
                firebaseStarted
                &&
                app.ready()
            )
                ? "READY"
                : "NOT READY"
        );


        Serial.println(
            "=========================="
        );
    }


    delay(10);
}