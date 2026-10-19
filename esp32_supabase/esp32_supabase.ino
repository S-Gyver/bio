/*
 * =========================================================================
 * ESP32 4-Channel Relay & DHT Temperature/Humidity - Supabase Cloud
 * =========================================================================
 * ควบคุมสวิตช์รีเลย์ 4 ช่อง และอ่านค่าอุณหภูมิ/ความชื้น ส่งขึ้น Cloud & Web Dashboard
 * 
 * การต่อสาย:
 * 1. Relay 4 ช่อง (Active LOW แบบ Open-Drain ไม่กินไฟค้าง):
 *    - IN 1 : GPIO 16 (สวิตช์ 1)
 *    - IN 2 : GPIO 17 (สวิตช์ 2)
 *    - IN 3 : GPIO 18 (สวิตช์ 3)
 *    - IN 4 : GPIO 19 (สวิตช์ 4)
 *    - VCC  : 3.3V หรือ 5V (VIN)
 *    - GND  : GND ของ ESP32
 * 
 * 2. เซนเซอร์วัดอุณหภูมิและความชื้น (DHT11 / DHT22):
 *    - ขา + (VCC) : ขา 3.3V (หรือ 5V) ของ ESP32
 *    - ขา - (GND) : ขา GND ของ ESP32
 *    - ขา OUT/DATA: ขา GPIO 4 (G4) ของ ESP32
 * =========================================================================
 * ข้อดี: มีฟังก์ชันอ่าน DHT ในตัว ไม่ต้องติดตั้ง Library เพิ่ม (Zero Library Error)
 * =========================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// --- เลือกรุ่นเซนเซอร์ DHT ---
// ถ้าใช้ DHT11 (ตัวสีฟ้า) ให้ใส่ false
// ถ้าใช้ DHT22 (ตัวสีขาว) ให้ใส่ true
const bool IS_DHT22 = false; 
const int DHT_PIN = 4; // ขาเซนเซอร์อุณหภูมิ (GPIO 4)

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

unsigned long lastRelayCheck = 0;
const unsigned long relayInterval = 1000; // ตรวจสอบคำสั่งสวิตช์ทุก 1 วินาที

unsigned long lastTelemetryCheck = 0;
const unsigned long telemetryInterval = 5000; // ส่งอุณหภูมิ/ความชื้นขึ้น Cloud ทุก 5 วินาที

// ฟังก์ชันแกะค่า Boolean จาก JSON โดยไม่ต้องลงไลบรารีเพิ่ม
bool parseJsonBool(const String& json, const String& key) {
  int keyIndex = json.indexOf("\"" + key + "\":");
  if (keyIndex == -1) return false;
  
  String val = json.substring(keyIndex + key.length() + 3, keyIndex + key.length() + 9);
  val.trim();
  return val.startsWith("true") || val.startsWith("1");
}

// ฟังก์ชันอ่านค่าเซนเซอร์ DHT11 / DHT22 โดยตรง (ไม่ต้องพึ่งไลบรารีภายนอก)
bool readDHTSensor(int pin, float &temp, float &hum, bool is22) {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  // 1. ส่ง Start Signal (ดึง LOW 20ms)
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(20);

  // 2. ปล่อย High และรอการตอบสนอง
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(35);

  noInterrupts(); // ปิด interrupt ชั่วขณะเพื่อจับจังหวะ pulse ให้แม่นยำ

  // 3. รอสัญญาณตอบสนองจากเซนเซอร์
  unsigned long timeout = micros();
  while (digitalRead(pin) == HIGH) {
    if (micros() - timeout > 100) { interrupts(); return false; }
  }
  timeout = micros();
  while (digitalRead(pin) == LOW) {
    if (micros() - timeout > 100) { interrupts(); return false; }
  }
  timeout = micros();
  while (digitalRead(pin) == HIGH) {
    if (micros() - timeout > 100) { interrupts(); return false; }
  }

  // 4. อ่านบิตข้อมูล 40 บิต (5 ไบต์)
  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(pin) == LOW) {
      if (micros() - timeout > 90) { interrupts(); return false; }
    }
    unsigned long startHigh = micros();
    while (digitalRead(pin) == HIGH) {
      if (micros() - startHigh > 110) { interrupts(); return false; }
    }
    unsigned long duration = micros() - startHigh;
    if (duration > 40) { // ถ้ายาวกว่า 40us คือบิต '1'
      data[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  interrupts(); // คืนค่า interrupt

  // 5. ตรวจสอบ Checksum
  uint8_t sum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
  if (data[4] != sum) {
    return false;
  }

  // 6. แปลงข้อมูล
  if (is22) {
    hum = ((data[0] << 8) | data[1]) * 0.1;
    int16_t rawT = ((data[2] & 0x7F) << 8) | data[3];
    temp = rawT * 0.1;
    if (data[2] & 0x80) temp = -temp;
  } else { // DHT11
    hum = (float)data[0] + (float)data[1] * 0.1;
    temp = (float)data[2] + (float)data[3] * 0.1;
  }

  return true;
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

      // อัปเดตสถานะรีเลย์ทีละช่องหากมีการเปลี่ยนแปลง
      if (r1 != relayStates[0]) setRelay(0, r1);
      if (r2 != relayStates[1]) setRelay(1, r2);
      if (r3 != relayStates[2]) setRelay(2, r3);
      if (r4 != relayStates[3]) setRelay(3, r4);
    } else {
      Serial.printf("[Cloud] ตรวจสอบสวิตช์ไม่สำเร็จ: HTTP %d\n", httpCode);
    }
    https.end();
  }
}

// ฟังก์ชันส่งค่าอุณหภูมิและความชื้นขึ้น Supabase Cloud
void sendTelemetryToSupabase(float temp, float hum) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Content-Profile", "public"); // เจาะจง schema public ป้องกัน 404
    https.addHeader("Accept-Profile", "public");

    String jsonBody = "{\"temperature\":" + String(temp, 1) + ",\"humidity\":" + String(hum, 1) + "}";

    int httpCode = https.PATCH(jsonBody);
    if (httpCode >= 200 && httpCode < 300) {
      Serial.printf("[Sensor -> Cloud] อุณหภูมิ: %.1f °C, ความชื้น: %.1f %% (HTTP %d)\n", temp, hum, httpCode);
    } else {
      Serial.printf("[Sensor -> Cloud] อัปเดตไม่สำเร็จ: HTTP %d\n", httpCode);
    }
    https.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=======================================================");
  Serial.println(" ESP32 4-Channel Relay & DHT Sensor Controller ");
  Serial.println(" ขา Relay: 16 (CH1), 17 (CH2), 18 (CH3), 19 (CH4) ");
  Serial.println(" ขา เซนเซอร์อุณหภูมิ: GPIO 4 ");
  Serial.println("=======================================================");

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
  Serial.println("ระบบพร้อมทำงาน! รับคำสั่งสวิตช์และส่งค่าอุณหภูมิขึ้น Cloud แล้ว");
}

void loop() {
  unsigned long now = millis();

  // 1. ตรวจสอบสถานะสวิตช์ทั้ง 4 ช่องจาก Supabase ทุก 1 วินาที
  if (now - lastRelayCheck >= relayInterval) {
    lastRelayCheck = now;
    checkSwitchesFromSupabase();
  }

  // 2. อ่านค่าเซนเซอร์อุณหภูมิ/ความชื้น และส่งขึ้น Supabase ทุก 5 วินาที
  if (now - lastTelemetryCheck >= telemetryInterval) {
    lastTelemetryCheck = now;
    
    float temperature = 0.0;
    float humidity = 0.0;
    if (readDHTSensor(DHT_PIN, temperature, humidity, IS_DHT22)) {
      sendTelemetryToSupabase(temperature, humidity);
    } else {
      Serial.println("[Sensor] กำลังอ่านค่าเซนเซอร์ หรือสายยังไม่เชื่อมต่อ...");
    }
  }

  // ป้องกัน WiFi หลุดแล้วไม่ต่อใหม่
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}
