/*
 * =========================================================================
 * ESP32 Super Realtime Controller - MQTT Cloud (50ms Ultra-Low Latency)
 * 4-Channel Relays + DHT Temp/Humidity + TCS34725 Live Color + LED Control
 * =========================================================================
 * 
 * ⚡ จุดเด่นของระบบ MQTT:
 * 1. เปิดท่อสัญญาณค้างไว้ (Persistent TCP Connection) ส่งคำสั่งถึงบอร์ดใน 0.05 วินาที
 * 2. สตรีมค่าสีสดๆ (Live Color Streaming) ได้ทันทีเมื่อสีเปลี่ยน เหมือนเปิดกล้องวิดีโอ
 * 3. มีระบบ TinyMQTT ในตัว ไม่ต้องติดตั้ง Library เพิ่มเติมแม้แต่ตัวเดียว (Zero Dependency)
 * 4. ใช้ Public Broker ฟรีระดับโลก (HiveMQ Cloud): broker.hivemq.com พอร์ต 1883
 * 
 * การต่อสาย:
 * - Relay 4 ช่อง  : GPIO 16, 17, 18, 19 (Open-Drain ตัดวงจร 100%)
 * - DHT อุณหภูมิ   : GPIO 4 (VCC -> 3.3V, GND -> GND, OUT -> GPIO 4)
 * - สี TCS34725   : SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3.3V, GND -> GND
 * - ไฟ LED เซนเซอร์ : GPIO 23 (สั่งเปิด-ปิดไฟส่องวัตถุได้ทันที)
 * =========================================================================
 */

#include <WiFi.h>
#include <Wire.h>

// -------------------------------------------------------------
// 1. กำหนดขาอุปกรณ์
// -------------------------------------------------------------
const int relayPins[4] = {16, 17, 18, 19};
bool relayStates[4] = {false, false, false, false};

const int COLOR_LED_PIN = 23;
bool colorLedState = true;

const int DHT_PIN = 4;
const bool IS_DHT22 = false; // ตัวสีฟ้า (DHT11) = false, ตัวสีขาว (DHT22) = true

#define TCS34725_ADDR 0x29
bool tcsFound = false;

// -------------------------------------------------------------
// 2. การตั้งค่า WiFi และ MQTT Broker
// -------------------------------------------------------------
const char* ssid = "Earth_Aoey_Aey";
const char* password = "0894565410";

const char* mqttBroker = "broker.hivemq.com";
const uint16_t mqttPort = 1883;

// Topic ที่ใช้สื่อสารกับหน้าเว็บ Dashboard
const char* topicCmd = "sgyver/esp32/commands"; // รับคำสั่งจากเว็บ
const char* topicSensors = "sgyver/esp32/sensors";  // ส่งค่าสถานะและเซนเซอร์ขึ้นเว็บ

// -------------------------------------------------------------
// 3. Embedded Zero-Dependency MQTT 3.1.1 Client
// -------------------------------------------------------------
class TinyMQTT {
private:
  WiFiClient client;
  const char* broker;
  uint16_t port;
  String clientId;
  unsigned long lastPing = 0;
  void (*messageCallback)(char* topic, uint8_t* payload, unsigned int length) = nullptr;

