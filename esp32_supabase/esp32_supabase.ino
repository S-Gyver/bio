/*
 * =========================================================================
 * ESP32 4-Channel Relay Controller - Supabase Cloud (GPIO 16, 17, 18, 19)
 * ควบคุมสวิตช์รีเลย์ 4 ช่องอิสระผ่าน Supabase Cloud & Web Dashboard
 * =========================================================================
 * การต่อสาย:
 * - IN 1 : GPIO 16 (สวิตช์ 1)
 * - IN 2 : GPIO 17 (สวิตช์ 2)
 * - IN 3 : GPIO 18 (สวิตช์ 3)
 * - IN 4 : GPIO 19 (สวิตช์ 4)
 * - VCC  : 3.3V หรือ 5V (VIN)
 * - GND  : GND
 * =========================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// 1. กำหนดขา Relay 4 ช่อง (16, 17, 18, 19)
const int relayPins[4] = {16, 17, 18, 19};
bool relayStates[4] = {false, false, false, false};

// ฟังก์ชันสั่งงานรีเลย์ Active LOW (Open-Drain ตัดวงจร 100% ป้องกันไฟค้าง)
void setRelay(int index, bool state) {
  if (index < 0 || index >= 4) return;
  relayStates[index] = state;
  int pin = relayPins[index];

  if (state) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);   // สั่งเปิดไฟ (ON)
    Serial.printf(">>> [Relay %d (ขา %d)] -> เปิด (ON)\n", index + 1, pin);
  } else {
    pinMode(pin, INPUT);      // สั่งปิดไฟ (OFF ปล่อยขาลอย)
    Serial.printf(">>> [Relay %d (ขา %d)] -> ปิด (OFF)\n", index + 1, pin);
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

// ฟังก์ชันแกะค่า Boolean จาก JSON โดยไม่ต้องลงไลบรารีเพิ่ม
bool parseJsonBool(const String& json, const String& key) {
  int keyIndex = json.indexOf("\"" + key + "\":");
  if (keyIndex == -1) return false;
  
  String val = json.substring(keyIndex + key.length() + 3, keyIndex + key.length() + 9);
  val.trim();
  return val.startsWith("true") || val.startsWith("1");
}

// ฟังก์ชันดึงสถานะรีเลย์ทั้ง 4 ช่องจาก Supabase
void checkSwitchesFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1&select=relay1,relay2,relay3,relay4";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Accept", "application/json");
    https.addHeader("Accept-Profile", "public"); // บังคับ schema public ป้องกัน 404

    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      
      bool r1 = parseJsonBool(payload, "relay1");
      bool r2 = parseJsonBool(payload, "relay2");
      bool r3 = parseJsonBool(payload, "relay3");
      bool r4 = parseJsonBool(payload, "relay4");

      // อัปเดตสถานะรีเลย์ทีละช่องเมื่อมีการกดเปิด-ปิดจากหน้าเว็บ
      if (r1 != relayStates[0]) setRelay(0, r1);
      if (r2 != relayStates[1]) setRelay(1, r2);
      if (r3 != relayStates[2]) setRelay(2, r3);
      if (r4 != relayStates[3]) setRelay(3, r4);
    } else {
      Serial.printf("[Cloud] ตรวจสอบสถานะไม่สำเร็จ: HTTP %d\n", httpCode);
    }
    https.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=============================================");
  Serial.println(" ESP32 4-Channel Relay Controller (Cloud) ");
  Serial.println(" ขา 16 (CH1), ขา 17 (CH2), ขา 18 (CH3), ขา 19 (CH4) ");
  Serial.println("=============================================");

  // สั่งปิดรีเลย์ทุกช่องไว้ก่อนเมื่อเริ่มต้น
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
  Serial.println("พร้อมรับคำสั่งเปิด-ปิดทั้ง 4 ช่องจาก Dashboard แล้ว!");
}

void loop() {
  unsigned long now = millis();

  // ตรวจสอบสถานะสวิตช์ทั้ง 4 ช่องจาก Supabase ทุก 1 วินาที
  if (now - lastCheckTime >= checkInterval) {
    lastCheckTime = now;
    checkSwitchesFromSupabase();
  }

  // ป้องกัน WiFi หลุดแล้วไม่ต่อใหม่
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}
