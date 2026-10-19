/*
 * =========================================================================
 * ESP32 Complete IoT Controller - Supabase Cloud (Fixed Color Overwrite)
 * 4-Channel Relays + DHT Temp/Humidity + TCS34725 Color Sensor
 * =========================================================================
 * 
 * 1. Relay 4 ช่อง (Active LOW แบบ Open-Drain ปลอดภัย ตัดวงจร 100%):
 *    - IN 1 : GPIO 16 (สวิตช์ 1)
 *    - IN 2 : GPIO 17 (สวิตช์ 2)
 *    - IN 3 : GPIO 18 (สวิตช์ 3)
 *    - IN 4 : GPIO 19 (สวิตช์ 4)
 *    - VCC  : 3.3V หรือ 5V (VIN)
 *    - GND  : GND ของ ESP32
 * 
 * 2. เซนเซอร์วัดอุณหภูมิและความชื้น (DHT11 / DHT22):
 *    - ขา + (VCC) : 3.3V
 *    - ขา - (GND) : GND
 *    - ขา OUT     : GPIO 4
 * 
 * 3. เซนเซอร์ตรวจจับสี (TCS34725 I2C Color Sensor):
 *    - ขา VIN / VCC : 3.3V
 *    - ขา GND       : GND
 *    - ขา SDA       : GPIO 21
 *    - ขา SCL       : GPIO 22
 *    - ขา LED       : ต่อ 3.3V เพื่อเปิดไฟส่องวัตถุ (สำคัญมากสำหรับการสะท้อนสี)
 * 
 * =========================================================================
 * การแก้ไข: 
 * - เพิ่ม Auto-Increment 0xA0 ในการอ่าน TCS34725 เพื่อให้อ่านค่าสี RGB ครบทุกช่อง
 * - เพิ่ม Delay ตาม Datasheet (PON 3ms, AEN 60ms)
 * - หากยังไม่ได้ต่อเซนเซอร์สี หรือแสงไม่พอ (c <= 10) จะ "ไม่ส่งค่าสีดำ #000000 ไปทับ" 
 *   ทำให้หน้าเว็บไม่ถูกเปลี่ยนเป็นสีดำ!
 * =========================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>

// --- ตั้งค่าเซนเซอร์ DHT ---
const bool IS_DHT22 = false; // ตัวสีฟ้า (DHT11) = false, ตัวสีขาว (DHT22) = true
const int DHT_PIN = 4;       // ขาต่อเซนเซอร์อุณหภูมิ (GPIO 4)

// --- ขารีเลย์ 4 ช่อง (16, 17, 18, 19) ---
const int relayPins[4] = {16, 17, 18, 19};
bool relayStates[4] = {false, false, false, false};

// --- การตั้งค่า WiFi ---
const char* ssid = "Earth_Aoey_Aey";
const char* password = "0894565410";

// --- การตั้งค่า Supabase Cloud ---
const char* supabaseUrl = "https://igiihteeeprpcxxlldkd.supabase.co";
const char* supabaseAnonKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImlnaWlodGVlZXBycGN4eGxsZGtkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODQ5ODkwNzksImV4cCI6MjEwMDU2NTA3OX0.fr8_ZAYKQ3D-JgEtAWGJnNvKjoUmYxs1T7tjzzsEltw";

// ตัวแปรช่วงเวลาการทำงาน
unsigned long lastRelayCheck = 0;
const unsigned long relayInterval = 1000;      // ตรวจสอบสวิตช์ทุก 1 วินาที

unsigned long lastTelemetryCheck = 0;
const unsigned long telemetryInterval = 4000;  // ส่งค่าเซนเซอร์ขึ้น Cloud ทุก 4 วินาที

// ตัวแปรสถานะเซนเซอร์สี TCS34725 (I2C Addr: 0x29)
#define TCS34725_ADDR 0x29
bool tcsFound = false;

// ฟังก์ชันสั่งงานรีเลย์ Active LOW (Open-Drain)
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

// ฟังก์ชันอ่านค่า Boolean จาก JSON
bool parseJsonBool(const String& json, const String& key) {
  int keyIndex = json.indexOf("\"" + key + "\":");
  if (keyIndex == -1) return false;
  String val = json.substring(keyIndex + key.length() + 3, keyIndex + key.length() + 9);
  val.trim();
  return val.startsWith("true") || val.startsWith("1");
}

// -------------------------------------------------------------
// สแกนหาอุปกรณ์ I2C บนขา 21 (SDA) และ 22 (SCL)
// -------------------------------------------------------------
void scanI2C() {
  Serial.println("\n[I2C Scanner] กำลังค้นหาอุปกรณ์บนขา SDA=21, SCL=22...");
  int nDevices = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf(">>> [I2C] พบอุปกรณ์ที่ Address: 0x%02X\n", address);
      nDevices++;
    }
  }
  if (nDevices == 0) {
    Serial.println(">>> [I2C] ไม่พบอุปกรณ์ I2C (ตรวจสอบสาย 3.3V, GND, SDA=21, SCL=22)");
  }
}

// -------------------------------------------------------------
// ไดรเวอร์เริ่มต้นเซนเซอร์สี TCS34725
// -------------------------------------------------------------
bool initTCS34725() {
  Wire.begin(21, 22); // SDA = GPIO 21, SCL = GPIO 22
  Wire.setClock(100000);

  // เช็คว่ามีอุปกรณ์ที่ I2C Address 0x29 หรือไม่
  Wire.beginTransmission(TCS34725_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  // อ่าน ID register (0x80 | 0x12)
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x12);
  Wire.endTransmission();
  Wire.requestFrom(TCS34725_ADDR, 1);
  if (Wire.available()) {
    uint8_t id = Wire.read();
    Serial.printf("[TCS34725] พบชิปเซนเซอร์สี ID: 0x%02X\n", id);
  }

  // 1. ตั้งค่าเวลา Integration Time (0xEB = 50ms)
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x01); // ATIME
  Wire.write(0xEB);
  Wire.endTransmission();

  // 2. ตั้งค่า Gain (0x01 = 4x Gain)
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x0F); // CONTROL
  Wire.write(0x01);
  Wire.endTransmission();

  // 3. สั่งเปิด Power ON (PON 0x01)
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x00); // ENABLE
  Wire.write(0x01);        // PON
  Wire.endTransmission();
  delay(3);                // รอ 3ms ตาม Datasheet

  // 4. สั่งเปิด RGBC Enable (AEN 0x02)
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x00); // ENABLE
  Wire.write(0x03);        // PON | AEN
  Wire.endTransmission();
  delay(60);               // รอให้ ADC แปลงสัญญาณรอบแรกเสร็จสมบูรณ์

  return true;
}

// -------------------------------------------------------------
// อ่านค่าสีจาก TCS34725
// -------------------------------------------------------------
bool readColorSensor(int &outR, int &outG, int &outB, int &outC, String &outHex) {
  // สั่งอ่าน Multi-byte ด้วย Auto-Increment (0xA0 = 0x80 | 0x20)
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0xA0 | 0x14); // CDATAL register with auto-increment
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom(TCS34725_ADDR, 8);
  if (Wire.available() < 8) return false;

  uint16_t c = Wire.read() | (Wire.read() << 8);
  uint16_t r = Wire.read() | (Wire.read() << 8);
  uint16_t g = Wire.read() | (Wire.read() << 8);
  uint16_t b = Wire.read() | (Wire.read() << 8);

  outC = c;

  // หากไม่มีแสง หรือไม่ได้ต่อเซนเซอร์ (c <= 10) จะไม่ถือว่าได้สีที่ถูกต้อง
  if (c <= 10) {
    return false;
  }

  // คำนวณ RGB 0-255 จากสัดส่วนแสง
  float rf = ((float)r / (float)c) * 255.0;
  float gf = ((float)g / (float)c) * 255.0;
  float bf = ((float)b / (float)c) * 255.0;

  outR = constrain((int)rf, 0, 255);
  outG = constrain((int)gf, 0, 255);
  outB = constrain((int)bf, 0, 255);

  char hexBuf[10];
  snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", outR, outG, outB);
  outHex = String(hexBuf);

  return true;
}

// -------------------------------------------------------------
// ไดรเวอร์อ่านเซนเซอร์ DHT11 / DHT22 (ในตัว ไม่ต้องลง Library)
// -------------------------------------------------------------
bool readDHTSensor(int pin, float &temp, float &hum, bool is22) {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(20);

  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(35);

  noInterrupts();

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

  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(pin) == LOW) {
      if (micros() - timeout > 90) { interrupts(); return false; }
    }
    unsigned long startHigh = micros();
    while (digitalRead(pin) == HIGH) {
      if (micros() - startHigh > 110) { interrupts(); return false; }
    }
    if ((micros() - startHigh) > 40) {
      data[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  interrupts();

  if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    return false;
  }

  if (is22) {
    hum = ((data[0] << 8) | data[1]) * 0.1;
    int16_t rawT = ((data[2] & 0x7F) << 8) | data[3];
    temp = rawT * 0.1;
    if (data[2] & 0x80) temp = -temp;
  } else {
    hum = (float)data[0] + (float)data[1] * 0.1;
    temp = (float)data[2] + (float)data[3] * 0.1;
  }

  return true;
}

// -------------------------------------------------------------
// ดึงสถานะสวิตช์รีเลย์จาก Supabase
// -------------------------------------------------------------
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
    https.addHeader("Accept-Profile", "public");

    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      
      bool r1 = parseJsonBool(payload, "relay1");
      bool r2 = parseJsonBool(payload, "relay2");
      bool r3 = parseJsonBool(payload, "relay3");
      bool r4 = parseJsonBool(payload, "relay4");

      if (r1 != relayStates[0]) setRelay(0, r1);
      if (r2 != relayStates[1]) setRelay(1, r2);
      if (r3 != relayStates[2]) setRelay(2, r3);
      if (r4 != relayStates[3]) setRelay(3, r4);
    }
    https.end();
  }
}

// -------------------------------------------------------------
// ส่งค่า Telemetry ขึ้น Supabase (ส่งสีเฉพาะเมื่ออ่านได้จริง ไม่ส่งสีดำไปทับ!)
// -------------------------------------------------------------
void sendTelemetryToSupabase(float temp, float hum, bool hasColor, int r, int g, int b, int c, const String& hex) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Content-Profile", "public");
    https.addHeader("Accept-Profile", "public");

    String jsonBody = "{";
    bool hasField = false;

    // ส่งค่าอุณหภูมิและความชื้นถ้าอ่านได้
    if (temp > 0.0 || hum > 0.0) {
      jsonBody += "\"temperature\":" + String(temp, 1) + ",\"humidity\":" + String(hum, 1);
      hasField = true;
    }

    // *** จุดสำคัญ: ส่งสีเฉพาะเมื่ออ่านค่าแสงได้จริง (hasColor && c > 10) เท่านั้น ***
    // จะไม่ส่งสีดำ #000000 ไปทับเมื่อเซนเซอร์สียังไม่ได้เสียบ
    if (hasColor && c > 10) {
      if (hasField) jsonBody += ",";
      jsonBody += "\"color_r\":" + String(r) + 
                  ",\"color_g\":" + String(g) + 
                  ",\"color_b\":" + String(b) + 
                  ",\"color_c\":" + String(c) + 
                  ",\"color_hex\":\"" + hex + "\"";
      hasField = true;
    }

    jsonBody += "}";

    if (hasField) {
      int httpCode = https.PATCH(jsonBody);
      if (httpCode >= 200 && httpCode < 300) {
        if (hasColor && c > 10) {
          Serial.printf("[Cloud Sync] อุณหภูมิ: %.1f°C | สี: %s (R:%d G:%d B:%d C:%d) สำเร็จ\n", 
                        temp, hex.c_str(), r, g, b, c);
        } else {
          Serial.printf("[Cloud Sync] อุณหภูมิ: %.1f°C, ความชื้น: %.1f%% สำเร็จ\n", temp, hum);
        }
      } else {
        Serial.printf("[Cloud Sync] อัปเดตไม่สำเร็จ HTTP: %d\n", httpCode);
      }
    }

    https.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=======================================================");
  Serial.println(" ESP32 Full IoT Controller (Relays + DHT + TCS34725) ");
  Serial.println(" ขารีเลย์: 16 (CH1), 17 (CH2), 18 (CH3), 19 (CH4) ");
  Serial.println(" ขา DHT อุณหภูมิ: GPIO 4 ");
  Serial.println(" ขา เซนเซอร์สี: SDA=GPIO 21, SCL=GPIO 22 ");
  Serial.println("=======================================================");

  // สั่งปิดรีเลย์ทุกช่องไว้ก่อน
  for (int i = 0; i < 4; i++) {
    setRelay(i, false);
  }

  // ตรวจหาสาย I2C
  scanI2C();

  // เริ่มต้นเซนเซอร์ตรวจจับสี TCS34725
  tcsFound = initTCS34725();
  if (tcsFound) {
    Serial.println("✅ ตรวจพบเซนเซอร์ตรวจจับสี TCS34725 พร้อมทำงาน!");
  } else {
    Serial.println("⚠️ ยังไม่พบเซนเซอร์สี TCS34725 (เช็คสาย SDA=21, SCL=22, 3.3V, GND)");
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
  Serial.println("ระบบพร้อมทำงาน 100%!");
}

void loop() {
  unsigned long now = millis();

  // 1. ตรวจสอบสวิตช์รีเลย์จาก Cloud ทุก 1 วินาที
  if (now - lastRelayCheck >= relayInterval) {
    lastRelayCheck = now;
    checkSwitchesFromSupabase();
  }

  // 2. อ่านค่าเซนเซอร์และส่งขึ้น Supabase Cloud ทุก 4 วินาที
  if (now - lastTelemetryCheck >= telemetryInterval) {
    lastTelemetryCheck = now;

    // อ่านอุณหภูมิและความชื้น (DHT)
    float temperature = 0.0;
    float humidity = 0.0;
    bool dhtOk = readDHTSensor(DHT_PIN, temperature, humidity, IS_DHT22);

    // อ่านค่าสี (TCS34725)
    int r = 0, g = 0, b = 0, c = 0;
    String hex = "";
    bool hasColor = false;

    // สแกน I2C อัตโนมัติหากยังไม่เจอ
    if (!tcsFound) {
      tcsFound = initTCS34725();
    }

    if (tcsFound) {
      hasColor = readColorSensor(r, g, b, c, hex);
    }

    // ส่งข้อมูลขึ้น Supabase (ถ้ายังไม่มีสีจริง จะไม่ส่งสีดำไปทับ)
    sendTelemetryToSupabase(temperature, humidity, hasColor, r, g, b, c, hex);
  }

  // ป้องกัน WiFi หลุดแล้วไม่ต่อใหม่
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}