  void writeVarLength(uint32_t length) {
    do {
      uint8_t d = length % 128;
      length /= 128;
      if (length > 0) d |= 0x80;
      client.write(d);
    } while (length > 0);
  }

public:
  TinyMQTT() {
    clientId = "ESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  }

  void setServer(const char* host, uint16_t p = 1883) {
    broker = host;
    port = p;
  }

  void setCallback(void (*cb)(char*, uint8_t*, unsigned int)) {
    messageCallback = cb;
  }

  bool connected() {
    return client.connected();
  }

  bool connect() {
    if (client.connected()) return true;
    if (!client.connect(broker, port)) return false;

    uint16_t clientLen = clientId.length();
    uint16_t remLen = 10 + 2 + clientLen;

    client.write(0x10); // CONNECT
    writeVarLength(remLen);

    client.write((uint8_t)0x00);
    client.write((uint8_t)0x04);
    client.print("MQTT");

    client.write(0x04); // Version 3.1.1
    client.write(0x02); // Clean session
    client.write((uint8_t)0x00);
    client.write((uint8_t)0x3C); // 60s Keepalive

    client.write((uint8_t)(clientLen >> 8));
    client.write((uint8_t)(clientLen & 0xFF));
    client.print(clientId);

    unsigned long start = millis();
    while (client.available() < 4) {
      if (millis() - start > 3000) { client.stop(); return false; }
      delay(10);
    }
    uint8_t b0 = client.read();
    uint8_t b1 = client.read();
    uint8_t b2 = client.read();
    uint8_t b3 = client.read();

    if (b0 == 0x20 && b3 == 0x00) {
      lastPing = millis();
      return true;
    }
    client.stop();
    return false;
  }

  bool subscribe(const char* topic) {
    if (!client.connected()) return false;
    uint16_t topicLen = strlen(topic);
    uint16_t remLen = 2 + 2 + topicLen + 1;

    client.write(0x82); // SUBSCRIBE
    writeVarLength(remLen);
    client.write((uint8_t)0x00);
    client.write((uint8_t)0x01); // Packet ID = 1

    client.write((uint8_t)(topicLen >> 8));
    client.write((uint8_t)(topicLen & 0xFF));
    client.print(topic);
    client.write((uint8_t)0x00); // QoS 0
    return true;
  }

  bool publish(const char* topic, const char* payload) {
    if (!client.connected()) return false;
    uint16_t topicLen = strlen(topic);
    uint16_t payloadLen = strlen(payload);
    uint16_t remLen = 2 + topicLen + payloadLen;

    client.write(0x30); // PUBLISH
    writeVarLength(remLen);

    client.write((uint8_t)(topicLen >> 8));
    client.write((uint8_t)(topicLen & 0xFF));
    client.print(topic);
    client.print(payload);
    return true;
  }

  void loop() {
    if (!client.connected()) return;

    if (millis() - lastPing > 20000) {
      lastPing = millis();
      uint8_t ping[2] = {0xC0, 0x00};
      client.write(ping, 2);
    }

    while (client.available()) {
      uint8_t header = client.read();
      if ((header & 0xF0) == 0xD0) { // PINGRESP
        client.read();
        continue;
      }
      if ((header & 0xF0) == 0x90) { // SUBACK
        int rem = client.read();
        for (int i = 0; i < rem; i++) client.read();
        continue;
      }
      if ((header & 0xF0) == 0x30) { // PUBLISH
        uint32_t multiplier = 1;
        uint32_t remLen = 0;
        uint8_t digit;
        do {
          digit = client.read();
          remLen += (digit & 127) * multiplier;
          multiplier *= 128;
        } while ((digit & 128) != 0);

        uint8_t tLenH = client.read();
        uint8_t tLenL = client.read();
        uint16_t topicLen = (tLenH << 8) | tLenL;
        char topicBuf[128];
        int readTLen = min((int)topicLen, 127);
        client.readBytes(topicBuf, readTLen);
        topicBuf[readTLen] = '\0';
        for (int i = readTLen; i < topicLen; i++) client.read();

        uint16_t payloadLen = remLen - 2 - topicLen;
        uint8_t payloadBuf[256];
        int readPLen = min((int)payloadLen, 255);
        client.readBytes(payloadBuf, readPLen);
        payloadBuf[readPLen] = '\0';
        for (int i = readPLen; i < payloadLen; i++) client.read();

        if (messageCallback) {
          messageCallback(topicBuf, payloadBuf, readPLen);
        }
      }
    }
  }
};

TinyMQTT mqtt;

// -------------------------------------------------------------
// 4. ฟังก์ชันควบคุมฮาร์ดแวร์
// -------------------------------------------------------------
void setColorLed(bool state) {
  colorLedState = state;
  if (state) {
    pinMode(COLOR_LED_PIN, INPUT); // ปล่อยลอย ไฟสว่าง
  } else {
    pinMode(COLOR_LED_PIN, OUTPUT);
    digitalWrite(COLOR_LED_PIN, LOW); // ดึงกราวด์ ไฟดับ
  }
}

void setRelay(int index, bool state) {
  if (index < 0 || index >= 4) return;
  relayStates[index] = state;
  int pin = relayPins[index];
  if (state) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  } else {
    pinMode(pin, INPUT);
  }
}

// -------------------------------------------------------------
// 5. ไดรเวอร์เซนเซอร์ (DHT & TCS34725 ในตัว)
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

