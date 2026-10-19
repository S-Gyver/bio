/*
 * =========================================================================
 * ESP32 Relay & Sensor Controller (อัปเดตเชื่อมต่อกับ Web Dashboard)
 * =========================================================================
 * - รองรับ WiFi ของคุณ: "Earth_Aoey_aey"
 * - ใช้ขา GPIO รีเลย์ 4 ช่อง: 16, 17, 18, 19
 * - ใช้เทคนิคขับรีเลย์แบบ Active LOW (Open-Drain ตัดวงจร) ตามที่คุณกำหนด
 * - เพิ่ม CORS Header เพื่อให้เว็บ Dashboard ภายนอกเรียกใช้งานได้
 * - เพิ่ม REST API: /api/data, /toggle, /master สำหรับเชื่อมต่อหน้าเว็บใหม่
 * - หน้าเว็บเดิม (http://<IP>/) ยังคงเปิดใช้งานได้ปกติ
 * =========================================================================
 */

#include <WiFi.h>
#include <WebServer.h>

// --- WiFi Credentials ---
const char* ssid = "Earth_Aoey_Aey";
const char* password = "0894565410";

WebServer server(80);

// ขา GPIO สำหรับรีเลย์ 4 ช่อง (16, 17, 18, 19)
const int relayPins[4] = {16, 17, 18, 19};
bool relayStates[4] = {false, false, false, false}; // สถานะเริ่มต้น ปิดทั้งหมด

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

// ฟังก์ชันส่ง CORS Headers เพื่อไม่ให้ติดบล็อกบน Web Dashboard
void setCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// --- REST API 1: /api/data (ดึงสถานะรีเลย์และเซนเซอร์เป็น JSON) ---
void handleApiData() {
  setCORSHeaders();
  
  // สร้าง JSON ส่งกลับไปยัง Web Dashboard
  String json = "{";
  json += "\"relays\":[" 
       + String(relayStates[0] ? "1" : "0") + "," 
       + String(relayStates[1] ? "1" : "0") + "," 
       + String(relayStates[2] ? "1" : "0") + "," 
       + String(relayStates[3] ? "1" : "0") + "],";
  // ตัวเลขเซนเซอร์จำลองหรือเชื่อมต่อ DHT/TCS เพิ่มเติมได้ที่นี่
  json += "\"dht\":{\"temp\":28.5,\"hum\":62.0},";
  json += "\"color\":{\"r\":59,\"g\":130,\"b\":246,\"c\":1200,\"hex\":\"#3B82F6\"}";
  json += "}";

  server.send(200, "application/json", json);
}

// --- REST API 2: /toggle?ch=1 หรือ /toggle?ch=1&state=1 ---
void handleToggle() {
  setCORSHeaders();

  if (server.hasArg("ch")) {
    int ch = server.arg("ch").toInt();
    // รองรับทั้งแบบนับ 1..4 (Dashboard) และ 0..3 (หน้าเว็บเดิม)
    int index = (ch >= 1 && ch <= 4) ? (ch - 1) : ch;

    if (index >= 0 && index < 4) {
      if (server.hasArg("state")) {
        setRelay(index, server.arg("state").toInt() == 1);
      } else {
        setRelay(index, !relayStates[index]); // สลับสถานะ
      }

      // หากเรียกผ่าน Dashboard หรือ AJAX ให้ส่ง JSON กลับ
      server.send(200, "application/json", "{\"status\":\"ok\",\"ch\":" + String(index + 1) + ",\"state\":" + String(relayStates[index]) + "}");
      return;
    }
  }

  // หากกดจากหน้าเว็บเดิม (ไม่มีการระบุ AJAX) ให้ redirect กลับหน้าเดิม
  server.sendHeader("Location", "/");
  server.send(303);
}

// --- REST API 3: /master?state=1 (เปิดทั้งหมด / ปิดทั้งหมด) ---
void handleMaster() {
  setCORSHeaders();
  if (server.hasArg("state")) {
    bool targetState = (server.arg("state").toInt() == 1);
    for (int i = 0; i < 4; i++) {
      setRelay(i, targetState);
    }
    server.send(200, "application/json", "{\"status\":\"ok\",\"all\":" + String(targetState) + "}");
    return;
  }
  server.send(400, "application/json", "{\"error\":\"Missing state parameter\"}");
}

// --- หน้าเว็บควบคุมแบบเดิม (เปิดดูผ่าน IP โดยตรงได้) ---
String getHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32 Relay Control</title>";
  html += "<style>";
  html += "body { font-family: sans-serif; text-align: center; background: #0f172a; color: white; margin: 0; padding: 20px; }";
  html += "h1 { margin-bottom: 20px; font-size: 22px; }";
  html += ".grid { display: flex; flex-direction: column; gap: 15px; max-width: 380px; margin: auto; }";
  html += ".card { background: #1e293b; padding: 16px 20px; border-radius: 12px; display: flex; justify-content: space-between; align-items: center; border: 1px solid rgba(255,255,255,0.1); }";
  html += ".btn { border: none; padding: 12px 20px; font-size: 15px; border-radius: 8px; cursor: pointer; font-weight: bold; min-width: 130px; }";
  html += ".btn-on { background: #10b981; color: white; }";
  html += ".btn-off { background: #ef4444; color: white; }";
  html += "</style></head><body>";
  html += "<h1>ระบบควบคุมไฟ (ESP32)</h1>";
  html += "<div class='grid'>";

  for (int i = 0; i < 4; i++) {
    html += "<div class='card'>";
    html += "<span style='font-size: 15px; font-weight: bold;'>ช่อง " + String(i + 1) + " (ขา " + String(relayPins[i]) + ")</span>";
    if (relayStates[i]) {
      html += "<a href='/toggle?ch=" + String(i + 1) + "'><button class='btn btn-on'>เปิดอยู่ (กดปิด)</button></a>";
    } else {
      html += "<a href='/toggle?ch=" + String(i + 1) + "'><button class='btn btn-off'>ปิดอยู่ (กดเปิด)</button></a>";
    }
    html += "</div>";
  }

  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- กำลังเริ่มต้นระบบ ESP32 Relay Controller ---");

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
  Serial.print(">>> IP Address ของ ESP32 คือ: http://");
  Serial.print(WiFi.localIP());
  Serial.println(" <<<");
  Serial.println("(นำ IP นี้ไปกรอกในเว็บ Dashboard ได้ทันที)");

  // กำหนดเส้นทาง URL (Routing)
  server.on("/", handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/master", HTTP_GET, handleMaster);

  // รองรับ CORS Preflight
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      setCORSHeaders();
      server.send(204);
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println("HTTP Web Server พร้อมทำงานแล้ว!");
}

void loop() {
  server.handleClient();
}
