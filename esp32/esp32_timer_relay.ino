/*
 * ESP32_TimerRelay_Controller
 * Version: 1.2.1 (Single LED for all status)
 */

/*
ควบคุม relay ผ่าน MQTT ได้ 3 คำสั่ง:

ON: เปิด relay
OFF: ปิด relay
RESET: รีเซ็ตเวลานับถอยหลัง


แสดงสถานะผ่าน LED ตัวเดียว โดยเรียงลำดับความสำคัญ:

กะพริบเร็วมาก (100ms): 30 วินาทีสุดท้าย
กะพริบเร็ว (500ms): 3 นาทีสุดท้าย
กะพริบปกติ/ช้า: สถานะ WiFi


ยังคงฟังก์ชันเดิมทั้งหมด:

กดปุ่มสั้น: เปิด/รีเซ็ตเวลา
กดปุ่มค้าง: ปิดบังคับ
ปิดอัตโนมัติเมื่อครบเวลา


ส่งสถานะทั้งหมดผ่าน MQTT ทุก 10 วินาที หรือเมื่อมีการเปลี่ยนแปลง เช่น:

สถานะ WiFi และความแรงสัญญาณ
สถานะ relay แต่ละตัว
เวลาที่เหลือ (ถ้า relay กำลังทำงาน)

*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "101-IOT";
const char* password = "10101010";

// MQTT Configuration
const char* mqtt_server = "broker.hivemq.com";  
const int mqtt_port = 1883;
const char* mqtt_user = "";         // Public broker ไม่ต้องใส่ username
const char* mqtt_password = "";     // Public broker ไม่ต้องใส่ password
const char* device_id = "esp32_timer_relay_01"; 

// MQTT Topics
const char* topic_relay1_control = "singfirewire/relay/relay1/control"; // **เพิ่ม prefix "singfirewire/relay"**
const char* topic_relay2_control = "singfirewire/relay/relay2/control"; // **เพิ่ม prefix "singfirewire/relay"**
const char* topic_status = "singfirewire/relay/status"; 
const char* topic_command = "singfirewire/relay/command";

// ส่วนที่เหลือคงเดิมทั้งหมด

// Pin definitions
const int LED_PIN = 2;         // LED สำหรับแสดงสถานะทั้งหมด
const int SWITCH1_PIN = 22;    // สวิตช์ตัวที่ 1
const int SWITCH2_PIN = 23;    // สวิตช์ตัวที่ 2
const int RELAY1_PIN = 16;     // รีเลย์ตัวที่ 1
const int RELAY2_PIN = 19;     // รีเลย์ตัวที่ 2

// Timing configurations
const long EMERGENCY_BLINK = 100;   // กะพริบเร็วมาก (100ms) สำหรับ 30 วินาทีสุดท้าย
const long WARNING_BLINK = 500;     // กะพริบเร็ว (500ms) สำหรับ 3 นาทีสุดท้าย
const long WIFI_FAST_BLINK = 1000;  // กะพริบ 1 วินาที สำหรับไม่มี WiFi
const long WIFI_SLOW_BLINK = 3000;  // กะพริบช้า 3 วินาที สำหรับมี WiFi + Internet

const long CHECK_INTERVAL = 10000;        // ตรวจสอบสถานะทุก 10 วินาที
const long COUNTDOWN_TIME = 40 * 60 * 1000; // 40 นาที
const long WARNING_TIME = 3 * 60 * 1000;    // 3 นาทีสุดท้าย
const long URGENT_TIME = 30 * 1000;         // 30 วินาทีสุดท้าย
const long DEBOUNCE_DELAY = 50;             // ดีเลย์ป้องกันการกระเด้ง
const long LONG_PRESS_TIME = 2500;          // เวลากดค้าง 2.5 วินาที

// Global variables
WiFiClient espClient;
PubSubClient mqtt(espClient);
unsigned long lastMqttReconnectAttempt = 0;
const long MQTT_RECONNECT_INTERVAL = 5000;

// Operating variables
unsigned long previousMillis = 0;
bool ledState = LOW;
int connectionStatus = 0;
unsigned long relay1StartTime = 0;
unsigned long relay2StartTime = 0;
bool relay1Active = false;
bool relay2Active = false;
bool switch1LastState = HIGH;
bool switch2LastState = HIGH;
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
unsigned long switch1PressStart = 0;
unsigned long switch2PressStart = 0;
bool switch1LongPress = false;
bool switch2LongPress = false;

// Function to check internet connectivity
bool checkInternet() {
    HTTPClient http;
    http.begin("http://www.google.com");
    int httpCode = http.GET();
    http.end();
    return httpCode > 0;
}

// MQTT Callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(message);

    // Parse JSON command
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
        Serial.println("Failed to parse JSON");
        return;
    }

    // Process commands
    if (String(topic) == topic_command) {
        String command = doc["command"];
        if (command == "status") {
            publishStatus();
        }
    }
    else if (String(topic) == topic_relay1_control) {
        String action = doc["action"];
        if (action == "ON") {
            activateRelay(1, true);
        } else if (action == "OFF") {
            activateRelay(1, false);
        } else if (action == "RESET") {
            resetTimer(1);
        }
    }
    else if (String(topic) == topic_relay2_control) {
        String action = doc["action"];
        if (action == "ON") {
            activateRelay(2, true);
        } else if (action == "OFF") {
            activateRelay(2, false);
        } else if (action == "RESET") {
            resetTimer(2);
        }
    }
}

void updateLED(unsigned long currentMillis) {
    // ตรวจสอบสถานะการเตือนของ relay ทั้งสองตัว
    bool isEmergencyWarning = false;
    bool isWarning = false;
    
    if (relay1Active) {
        unsigned long remaining1 = COUNTDOWN_TIME - (currentMillis - relay1StartTime);
        if (remaining1 <= URGENT_TIME) {
            isEmergencyWarning = true;
        } else if (remaining1 <= WARNING_TIME) {
            isWarning = true;
        }
    }
    
    if (relay2Active && !isEmergencyWarning) {
        unsigned long remaining2 = COUNTDOWN_TIME - (currentMillis - relay2StartTime);
        if (remaining2 <= URGENT_TIME) {
            isEmergencyWarning = true;
        } else if (remaining2 <= WARNING_TIME) {
            isWarning = true;
        }
    }
    
    // จัดลำดับความสำคัญของการแสดงผล LED
    if (isEmergencyWarning) {
        // กะพริบเร็วมากในช่วง 30 วินาทีสุดท้าย
        digitalWrite(LED_PIN, (currentMillis % EMERGENCY_BLINK < EMERGENCY_BLINK/2) ? HIGH : LOW);
    }
    else if (isWarning) {
        // กะพริบเร็วในช่วง 3 นาทีสุดท้าย
        digitalWrite(LED_PIN, (currentMillis % WARNING_BLINK < WARNING_BLINK/2) ? HIGH : LOW);
    }
    else {
        // แสดงสถานะ WiFi ตามปกติ
        switch (connectionStatus) {
            case 0:  // No WiFi - Fast blink
                digitalWrite(LED_PIN, (currentMillis % WIFI_FAST_BLINK < WIFI_FAST_BLINK/2) ? HIGH : LOW);
                break;
            
            case 1:  // WiFi connected - LED off
                digitalWrite(LED_PIN, LOW);
                break;
            
            case 2:  // WiFi + Internet - Slow blink
                digitalWrite(LED_PIN, (currentMillis % WIFI_SLOW_BLINK < WIFI_SLOW_BLINK/2) ? HIGH : LOW);
                break;
            
            case 3:  // WiFi but no Internet - LED on
                digitalWrite(LED_PIN, HIGH);
                break;
        }
    }
}

void activateRelay(int relay, bool state) {
    if (relay == 1) {
        if (state && !relay1Active) {
            relay1Active = true;
            relay1StartTime = millis();
            digitalWrite(RELAY1_PIN, HIGH);
            Serial.println("Relay 1 activated via MQTT");
        } else if (!state) {
            relay1Active = false;
            digitalWrite(RELAY1_PIN, LOW);
            Serial.println("Relay 1 deactivated via MQTT");
        }
    } else if (relay == 2) {
        if (state && !relay2Active) {
            relay2Active = true;
            relay2StartTime = millis();
            digitalWrite(RELAY2_PIN, HIGH);
            Serial.println("Relay 2 activated via MQTT");
        } else if (!state) {
            relay2Active = false;
            digitalWrite(RELAY2_PIN, LOW);
            Serial.println("Relay 2 deactivated via MQTT");
        }
    }
    publishStatus();
}

void resetTimer(int relay) {
    if (relay == 1 && relay1Active) {
        relay1StartTime = millis();
        Serial.println("Relay 1 timer reset via MQTT");
    } else if (relay == 2 && relay2Active) {
        relay2StartTime = millis();
        Serial.println("Relay 2 timer reset via MQTT");
    }
    publishStatus();
}

void handleRelay1(bool switchPressed) {
    if (switchPressed) {
        if (!relay1Active) {  // ถ้ารีเลย์ปิดอยู่
            relay1Active = true;
            relay1StartTime = millis();
            digitalWrite(RELAY1_PIN, HIGH);
            Serial.println("Relay 1 activated - Timer started (40 minutes)");
        } else if (relay1Active && ((millis() - relay1StartTime) >= (COUNTDOWN_TIME - WARNING_TIME))) {
            // รีเซ็ตเวลาถ้าอยู่ในช่วง 3 นาทีสุดท้าย
            relay1StartTime = millis();
            Serial.println("Relay 1 timer reset - New 40 minutes countdown started");
        }
        publishStatus();
    }
}

void handleRelay2(bool switchPressed) {
    if (switchPressed) {
        if (!relay2Active) {  // ถ้ารีเลย์ปิดอยู่
            relay2Active = true;
            relay2StartTime = millis();
            digitalWrite(RELAY2_PIN, HIGH);
            Serial.println("Relay 2 activated - Timer started (40 minutes)");
        } else if (relay2Active && ((millis() - relay2StartTime) >= (COUNTDOWN_TIME - WARNING_TIME))) {
            // รีเซ็ตเวลาถ้าอยู่ในช่วง 3 นาทีสุดท้าย
            relay2StartTime = millis();
            Serial.println("Relay 2 timer reset - New 40 minutes countdown started");
        }
        publishStatus();
    }
}

void publishStatus() {
    StaticJsonDocument<400> doc;
    
    doc["device_id"] = device_id;
    doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi_rssi"] = WiFi.RSSI();
    
    JsonObject relay1 = doc.createNestedObject("relay1");
    relay1["active"] = relay1Active;
    if (relay1Active) {
        unsigned long remaining = (relay1StartTime + COUNTDOWN_TIME - millis()) / 1000;
        relay1["remaining"] = remaining; // **แก้ไข field name เป็น "remaining"** 
    }

    JsonObject relay2 = doc.createNestedObject("relay2");
    relay2["active"] = relay2Active;
    if (relay2Active) {
        unsigned long remaining = (relay2StartTime + COUNTDOWN_TIME - millis()) / 1000;
        relay2["remaining"] = remaining; // **แก้ไข field name เป็น "remaining"**
    }

    char buffer[400];
    serializeJson(doc, buffer);
    mqtt.publish(topic_status, buffer, true);  // Retain message
}

bool reconnectMQTT() {
    if (mqtt.connect(device_id, mqtt_user, mqtt_password)) {
        Serial.println("Connected to MQTT Broker");
        
        // Subscribe to topics
        mqtt.subscribe(topic_relay1_control);
        mqtt.subscribe(topic_relay2_control);
        mqtt.subscribe(topic_command);
        
        // Publish initial status
        publishStatus();
        return true;
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting setup...");
    
    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(RELAY1_PIN, OUTPUT);
    pinMode(RELAY2_PIN, OUTPUT);
    pinMode(SWITCH1_PIN, INPUT_PULLUP);
    pinMode(SWITCH2_PIN, INPUT_PULLUP);
    
    // Set initial states
    digitalWrite(RELAY1_PIN, LOW);
    digitalWrite(RELAY2_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    
    // Setup MQTT
    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setCallback(mqttCallback);
    
    WiFi.begin(ssid, password);
    Serial.println("Setup complete.");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // MQTT Connection management
    if (!mqtt.connected()) {
        if (currentMillis - lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL) {
            lastMqttReconnectAttempt = currentMillis;
            if (reconnectMQTT()) {
                lastMqttReconnectAttempt = 0;
            }
        }
    } else {
        mqtt.loop();
    }

    // อ่านค่าสวิตช์
    bool switch1Reading = digitalRead(SWITCH1_PIN);
    bool switch2Reading = digitalRead(SWITCH2_PIN);

    // จัดการสวิตช์ 1
    if (switch1Reading != switch1LastState) {
        lastDebounceTime1 = currentMillis;
    }

    if ((currentMillis - lastDebounceTime1) > DEBOUNCE_DELAY) {
        // เมื่อมีการกดสวิตช์ (เปลี่ยนจาก HIGH เป็น LOW)
        if (switch1Reading == LOW && !switch1LongPress) {
            // ถ้าเป็นการกดครั้งแรก
            if (switch1PressStart == 0) {
   //             switch1P
                switch1PressStart = currentMillis;
            }
            
            // ตรวจสอบการกดค้าง
            if ((currentMillis - switch1PressStart) >= LONG_PRESS_TIME) {
                if (relay1Active) {
                    relay1Active = false;
                    digitalWrite(RELAY1_PIN, LOW);
                    switch1LongPress = true;
                    Serial.println("Long press detected - Relay 1 forced OFF");
                    publishStatus();
                }
            }
        }
        // เมื่อปล่อยสวิตช์
        else if (switch1Reading == HIGH) {
            // ถ้าไม่ใช่การกดค้าง และมีการกดมาก่อน
            if (!switch1LongPress && switch1PressStart > 0) {
                handleRelay1(true);
            }
            // รีเซ็ตตัวแปร
            switch1PressStart = 0;
            switch1LongPress = false;
        }
    }
    switch1LastState = switch1Reading;

    // จัดการสวิตช์ 2
    if (switch2Reading != switch2LastState) {
        lastDebounceTime2 = currentMillis;
    }

    if ((currentMillis - lastDebounceTime2) > DEBOUNCE_DELAY) {
        // เมื่อมีการกดสวิตช์ (เปลี่ยนจาก HIGH เป็น LOW)
        if (switch2Reading == LOW && !switch2LongPress) {
            // ถ้าเป็นการกดครั้งแรก
            if (switch2PressStart == 0) {
                switch2PressStart = currentMillis;
            }
            
            // ตรวจสอบการกดค้าง
            if ((currentMillis - switch2PressStart) >= LONG_PRESS_TIME) {
                if (relay2Active) {
                    relay2Active = false;
                    digitalWrite(RELAY2_PIN, LOW);
                    switch2LongPress = true;
                    Serial.println("Long press detected - Relay 2 forced OFF");
                    publishStatus();
                }
            }
        }
        // เมื่อปล่อยสวิตช์
        else if (switch2Reading == HIGH) {
            // ถ้าไม่ใช่การกดค้าง และมีการกดมาก่อน
            if (!switch2LongPress && switch2PressStart > 0) {
                handleRelay2(true);
            }
            // รีเซ็ตตัวแปร
            switch2PressStart = 0;
            switch2LongPress = false;
        }
    }
    switch2LastState = switch2Reading;
    
    // ตรวจสอบและอัพเดทสถานะ relay 1
    if (relay1Active) {
        unsigned long relay1Elapsed = currentMillis - relay1StartTime;
        if (relay1Elapsed >= COUNTDOWN_TIME) {
            relay1Active = false;
            digitalWrite(RELAY1_PIN, LOW);
            Serial.println("Relay 1 timer completed - turned OFF");
            publishStatus();
        }
    }
    
    // ตรวจสอบและอัพเดทสถานะ relay 2
    if (relay2Active) {
        unsigned long relay2Elapsed = currentMillis - relay2StartTime;
        if (relay2Elapsed >= COUNTDOWN_TIME) {
            relay2Active = false;
            digitalWrite(RELAY2_PIN, LOW);
            Serial.println("Relay 2 timer completed - turned OFF");
            publishStatus();
        }
    }
    
    // อัพเดทสถานะ LED
    updateLED(currentMillis);
    
    // Status update every CHECK_INTERVAL
    if (currentMillis - previousMillis >= CHECK_INTERVAL) {
        previousMillis = currentMillis;
        
        // Check WiFi and Internet status
        if (WiFi.status() != WL_CONNECTED) {
            connectionStatus = 0;  // No WiFi
        } else {
            if (checkInternet()) {
                connectionStatus = 2;  // WiFi + Internet
            } else {
                connectionStatus = 1;  // WiFi but no Internet
            }
        }

        // Print status report
        Serial.println("\n=== Status Report ===");
        Serial.print("Time: ");
        Serial.print(currentMillis / 1000);
        Serial.println(" seconds");
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi: Connected");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            Serial.print("Signal Strength (RSSI): ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            
            if (connectionStatus == 2) {
                Serial.println("Internet: Connected");
            } else {
                Serial.println("Internet: Not Connected");
            }
        } else {
            Serial.println("WiFi: Not Connected");
        }

        // Print relay status
        if (relay1Active) {
            unsigned long remaining = (relay1StartTime + COUNTDOWN_TIME - currentMillis) / 1000;
            Serial.print("Relay 1: ON, Time remaining: ");
            Serial.print(remaining / 60);
            Serial.print(":");
            Serial.println(remaining % 60);
        } else {
            Serial.println("Relay 1: OFF");
        }
        
        if (relay2Active) {
            unsigned long remaining = (relay2StartTime + COUNTDOWN_TIME - currentMillis) / 1000;
            Serial.print("Relay 2: ON, Time remaining: ");
            Serial.print(remaining / 60);
            Serial.print(":");
            Serial.println(remaining % 60);
        } else {
            Serial.println("Relay 2: OFF");
        }
        Serial.println("===================");
        
        // Publish status update
        publishStatus();
    }
}
