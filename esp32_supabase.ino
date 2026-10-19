/*
 * =========================================================================
 * ESP32 Simple Switch Controller - GPIO 16 Only
 * ควบคุมสวิตช์เปิด-ปิดที่ขา 16 ผ่าน Supabase Cloud
 * (ตัดส่วนเซนเซอร์ DHT22 และเซนเซอร์สีออกทั้งหมดตามต้องการ)
 * =========================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// 1. กำหนดขา Relay (ใช้เฉพาะขา 16)
#define RELAY_PIN 16
bool currentRelayState = false;

// ฟังก์ชันสั่งงานรีเลย์ Active LOW (Open-Drain ตัดวงจร ป้องกันไฟค้าง)
void setRelay(bool state) {
  currentRelayState = state;
  if (state) {
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);   // สั่งเปิดไฟ (ON)
    Serial.println(">>> [Relay ขา 16] -> เปิด (ON)");
  } else {
    pinMode(RELAY_PIN, INPUT);      // สั่งปิดไฟ (OFF ปล่อยขาลอย)
    Serial.println(">>> [Relay ขา 16] -> ปิด (OFF)");
  }
}

// 2. การตั้งค่า WiFi
const char* ssid = "Earth_Aoey_Aey";
const char* password = "0894565410";

// 3. การตั้งค่า Supabase Cloud
const char* supabaseUrl = "https://igiihteeeprpcxxlldkd.supabase.co";
const char* supabaseAnonKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImlnaWlodGVlZXBycGN4eGxsZGtkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODQ5ODkwNzksImV4cCI6MjEwMDU2NTA3OX0.fr8_ZAYKQ3D-JgEtAWGJnNvKjoUmYxs1T7tjzzsEltw";

unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 1000; // ตรวจสอบคำสั่งสวิตช์ทุก 1 วินาที

// ฟังก์ชันเช็คสถานะ relay1 จาก Supabase
void checkSwitchFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1&select=relay1";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Accept", "application/json");

    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      
      // ตรวจสอบว่าใน payload มีคำว่า true หรือไม่
      int idx = payload.indexOf("\"relay1\":");
      if (idx != -1) {
        String val = payload.substring(idx + 9, idx + 15);
        val.trim();
        bool targetState = val.startsWith("true") || val.startsWith("1");
        
        if (targetState != currentRelayState) {
          setRelay(targetState);
        }
      }
    } else {
      Serial.printf("[Cloud] ตรวจสอบสถานะไม่สำเร็จ: HTTP %d\n", httpCode);
    }
    https.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- เริ่มต้นระบบสวิตช์ขา 16 (ESP32) ---");

  // สั่งปิดไฟไว้ก่อนตอนเริ่ม
  setRelay(false);

  // เชื่อมต่อ WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected successfully!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("พร้อมรับคำสั่งเปิด-ปิดขา 16 จาก Supabase แล้ว!");
}

void loop() {
  unsigned long now = millis();

  // ตรวจสอบสถานะจาก Supabase ทุก 1 วินาที
  if (now - lastCheckTime >= checkInterval) {
    lastCheckTime = now;
    checkSwitchFromSupabase();
  }

  // ป้องกัน WiFi หลุดแล้วไม่ต่อใหม่
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}
