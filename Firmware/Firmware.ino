#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>


// --- Network Settings ---
const char* AP_SSID = "Pond-Water-Monitor-Pro";
const char* AP_PASS = "pond123456";
const char* ST_SSID = "YOUR_WIFI";      // Change to your home WiFi SSID if needed
const char* ST_PASS = "YOUR_PASSWORD";  // Change to your home WiFi Password

// --- Core Architecture Definitions ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// FreeRTOS Task Handles
TaskHandle_t SensorTaskHandle = NULL;
TaskHandle_t TelemetryTaskHandle = NULL;
SemaphoreHandle_t dataMutex;

// --- Data Structures ---
struct WaterMetrics {
    float temperature = 25.0;
    float ph = 7.0;
    float tds = 0.0;
    float turbidity = 0.0;
    unsigned long timestamp = 0;
};

struct CalibrationData {
    float ph_4_voltage;
    float ph_7_voltage;
    float tds_factor;
    float turbidity_offset;
    float temp_offset;
};

// Global Context Instantiations
WaterMetrics currentMetrics;
CalibrationData calibration;
bool isSystemCalibrated = false;

// --- Signal Conditioning & Filtering ---
// Sorts samples and calculates the average of the middle variance band to eliminate spike noise
float readFilteredAnalog(uint8_t pin, int totalSamples = 32) {
    int samples[totalSamples];
    
    // Acquire samples
    for (int i = 0; i < totalSamples; i++) {
        samples[i] = analogRead(pin);
        delayMicroseconds(80);
    }
    
    // Simple bubble sort
    for (int i = 0; i < totalSamples - 1; i++) {
        for (int j = i + 1; j < totalSamples; j++) {
            if (samples[i] > samples[j]) {
                int temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }
    
    // Discard upper and lower 25% of samples, average the middle 50%
    long sum = 0;
    int startSample = totalSamples / 4;
    int endSample = totalSamples - startSample;
    int sampleCount = endSample - startSample;
    
    for (int i = startSample; i < endSample; i++) {
        sum += samples[i];
    }
    
    return (float)sum / sampleCount;
}

// --- Non-Volatile Memory (NVM) Management ---
void loadCalibrationSettings() {
    preferences.begin("pond_cal_v2", false);
    calibration.ph_4_voltage = preferences.getFloat("ph4v", 3.05);
    calibration.ph_7_voltage = preferences.getFloat("ph7v", 2.50);
    calibration.tds_factor = preferences.getFloat("tdsf", 1.00);
    calibration.turbidity_offset = preferences.getFloat("turboff", 0.0);
    calibration.temp_offset = preferences.getFloat("tempoff", 0.0);
    isSystemCalibrated = preferences.getBool("is_cal", false);
    preferences.end();
    Serial.println("[SYSTEM] Calibration settings loaded successfully from NVM.");
}

void saveCalibrationSettings() {
    preferences.begin("pond_cal_v2", false);
    preferences.putFloat("ph4v", calibration.ph_4_voltage);
    preferences.putFloat("ph7v", calibration.ph_7_voltage);
    preferences.putFloat("tdsf", calibration.tds_factor);
    preferences.putFloat("turboff", calibration.turbidity_offset);
    preferences.putFloat("tempoff", calibration.temp_offset);
    preferences.putBool("is_cal", true);
    preferences.end();
    Serial.println("[SYSTEM] NV-Storage written with updated calibration metrics.");
}

void resetCalibrationSettings() {
    preferences.begin("pond_cal_v2", false);
    preferences.clear();
    preferences.end();
    loadCalibrationSettings();
    Serial.println("[SYSTEM] Factory reset execution complete on NV-Storage namespace.");
}

// --- WebSocket Core Engine ---
void notifyClients() {
    if (ws.count() == 0) return; // Optimize transmission if no active sessions exist

    JsonDocument doc;
    doc["event"] = "telemetry";
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        doc["temp"] = currentMetrics.temperature;
        doc["ph"] = currentMetrics.ph;
        doc["tds"] = currentMetrics.tds;
        doc["turb"] = currentMetrics.turbidity;
        doc["uptime"] = currentMetrics.timestamp;
        xSemaphoreGive(dataMutex);
    }
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    ws.textAll(jsonOutput);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (error) return;

        if (doc.containsKey("request") && doc["request"] == "ping") {
            ws.textAll("{\"event\":\"pong\"}");
        }
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WEBSOCKET] ClientNormally I can help with things like this, but I don't seem to have access to that content. You can try again or ask me for something else.
