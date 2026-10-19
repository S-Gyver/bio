-- =========================================================================
-- ESP32 IoT Dashboard - Supabase SQL Schema
-- คัดลอกโค้ดทั้งหมดนี้ไปวางใน Supabase Dashboard -> SQL Editor แล้วกด RUN
-- =========================================================================

-- 1. สร้างตารางเก็บสถานะอุปกรณ์ esp32_devices
CREATE TABLE IF NOT EXISTS public.esp32_devices (
    id INT PRIMARY KEY DEFAULT 1,
    name TEXT DEFAULT 'ESP32 Smart Controller',
    
    -- สถานะรีเลย์ 4 ช่อง
    relay1 BOOLEAN DEFAULT FALSE,
    relay2 BOOLEAN DEFAULT FALSE,
    relay3 BOOLEAN DEFAULT FALSE,
    relay4 BOOLEAN DEFAULT FALSE,

    -- ค่าเซนเซอร์ DHT22
    temperature NUMERIC(4, 1) DEFAULT 28.5,
    humidity NUMERIC(4, 1) DEFAULT 62.0,

    -- ค่าเซนเซอร์ตรวจจับสี TCS34725
    color_r INT DEFAULT 59,
    color_g INT DEFAULT 130,
    color_b INT DEFAULT 246,
    color_c INT DEFAULT 1200,
    color_hex TEXT DEFAULT '#3B82F6',

    -- เวลาอัปเดตล่าสุด
    last_seen TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- 2. สร้างแถวเริ่มต้นสำหรับ Device 1 (หากยังไม่มี)
INSERT INTO public.esp32_devices (id, name, relay1, relay2, relay3, relay4, temperature, humidity, color_hex)
VALUES (1, 'ESP32 Smart Controller', FALSE, FALSE, FALSE, FALSE, 28.5, 62.0, '#3B82F6')
ON CONFLICT (id) DO NOTHING;

-- 3. เปิดระบบ Row Level Security (RLS)
ALTER TABLE public.esp32_devices ENABLE ROW LEVEL SECURITY;

-- 4. กำหนดสิทธิ์ให้ผู้ใช้ทั่วไป (Anon Key) สามารถอ่านและอัปเดตข้อมูลได้
DROP POLICY IF EXISTS "Allow public read access" ON public.esp32_devices;
CREATE POLICY "Allow public read access" ON public.esp32_devices
    FOR SELECT TO anon USING (true);

DROP POLICY IF EXISTS "Allow public update access" ON public.esp32_devices;
CREATE POLICY "Allow public update access" ON public.esp32_devices
    FOR UPDATE TO anon USING (true) WITH CHECK (true);

DROP POLICY IF EXISTS "Allow public insert access" ON public.esp32_devices;
CREATE POLICY "Allow public insert access" ON public.esp32_devices
    FOR INSERT TO anon WITH CHECK (true);

-- 5. เปิดระบบ Realtime ให้ส่งข้อมูลการเปลี่ยนแปลงไปยังหน้าเว็บทันที
ALTER PUBLICATION supabase_realtime ADD TABLE public.esp32_devices;
