/*
 * =========================================================================
 * ESP32 Smart Controller - Supabase Cloud Controller (HTTPS / Realtime)
 * =========================================================================
 * 
 * วิธีการทำงาน:
 * 1. บอร์ดเชื่อมต่อ WiFi บ้านของคุณ ("Earth_Aoey_aey")
 * 2. บอร์ดเชื่อมต่อ HTTPS ไปยังฐานข้อมูล Supabase Cloud ของคุณโดยตรง
 * 3. เมื่อหน้าเว็บสั่งเปิด/ปิดสวิตช์ผ่าน Supabase บอร์ดจะตรวจจับและสั่งรีเลย์ทันที
 * 4. บอร์ดจะส่งค่าเซนเซอร์ขึ้นไปอัปเดตบน Supabase เพื่อแสดงผลบนหน้าเว็บออนไลน์
 * 
 * ไลบรารีที่ต้องติดตั้งใน Arduino IDE:
 * - ArduinoJson (โดย Benoit Blanchon - ติดตั้งผ่าน Library Manager)
 * =========================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// --- ตั้งค่าเซนเซอร์ DHT22 (ขา G4 / GPIO 4) ---
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- 1. การตั้งค่า WiFi ---
const char* ssid = "Earth_Aoey_Aey";
const char* password = "0894565410";

// --- 2. การตั้งค่า Supabase Cloud ---
const char* supabaseUrl = "https://igiihteeeprpcxxlldkd.supabase.co";
const char* supabaseAnonKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImlnaWlodGVlZXBycGN4eGxsZGtkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODQ5ODkwNzksImV4cCI6MjEwMDU2NTA3OX0.fr8_ZAYKQ3D-JgEtAWGJnNvKjoUmYxs1T7tjzzsEltw";

// --- 3. ขา GPIO รีเลย์ 4 ช่อง (16, 17, 18, 19) ---
const int relayPins[4] = {16, 17, 18, 19};
bool relayStates[4] = {false, false, false, false};

// ฟังก์ชันสั่งงานรีเลย์แบบ Active LOW (Open-Drain ตัดวงจร ป้องกันไฟค้าง)
void setRelay(int index, bool state) {
  if (index < 0 || index >= 4) return;
  relayStates[index] = state;
  int pin = relayPins[index];
  if (state) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);  // สั่งเปิด (ON)
  } else {
    pinMode(pin, INPUT);     // สั่งปิด (OFF ปล่อยขาลอย)
  }
}

// ช่วงเวลาอัปเดต (มิลลิวินาที)
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 1500; // ตรวจสอบคำสั่งสวิตช์ทุก 1.5 วินาที

unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 4000; // ส่งค่าเซนเซอร์ขึ้น Cloud ทุก 4 วินาที

// ฟังก์ชันดึงสถานะรีเลย์จาก Supabase
void syncRelaysFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // ไม่ต้องตรวจ SSL Fingerprint เพื่อให้เชื่อมต่อง่ายและเสถียร

  HTTPClient https;
  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1&select=relay1,relay2,relay3,relay4";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Accept", "application/json");

    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error && doc.is<JsonArray>() && doc.size() > 0) {
        JsonObject dev = doc[0];
        bool r1 = dev["relay1"] | false;
        bool r2 = dev["relay2"] | false;
        bool r3 = dev["relay3"] | false;
        bool r4 = dev["relay4"] | false;

        // หากมีการเปลี่ยนสถานะ ให้สั่งงานทันที
        if (r1 != relayStates[0]) { setRelay(0, r1); Serial.printf("[Cloud] Relay 1 -> %s\n", r1 ? "ON" : "OFF"); }
        if (r2 != relayStates[1]) { setRelay(1, r2); Serial.printf("[Cloud] Relay 2 -> %s\n", r2 ? "ON" : "OFF"); }
        if (r3 != relayStates[2]) { setRelay(2, r3); Serial.printf("[Cloud] Relay 3 -> %s\n", r3 ? "ON" : "OFF"); }
        if (r4 != relayStates[3]) { setRelay(3, r4); Serial.printf("[Cloud] Relay 4 -> %s\n", r4 ? "ON" : "OFF"); }
      }
    } else {
      Serial.printf("[Supabase] Sync Relays Error: HTTP %d\n", httpCode);
    }
    https.end();
  }
}

// ฟังก์ชันส่งค่าเซนเซอร์ขึ้นไปอัปเดตบน Supabase
void sendTelemetryToSupabase(float temp, float hum, int r, int g, int b, int c, String hexColor) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Prefer", "return=minimal");

    // สร้าง JSON ส่งขึ้นไปอัปเดต
    StaticJsonDocument<256> doc;
    doc["temperature"] = temp;
    doc["humidity"] = hum;
    doc["color_r"] = r;
    doc["color_g"] = g;
    doc["color_b"] = b;
    doc["color_c"] = c;
    doc["color_hex"] = hexColor;

    String body;
    serializeJson(doc, body);

    // ใช้ sendRequest("PATCH", ...) รองรับทุกเวอร์ชันของ ESP32
    int httpCode = https.sendRequest("PATCH", body);
    if (httpCode >= 200 && httpCode < 300) {
      Serial.printf("[Cloud] ส่งข้อมูลเซนเซอร์ขึ้น Supabase สำเร็จ (HTTP %d)\n", httpCode);
    } else {
      Serial.printf("[Cloud] ส่งข้อมูลไม่สำเร็จ: HTTP %d (โปรดตรวจดู RLS Policy ใน Supabase)\n", httpCode);
    }
    https.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- เริ่มต้น ESP32 Supabase Cloud Controller ---");

  // เริ่มต้นเซนเซอร์ DHT22
  dht.begin();
  Serial.println("[DHT22] Initialized on GPIO 4 (G4)");

  // สั่งปิดรีเลย์ทุกช่องไว้ก่อน
  for (int i = 0; i < 4; i++) {
    setRelay(i, false);
  }

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
  Serial.println("พร้อมเชื่อมต่อกับ Supabase Cloud!");
}

void loop() {
  unsigned long now = millis();

  // 1. ตรวจสอบสถานะรีเลย์จาก Supabase ทุก 1.5 วินาที
  if (now - lastFetchTime >= fetchInterval) {
    lastFetchTime = now;
    syncRelaysFromSupabase();
  }

  // 2. อ่านค่าเซนเซอร์ DHT22 จริง และส่งขึ้น Supabase ทุก 5 วินาที
  if (now - lastTelemetryTime >= telemetryInterval) {
    lastTelemetryTime = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("[DHT22] อ่านค่าไม่สำเร็จ กรุณาตรวจสายขา G4 (+ ที่ 3.3V, - ที่ GND)");
    } else {
      Serial.printf("[DHT22] ค่าจริง -> อุณหภูมิ: %.1f °C, ความชื้น: %.1f %%\n", t, h);
      sendTelemetryToSupabase(t, h, 59, 130, 246, 1200, "#3B82F6");
    }
  }

  // ป้องกัน WiFi หลุดแล้วไม่ต่อใหม่
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}
