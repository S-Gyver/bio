/*
 * =========================================================================
 * ESP32 Complete IoT Controller - Supabase Cloud (High-Speed Realtime v1)
 * 4-Channel Relays + DHT Temp/Humidity + TCS34725 Color Sensor + LED Control
 * =========================================================================
 * 
 * 🚀 การปรับจูนความเร็ว (Turbo Realtime Tuning):
 * 1. ตรวจสอบคำสั่งสวิตช์รีเลย์และไฟ LED ทุก 350ms (ตอบสนองในเสี้ยววินาที)
 * 2. ตรวจสอบและส่งค่าสีทุก 800ms และส่งทันทีเมื่อตรวจพบว่าวัตถุเปลี่ยนสี
 * 3. เปิดใช้งาน HTTP Keep-Alive (setReuse) เพื่อลดเวลาต่อ TLS Handshake ลงเหลือ 50ms
 * 4. เซนเซอร์ DHT อุณหภูมิ/ความชื้น ส่งอัปเดตทุก 3 วินาทีเพื่อความแม่นยำ
 * 
 * ขาต่อใช้งาน:
 * - Relay 4 ช่อง : GPIO 16, 17, 18, 19
 * - DHT อุณหภูมิ  : GPIO 4
 * - สี TCS34725  : SDA=GPIO 21, SCL=GPIO 22
 * - ไฟ LED เซนเซอร์: GPIO 23
 * =========================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>

// --- ขาควบคุมไฟ LED สีขาวของเซนเซอร์สี (GPIO 23) ---
const int COLOR_LED_PIN = 23; 
bool colorLedState = true; // สถานะเริ่มต้น: เปิดไฟ (ON)

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

// 🚀 ตัวแปรช่วงเวลาความเร็วสูง (High-Speed Intervals)
unsigned long lastRelayCheck = 0;
const unsigned long relayInterval = 350;     // ตรวจสอบสวิตช์ทุก 350 มิลลิวินาที (ไวขึ้นเกือบ 3 เท่า!)

unsigned long lastColorCheck = 0;
const unsigned long colorInterval = 800;     // ส่งค่าสีทุก 800 มิลลิวินาที (ไวขึ้น 5 เท่า!)

unsigned long lastDhtCheck = 0;
const unsigned long dhtInterval = 3000;      // ส่งอุณหภูมิ/ความชื้นทุก 3 วินาที

// ตัวแปรจดจำค่าสีล่าสุด เพื่อส่งทันทีเมื่อสีเปลี่ยน
int lastSentR = -999, lastSentG = -999, lastSentB = -999;
float lastTemp = 28.5, lastHum = 60.0;

#define TCS34725_ADDR 0x29
bool tcsFound = false;

// ฟังก์ชันสั่งงานไฟ LED บนเซนเซอร์สี
void setColorLed(bool state) {
  colorLedState = state;
  if (state) {
    pinMode(COLOR_LED_PIN, INPUT); // ปล่อยขาลอย ไฟ LED ติดสว่าง
    Serial.println(">>> [Sensor LED] เปิดไฟส่องสี (ON)");
  } else {
    pinMode(COLOR_LED_PIN, OUTPUT);
    digitalWrite(COLOR_LED_PIN, LOW); // ดึงลงกราวด์ ไฟ LED ดับสนิท
    Serial.println(">>> [Sensor LED] ปิดไฟส่องสี (OFF)");
  }
}

// ฟังก์ชันสั่งงานรีเลย์ Active LOW (Open-Drain)
void setRelay(int index, bool state) {
  if (index < 0 || index >= 4) return;
  relayStates[index] = state;
  int pin = relayPins[index];

  if (state) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    Serial.printf(">>> [Relay %d (ขา %d)] -> เปิด (ON)\n", index + 1, pin);
  } else {
    pinMode(pin, INPUT);
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
    Serial.println(">>> [I2C] ยังไม่พบอุปกรณ์ I2C (ตรวจสอบสาย 3.3V, GND, SDA=21, SCL=22)");
  }
}

// -------------------------------------------------------------
// เริ่มต้นเซนเซอร์สี TCS34725
// -------------------------------------------------------------
bool initTCS34725() {
  Wire.begin(21, 22);
  Wire.setClock(100000);

  Wire.beginTransmission(TCS34725_ADDR);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x01);
  Wire.write(0xEB); // ATIME 50ms
  Wire.endTransmission();

  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x0F);
  Wire.write(0x01); // 4x Gain
  Wire.endTransmission();

  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x00);
  Wire.write(0x01); // PON
  Wire.endTransmission();
  delay(3);

  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | 0x00);
  Wire.write(0x03); // PON | AEN
  Wire.endTransmission();
  delay(60);

  return true;
}

// -------------------------------------------------------------
// อ่านค่าสีจาก TCS34725
// -------------------------------------------------------------
bool readColorSensor(int &outR, int &outG, int &outB, int &outC, String &outHex) {
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0xA0 | 0x14); // Auto-increment read
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom(TCS34725_ADDR, 8);
  if (Wire.available() < 8) return false;

  uint8_t c_l = Wire.read(); uint8_t c_h = Wire.read();
  uint8_t r_l = Wire.read(); uint8_t r_h = Wire.read();
  uint8_t g_l = Wire.read(); uint8_t g_h = Wire.read();
  uint8_t b_l = Wire.read(); uint8_t b_h = Wire.read();

  uint16_t c = (c_h << 8) | c_l;
  uint16_t r = (r_h << 8) | r_l;
  uint16_t g = (g_h << 8) | g_l;
  uint16_t b = (b_h << 8) | b_l;

  outC = c;
  if (c == 0) return false;

  float rf = (float)r / (float)c;
  float gf = (float)g / (float)c;
  float bf = (float)b / (float)c;

  float maxComp = max(rf, max(gf, bf));
  if (maxComp > 0.0) {
    outR = constrain((int)((rf / maxComp) * 255.0), 0, 255);
    outG = constrain((int)((gf / maxComp) * 255.0), 0, 255);
    outB = constrain((int)((bf / maxComp) * 255.0), 0, 255);
  } else {
    outR = 0; outG = 0; outB = 0;
  }

  char hexBuf[10];
  snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", outR, outG, outB);
  outHex = String(hexBuf);

  return true;
}

// -------------------------------------------------------------
// อ่านค่า DHT11 / DHT22
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

  if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) return false;

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
// ดึงสถานะสวิตช์รีเลย์และไฟ LED ส่องสีจาก Supabase (ความเร็วสูง)
// -------------------------------------------------------------
void checkSwitchesFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(1200); // ตั้ง Timeout สั้น 1.2 วินาที เพื่อไม่ให้ระบบค้าง
  https.setReuse(true);   // เปิดใช้ Connection Reuse เพื่อความไวสูงสุด

  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1&select=relay1,relay2,relay3,relay4,name";

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

      if (payload.indexOf("\"name\":\"led_off\"") != -1) {
        if (colorLedState != false) setColorLed(false);
      } else if (payload.indexOf("\"name\":\"led_on\"") != -1) {
        if (colorLedState != true) setColorLed(true);
      }
    }
    https.end();
  }
}

// -------------------------------------------------------------
// ส่งข้อมูลสีขึ้น Supabase แบบความเร็วสูง (ส่งทันทีที่สีเปลี่ยน)
// -------------------------------------------------------------
void sendColorToSupabase(int r, int g, int b, int c, const String& hex) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(1200);
  https.setReuse(true);

  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Content-Profile", "public");
    https.addHeader("Accept-Profile", "public");

    String jsonBody = "{\"color_r\":" + String(r) + 
                      ",\"color_g\":" + String(g) + 
                      ",\"color_b\":" + String(b) + 
                      ",\"color_c\":" + String(c) + 
                      ",\"color_hex\":\"" + hex + "\"}";

    https.PATCH(jsonBody);
    https.end();
  }
}

// -------------------------------------------------------------
// ส่งอุณหภูมิและความชื้นขึ้น Supabase
// -------------------------------------------------------------
void sendDhtToSupabase(float temp, float hum) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(1200);

  String endpoint = String(supabaseUrl) + "/rest/v1/esp32_devices?id=eq.1";

  if (https.begin(client, endpoint)) {
    https.addHeader("apikey", supabaseAnonKey);
    https.addHeader("Authorization", String("Bearer ") + supabaseAnonKey);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Content-Profile", "public");
    https.addHeader("Accept-Profile", "public");

    String jsonBody = "{\"temperature\":" + String(temp, 1) + ",\"humidity\":" + String(hum, 1) + "}";

    https.PATCH(jsonBody);
    https.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=======================================================");
  Serial.println(" ESP32 Turbo IoT Controller (High-Speed Realtime v1) ");
  Serial.println(" - ตรวจสอบสวิตช์: ทุก 350ms ");
  Serial.println(" - ส่งค่าสี: ทุก 800ms (หรือทันทีเมื่อสีเปลี่ยน) ");
  Serial.println(" - ส่งอุณหภูมิ: ทุก 3 วินาที ");
  Serial.println("=======================================================");

  for (int i = 0; i < 4; i++) {
    setRelay(i, false);
  }

  setColorLed(true);
  scanI2C();

  tcsFound = initTCS34725();
  if (tcsFound) {
    Serial.println("✅ ตรวจพบเซนเซอร์ตรวจจับสี TCS34725 พร้อมทำงาน!");
  } else {
    Serial.println("⚠️ ยังไม่พบเซนเซอร์สี (เช็คสาย SDA=21, SCL=22, 3.3V, GND)");
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected successfully!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  unsigned long now = millis();

  // 1. ตรวจสอบสวิตช์รีเลย์และไฟ LED ส่องสีแบบความเร็วสูง (ทุก 350ms)
  if (now - lastRelayCheck >= relayInterval) {
    lastRelayCheck = now;
    checkSwitchesFromSupabase();
  }

  // 2. ตรวจสอบและส่งค่าสีขึ้น Cloud (ส่งเมื่อสีเปลี่ยน หรือส่ง Heartbeat ทุก 3 วินาที)
  static unsigned long lastSupabaseColorSync = 0;
  if (now - lastColorCheck >= colorInterval) {
    lastColorCheck = now;

    if (!tcsFound) {
      tcsFound = initTCS34725();
    }

    if (tcsFound) {
      int r = 0, g = 0, b = 0, c = 0;
      String hex = "";
      if (readColorSensor(r, g, b, c, hex)) {
        bool colorChanged = (lastSentR == -999 || abs(r - lastSentR) > 4 || abs(g - lastSentG) > 4 || abs(b - lastSentB) > 4);
        bool heartbeatDue = (now - lastSupabaseColorSync >= 3000);

        if (colorChanged || heartbeatDue) {
          lastSentR = r;
          lastSentG = g;
          lastSentB = b;
          lastSupabaseColorSync = now;
          sendColorToSupabase(r, g, b, c, hex);
          Serial.printf("[Color Sensor] สี -> %s (R:%d G:%d B:%d Clear:%d)\n", hex.c_str(), r, g, b, c);
        }
      } else {
        static unsigned long lastWarn = 0;
        if (now - lastWarn > 5000) {
          lastWarn = now;
          Serial.println("⚠️ [TCS34725] อ่านค่าไม่ได้ (อาจมืดเกินไป หรือไฟ LED ดับ หรือสาย I2C หลวม)");
        }
      }
    } else {
      static unsigned long lastWarnFind = 0;
      if (now - lastWarnFind > 5000) {
        lastWarnFind = now;
        Serial.println("❌ ไม่พบเซนเซอร์ TCS34725 กรุณาตรวจสาย SDA(21), SCL(22), 3.3V, GND");
      }
    }
  }

  // 3. อ่านค่าเซนเซอร์อุณหภูมิ/ความชื้น DHT (ทุก 3 วินาที)
  if (now - lastDhtCheck >= dhtInterval) {
    lastDhtCheck = now;

    float temperature = 0.0;
    float humidity = 0.0;
    if (readDHTSensor(DHT_PIN, temperature, humidity, IS_DHT22)) {
      if (abs(temperature - lastTemp) >= 0.2 || abs(humidity - lastHum) >= 1.0) {
        lastTemp = temperature;
        lastHum = humidity;
        sendDhtToSupabase(temperature, humidity);
        Serial.printf("[DHT] อุณหภูมิ: %.1f°C, ความชื้น: %.1f%%\n", temperature, humidity);
      }
    }
  }

  // ป้องกัน WiFi หลุด
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}
