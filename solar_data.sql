-- 1. TẠO DATABASE
CREATE DATABASE IF NOT EXISTS solar_monitoring;
USE solar_monitoring;

-- =======================================================
-- 2. BẢNG DEVICES (Sổ hộ khẩu thiết bị)
-- Lưu ý: device_eui là Khóa chính (Primary Key)
-- =======================================================
CREATE TABLE IF NOT EXISTS devices (
    device_eui VARCHAR(50) PRIMARY KEY, -- Mã Hex định danh (VD: d7a92b1f...)
    name VARCHAR(100) NOT NULL,         -- Tên hiển thị (VD: Sensor Node 01)
    application_name VARCHAR(100),      -- Tên ứng dụng
    location VARCHAR(100),              -- Tọa độ GPS (VD: "10.823,106.629")
    description TEXT,
    is_active TINYINT(1) DEFAULT 1,     -- 1: Online, 0: Offline
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- =======================================================
-- 3. BẢNG SENSOR_DATA (Dữ liệu cảm biến đo đạc)
-- Lưu trữ Time-series data
-- =======================================================
CREATE TABLE IF NOT EXISTS sensor_data (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_eui VARCHAR(50),
    
    -- Thông số Điện năng
    bus_voltage FLOAT,      -- Điện áp (V)
    current_A FLOAT,        -- Dòng điện (A)
    power_W FLOAT,          -- Công suất (W)
    solar_eff FLOAT DEFAULT 0, -- Hiệu suất (%)
    
    -- Thông số Môi trường
    pm25 FLOAT,             -- Bụi mịn
    temp_c FLOAT,           -- Nhiệt độ
    humidity FLOAT,         -- Độ ẩm
    battery_pct FLOAT,      -- Pin (%)
    
    -- Trạng thái Chấp hành (0: Off, 1: On)
    motor_status TINYINT(1) DEFAULT 0,
    pump_status TINYINT(1) DEFAULT 0,
    
    measured_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    -- Ràng buộc khóa ngoại: Xóa thiết bị -> Xóa luôn data của nó
    FOREIGN KEY (device_eui) REFERENCES devices(device_eui) ON DELETE CASCADE
);

-- =======================================================
-- 4. BẢNG UPLINK_MESSAGES (Thông số sóng LoRa)
-- =======================================================
CREATE TABLE IF NOT EXISTS uplink_messages (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_eui VARCHAR(50),
    
    f_cnt INT,              -- Frame Counter
    rssi INT,               -- Cường độ sóng (dBm)
    snr FLOAT,              -- Tỷ lệ tín hiệu/nhiễu
    payload_hex TEXT,       -- Gói tin thô dạng Hex (để debug)
    
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, -- Thời gian server nhận
    
    FOREIGN KEY (device_eui) REFERENCES devices(device_eui) ON DELETE CASCADE
);

-- =======================================================
-- 5. BẢNG DOWNLINK_MESSAGES (Lịch sử điều khiển)
-- Cập nhật mới nhất: Có sent_at và status VARCHAR dài
-- =======================================================
CREATE TABLE IF NOT EXISTS downlink_messages (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    device_eui VARCHAR(50),
    
    command VARCHAR(50),        -- Tên lệnh (VD: pump_on)
    payload_hex VARCHAR(50),    -- Mã Hex gửi đi (VD: A1)
    status VARCHAR(50) DEFAULT 'PENDING', -- Trạng thái (PENDING, SENT, FAILED)
    
    created_by VARCHAR(50) DEFAULT 'admin', -- Người gửi lệnh
    sent_at TIMESTAMP NULL,                 -- Thời gian gửi thực tế
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, -- Thời gian tạo lệnh
    
    FOREIGN KEY (device_eui) REFERENCES devices(device_eui) ON DELETE CASCADE
);

-- =======================================================
-- 6. BẢNG SENSOR_THRESHOLDS (Cài đặt ngưỡng)
-- =======================================================
CREATE TABLE IF NOT EXISTS sensor_thresholds (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_eui VARCHAR(50),
    
    param_name VARCHAR(50) NOT NULL, -- Tên tham số (VD: pm25_max)
    min_val FLOAT NULL,
    max_val FLOAT NULL,
    action VARCHAR(50) DEFAULT 'ALERT',
    
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    FOREIGN KEY (device_eui) REFERENCES devices(device_eui) ON DELETE CASCADE
);

-- =======================================================
-- 7. BẢNG WEBHOOK_LOGS (Nhật ký hệ thống)
-- Bảng độc lập, không có khóa ngoại
-- =======================================================
CREATE TABLE IF NOT EXISTS webhook_logs (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    event_type VARCHAR(50), -- UPLINK, DOWNLINK, ERROR
    raw_body JSON,          -- Lưu toàn bộ cục JSON nhận được
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);