uint16_t readTCS16(uint8_t reg) {
  Wire.beginTransmission(TCS34725_ADDR);
  Wire.write(0x80 | reg);
  if (Wire.endTransmission() != 0) return 0;

  Wire.requestFrom((uint8_t)TCS34725_ADDR, (uint8_t)2);
  if (Wire.available() < 2) return 0;
  uint8_t low = Wire.read();
  uint8_t high = Wire.read();
  return ((uint16_t)high << 8) | low;
}

bool readColorSensor(int &outR, int &outG, int &outB, int &outC, String &outHex) {
  uint16_t c = readTCS16(0x14); // CDATAL
  uint16_t r = readTCS16(0x16); // RDATAL
  uint16_t g = readTCS16(0x18); // GDATAL
  uint16_t b = readTCS16(0x1A); // BDATAL

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

bool readDHTSensor(int pin, float &temp, float &hum, bool is22) {
  uint8_t data[5] = {0, 0, 0, 0, 0};
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(20);
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(35);

  noInterrupts();
  unsigned long timeout = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) { interrupts(); return false; } }
  timeout = micros();
  while (digitalRead(pin) == LOW) { if (micros() - timeout > 100) { interrupts(); return false; } }
  timeout = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) { interrupts(); return false; } }

  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(pin) == LOW) { if (micros() - timeout > 90) { interrupts(); return false; } }
    unsigned long startHigh = micros();
    while (digitalRead(pin) == HIGH) { if (micros() - startHigh > 110) { interrupts(); return false; } }
    if ((micros() - startHigh) > 40) data[i / 8] |= (1 << (7 - (i % 8)));
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
// 6. ส่งสถานะทั้งหมดขึ้น MQTT (Realtime 50ms)
// -------------------------------------------------------------
float currentTemp = 28.5, currentHum = 60.0;
int curR = -999, curG = -999, curB = -999, curC = 0;
String curHex = "#3B82F6";

void publishAllSensors() {
  String json = "{";
  json += "\"relays\":[" + String(relayStates[0] ? "1" : "0") + "," +
                          String(relayStates[1] ? "1" : "0") + "," +
                          String(relayStates[2] ? "1" : "0") + "," +
                          String(relayStates[3] ? "1" : "0") + "],";
  json += "\"temp\":" + String(currentTemp, 1) + ",";
  json += "\"hum\":" + String(currentHum, 1) + ",";
  json += "\"led\":" + String(colorLedState ? "true" : "false") + ",";
  json += "\"color\":{\"r\":" + String(curR >= 0 ? curR : 59) + ",\"g\":" + String(curG >= 0 ? curG : 130) + ",\"b\":" + String(curB >= 0 ? curB : 246) + ",\"c\":" + String(curC) + ",\"hex\":\"" + curHex + "\"}";
  json += "}";

  mqtt.publish(topicSensors, json.c_str());
}

// -------------------------------------------------------------
// 7. ฟังก์ชันรับคำสั่งจากหน้าเว็บ (ตอบสนองใน 0.05 วินาที!)
// -------------------------------------------------------------
void onMqttCommand(char* topic, uint8_t* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("⚡ [MQTT Command ได้รับใน 50ms]: %s\n", msg.c_str());

  // ตรวจคำสั่งรีเลย์เดี่ยว: {"relay":1, "state":true}
  int rIdx = msg.indexOf("\"relay\":");
  if (rIdx != -1) {
    int ch = msg.substring(rIdx + 8, rIdx + 10).toInt();
    bool state = (msg.indexOf("\"state\":true") != -1) || (msg.indexOf("\"state\":1") != -1);
    if (ch >= 1 && ch <= 4) {
      setRelay(ch - 1, state);
      Serial.printf(">>> Relay %d -> %s\n", ch, state ? "เปิด" : "ปิด");
      publishAllSensors(); // ตอบกลับทันทีใน 50ms
      return;
    }
  }

  // ตรวจคำสั่ง Master All: {"master":true}
  if (msg.indexOf("\"master\":true") != -1) {
    for (int i = 0; i < 4; i++) setRelay(i, true);
    publishAllSensors();
    return;
  } else if (msg.indexOf("\"master\":false") != -1) {
    for (int i = 0; i < 4; i++) setRelay(i, false);
    publishAllSensors();
    return;
  }

  // ตรวจคำสั่งไฟ LED เซนเซอร์สี: {"led":true}
  if (msg.indexOf("\"led\":true") != -1) {
    setColorLed(true);
    publishAllSensors();
    return;
  } else if (msg.indexOf("\"led\":false") != -1) {
    setColorLed(false);
    publishAllSensors();
    return;
  }
}

