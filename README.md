# ☀️ Solar Monitor & Auto-Cleaning System (LoRaWAN IoT)

![Device](https://img.shields.io/badge/Device-Heltec_WiFi_LoRa_32_V3-blue?style=for-the-badge&logo=arduino)
![Platform](https://img.shields.io/badge/Platform-Python_Flask-yellow?style=for-the-badge&logo=python)
![Database](https://img.shields.io/badge/Database-MySQL_XAMPP-orange?style=for-the-badge&logo=mysql)
![Status](https://img.shields.io/badge/Status-Completed-success?style=for-the-badge)

> **Đồ án Tốt nghiệp Kỹ sư Công nghệ Thông tin**
> * **Sinh viên:** Vũ Việt Hoàng - 20050032
> * **GVHD:** ThS. Lê Duy Hùng
> * **Trường:** Đại học Bình Dương

---

## 📖 Giới thiệu (Overview)

Hệ thống **IoT Giám sát và Tự động vệ sinh tấm pin năng lượng mặt trời** là giải pháp công nghệ nhằm giải quyết vấn đề suy hao hiệu suất điện năng do bụi bẩn. Hệ thống sử dụng công nghệ giao tiếp vô tuyến tầm xa **LoRa** (Long Range) để truyền dữ liệu từ các Node cảm biến ngoài trời về trung tâm giám sát, khắc phục nhược điểm về khoảng cách của WiFi.

### 🚀 Chức năng chính:
1.  **Giám sát thời gian thực (Real-time Monitoring):**
    * Đo dòng điện (A), Điện áp (V), Công suất (W) từ tấm pin.
    * Đo nồng độ bụi trong không khí (mg/m³).
2.  **Vệ sinh tự động (Smart Cleaning):**
    * Tự động kích hoạt động cơ chổi quét khi nồng độ bụi vượt ngưỡng cài đặt.
3.  **Điều khiển từ xa (Remote Control):**
    * Cho phép người dùng bật/tắt chế độ vệ sinh thủ công qua Web Dashboard.
4.  **Báo cáo & Lưu trữ:**
    * Lưu lịch sử dữ liệu vào MySQL để phân tích hiệu suất theo thời gian.

---

## 📂 Cấu trúc dự án (Project Structure)

Dưới đây là cấu trúc thư mục của Source Code:

```text
VuVietHoang-20050032-23TH01/
├── Solar_Monitoring_Project/           # [MAIN] Thư mục chứa Source Code chính
    ├── esp32-lora/                     # Code Firmware cho mạch Heltec V3 (Arduino)
    │   └── Solar_Node.ino              # File code nạp cho ESP32
    ├── static/                         # Tài nguyên Frontend (CSS, JS, Images)
    ├── templates/                      # Giao diện HTML (Dashboard)
    └── index.html
    ├── app.py                          # Web Server (Python Flask Backend)
    └── database.sql                    # File cấu trúc CSDL MySQL (Nếu có)
```
🛠️ Yêu cầu hệ thống (Prerequisites)
Để chạy được dự án này, bạn cần chuẩn bị:

1. Phần cứng (Hardware)
- Vi điều khiển: Heltec WiFi LoRa 32 V3.
- Cảm biến: INA219 (Dòng/Áp), DHT22 (Nhiệt độ/Độ ẩm), Sharp GP2Y10 (Bụi).
- Cơ cấu chấp hành: Động cơ DC giảm tốc, Driver L298N.
- Nguồn: Pin Li-ion 18650 hoặc Nguồn Adapter 5V.

2. Phần mềm (Software)
IDE: Arduino IDE (để nạp code cho mạch).
Server: Python 3.9.13 (cài đặt các thư viện Flask).
Database: XAMPP (Module MySQL/phpMyAdmin).

⚙️ Hướng dẫn cài đặt (Installation Guide)
Bước 1: Cấu hình Phần cứng (ESP32)

1. Mở thư mục Solar_Monitoring_Project/esp32-lora.
2. Mở file .ino bằng Arduino IDE.
3. Cài đặt các thư viện cần thiết trong Library Manager:
- Heltec ESP32 Dev-boards
- Adafruit INA219
- LoRaWan_APP
4. Kết nối mạch Heltec V3 với máy tính và nạp code.

Bước 2: Cấu hình Cơ sở dữ liệu (Database)

1. Cài đặt và mở XAMPP Control Panel -> Start Apache và MySQL.
2. Truy cập http://localhost/phpmyadmin.
3. Tạo Database mới tên là: solar_monitoring.
4. Import file SQL (nếu có) hoặc tạo bảng sensor_data với các cột: id, voltage, current, power, dust_density, timestamp.

Bước 3: Chạy Web Server (Python)
1. Mở terminal (CMD/VS Code) tại thư mục Solar_Monitoring_Project.
2. Cài đặt các thư viện Python:
```text
pip install flask mysql-connector-python pyserial
```
3. Chạy Server:
```text
python api_to_sql.py
```
4. Mở trình duyệt và truy cập: http://localhost:5000

📸 Hình ảnh Demo (Screenshots)

👨‍💻 Tác giả (Author)
Vũ Việt Hoàng
MSSV: 20050032
Khoa: Công nghệ Thông tin, Robot & AI
Liên hệ: [Email của bạn]@gmail.com

