# 🚀 ESP32 Smart Controller & IoT Dashboard

เว็บแอปพลิเคชัน (Web App) โมเดิร์นแดชบอร์ดสำหรับมอนิเตอร์และควบคุมบอร์ด **ESP32** รองรับการควบคุมรีเลย์ 4 ช่อง, วัดอุณหภูมิและความชื้นด้วย DHT22, และตรวจจับเฉดสีจริงด้วย TCS34725 Color Sensor พร้อมโหมดจำลอง (Demo Mode) ที่เปิดใช้งานและทดสอบฟังก์ชันทั้งหมดได้ทันที

---

## 📁 โครงสร้างโปรเจกต์ (Project Structure)

```text
esp32-iot-dashboard/
├── index.html            # โครงสร้างหน้าเว็บ Semantic HTML5 รองรับ Mobile-First
├── style.css             # สไตล์ CSS โมเดิร์น Glassmorphism, Theme มืด/สว่าง, Touch-friendly
├── app.js                # จัดการ State, Auto-polling, Fetch API, จำลองข้อมูล และ Toast แจ้งเตือน
├── esp32_firmware.ino    # ซอร์สโค้ด Arduino/ESP32 พร้อม REST API และเปิด CORS ให้เชื่อมต่อง่าย
└── README.md             # คู่มือการใช้งานและการต่อวงจร
```

---

## ✨ ฟีเจอร์หลัก (Key Features)

1. **⚡ ควบคุมไฟ 4 ช่อง (4-Channel Relay Control):**
   - สวิตช์สไตล์ iOS สัมผัสนุ่มนวล (Touch-Friendly min 44px) รองรับการสั่น Haptic Feedback
   - บอกสถานะเปิด/ปิด (ON / OFF) พร้อมไฟสีเรืองแสง (Glow Effect)
   - ปุ่ม Master: **"เปิดทั้งหมด"** และ **"ปิดทั้งหมด"**
2. **🌡️ เซนเซอร์วัดสภาพแวดล้อม (DHT22 Sensor):**
   - อุณหภูมิ (°C) ตัวเลขขนาดใหญ่อ่านง่าย พร้อมแถบสถานะ (เช่น เย็นสบาย, สบายตัว, อากาศร้อน)
   - ความชื้นสัมพัทธ์ (%) พร้อมเกจเปอร์เซ็นต์และคำแนะนำระดับความชื้น
3. **🎨 เซนเซอร์ตรวจจับสี (TCS34725 RGB & Clear):**
   - **Live Color Preview Box:** กล่องสีจริงที่เซนเซอร์ตรวจจับ พร้อมเงาเรืองแสงตามเฉดสี
   - โค้ดสี **HEX Code** (เช่น `#3B82F6`) พร้อมปุ่มคลิกเพื่อคัดลอก (Copy with animation)
   - แถบแสดงค่าสีแยกช่อง **Red, Green, Blue** (0 - 255) และความเข้มแสง **Clear**
   - ปุ่ม **"สุ่มสีทดสอบ"** สำหรับทดลองเฉดสีในโหมด Demo
4. **📡 การตั้งค่าการเชื่อมต่อ (Connection & Auto-Fetch):**
   - ช่องกรอก IP Address ของ ESP32 (เช่น `192.168.1.47`) บันทึกค่าลง `localStorage` อัตโนมัติ
   - Badge แสดงสถานะการเชื่อมต่อ: `Connected (Online)`, `Disconnected`, `กำลังดึงข้อมูล...`, `โหมดจำลอง (Demo)`
   - เลือกระยะเวลา Auto-Fetch (1s, 2s, 3s, 5s หรือปิด) พร้อมแสดงค่า Ping Latency
   - **สวิตช์เปิด/ปิด โหมดจำลอง (Demo Mode):** ทดสอบหน้าเว็บได้ทันทีโดยไม่ต้องต่อฮาร์ดแวร์จริง
5. **🌓 Dark / Light Mode:**
   - รองรับการสลับธีมมืดและสว่าง บันทึกค่าจำไว้ในเบราว์เซอร์

---

## 🛠️ การต่อวงจรฮาร์ดแวร์กับ ESP32 (Pin Wiring)

| โมดูล / เซนเซอร์ | ขาบนโมดูล | ขาบน ESP32 (GPIO) | หมายเหตุ |
| :--- | :--- | :--- | :--- |
| **Relay Module** | VCC | 5V / VIN | แนะนำใช้แหล่งจ่ายไฟแยกหากโหลดเยอะ |
| | GND | GND | กราวด์ร่วม |
| | IN1 | GPIO 25 | ควบคุม Relay ช่อง 1 |
| | IN2 | GPIO 26 | ควบคุม Relay ช่อง 2 |
| | IN3 | GPIO 32 | ควบคุม Relay ช่อง 3 |
| | IN4 | GPIO 33 | ควบคุม Relay ช่อง 4 |
| **DHT22** | VCC / + | 3.3V หรือ 5V | |
| | DATA / OUT | GPIO 4 | แนะนำใส่ Pull-up Resistor 10kΩ ถ้าเป็นตัวเดี่ยว |
| | GND / - | GND | กราวด์ร่วม |
| **TCS34725** | VIN | 3.3V | |
| | GND | GND | กราวด์ร่วม |
| | SCL | GPIO 22 | I2C Clock |
| | SDA | GPIO 21 | I2C Data |

---

## 🔌 ข้อกำหนด REST API (ESP32 Endpoints)

เฟิร์มแวร์ใน `esp32_firmware.ino` รองรับ Endpoints ดังนี้:

### 1. ดึงข้อมูลสถานะและเซนเซอร์ทั้งหมด
- **Endpoint:** `GET http://<ESP32_IP>/api/data`
- **Response (JSON):**
```json
{
  "relays": [0, 1, 0, 0],
  "dht": {
    "temp": 28.5,
    "hum": 64.0
  },
  "color": {
    "r": 59,
    "g": 130,
    "b": 246,
    "c": 1280,
    "hex": "#3B82F6"
  }
}
```

### 2. สั่งเปิด-ปิดสวิตช์รายช่อง
- **Endpoint:** `GET http://<ESP32_IP>/toggle?ch=<1..4>&state=<0|1>`
- **Response (JSON):**
```json
{ "status": "ok", "ch": 1, "state": true }
```

### 3. สั่งเปิด-ปิดสวิตช์ทั้งหมด (Master)
- **Endpoint:** `GET http://<ESP32_IP>/master?state=<0|1>`
- **Response (JSON):**
```json
{ "status": "ok", "all": true }
```

> **หมายเหตุเรื่อง CORS:** ตัวเฟิร์มแวร์ได้เปิด Header `Access-Control-Allow-Origin: *` ไว้เรียบร้อยแล้ว ทำให้เบราว์เซอร์สามารถเรียกใช้งานผ่าน Fetch API ข้ามเครือข่ายได้โดยไม่ติดบล็อก CORS Policy

---

## 🖥️ วิธีเปิดใช้งานเว็บแอป (How to Run)

1. ดับเบิลคลิกเปิดไฟล์ `index.html` ผ่าน Google Chrome, Edge หรือ Safari ได้ทันที หรือ
2. รันผ่าน Local Web Server (เช่น Live Server บน VS Code หรือ `python -m http.server 8080` / `npx serve`)
3. เปิดทดสอบบนมือถือโดยเชื่อมต่อ WiFi วงเดียวกัน แล้วเปิดผ่าน IP เครื่องคอมพิวเตอร์ของคุณ