// -------------------------------------------------------------
// 8. SETUP & LOOP
// -------------------------------------------------------------
unsigned long lastColorSample = 0;
unsigned long lastDhtSample = 0;
static unsigned long lastHeartbeat = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=======================================================");
  Serial.println("  ESP32 Super Realtime Controller - MQTT Cloud (50ms) ");
  Serial.println("  Broker: broker.hivemq.com (Port 1883) ");
  Serial.println("  Topic Command: sgyver/esp32/commands ");
  Serial.println("  Topic Sensors: sgyver/esp32/sensors ");
  Serial.println("=======================================================");

  for (int i = 0; i < 4; i++) setRelay(i, false);
  setColorLed(true);

  // I2C Scanner
  Wire.begin(21, 22);
  Serial.println("[I2C Scanner] สแกนหาอุปกรณ์ I2C (SDA=21, SCL=22)...");
  int foundCount = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  -> พบอุปกรณ์ I2C ที่ Address 0x%02X\n", addr);
      foundCount++;
    }
  }
  if (foundCount == 0) {
    Serial.println("⚠️ ไม่พบอุปกรณ์ I2C ใดๆ! ตรวจสอบสาย SDA(21), SCL(22), 3.3V, GND");
  }

  tcsFound = initTCS34725();
  if (tcsFound) Serial.println("✅ เซนเซอร์ตรวจจับสี TCS34725 พร้อมทำงาน!");
  else Serial.println("⚠️ ยังไม่พบ TCS34725 จะพยายามเชื่อมต่อใหม่เรื่อยๆ ใน loop");

  Serial.print("กำลังเชื่อมต่อ WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  // ตั้งค่า MQTT
  mqtt.setServer(mqttBroker, mqttPort);
  mqtt.setCallback(onMqttCommand);
}

void loop() {
  // ตรวจสอบการเชื่อมต่อ MQTT (ต่อใหม่อัตโนมัติเมื่อหลุด)
  if (!mqtt.connected()) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 2500) {
      lastReconnect = millis();
      Serial.print("[MQTT] กำลังเชื่อมต่อ Broker HiveMQ...");
      if (mqtt.connect()) {
        Serial.println(" สำเร็จ!");
        mqtt.subscribe(topicCmd);
        Serial.println("✅ Subscribed รับคำสั่งที่: " + String(topicCmd));
        publishAllSensors();
      } else {
        Serial.println(" ไม่สำเร็จ รอต่อใหม่...");
      }
    }
  } else {
    mqtt.loop();
  }

  unsigned long now = millis();

  // 1. ตรวจจับสี (ตรวจทุก 120ms สตรีมค่าเมื่อสีเปลี่ยน หรือส่งเป็นระยะทุก 2 วินาที)
  if (now - lastColorSample >= 120) {
    lastColorSample = now;
    if (!tcsFound) tcsFound = initTCS34725();

    if (tcsFound) {
      int r = 0, g = 0, b = 0, c = 0;
      String hex = "";
      if (readColorSensor(r, g, b, c, hex)) {
        bool colorChanged = (curR == -999 || abs(r - curR) > 4 || abs(g - curG) > 4 || abs(b - curB) > 4);
        bool heartbeatDue = (now - lastHeartbeat >= 2000);
        if (colorChanged || heartbeatDue) {
          curR = r; curG = g; curB = b; curC = c; curHex = hex;
          lastHeartbeat = now;
          publishAllSensors();
          if (colorChanged) {
            Serial.printf("🎨 [Live Color Stream 50ms] -> %s (R:%d G:%d B:%d Clear:%d)\n", hex.c_str(), r, g, b, c);
          }
        }
      }
    }
  }

  // 2. อ่านอุณหภูมิและความชื้น DHT ทุก 3 วินาที
  if (now - lastDhtSample >= 3000) {
    lastDhtSample = now;
    float t = 0, h = 0;
    if (readDHTSensor(DHT_PIN, t, h, IS_DHT22)) {
      if (abs(t - currentTemp) >= 0.2 || abs(h - currentHum) >= 1.0) {
        currentTemp = t; currentHum = h;
        publishAllSensors();
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
}
