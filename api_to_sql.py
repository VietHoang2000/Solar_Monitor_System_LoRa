from flask import Flask, request, jsonify, render_template
import mysql.connector
from mysql.connector import pooling
import json
import datetime
import requests

app = Flask(__name__)

# =========================
# 1. CẤU HÌNH DATABASE VÀ KẾT NỐI CLOUD
# =========================
DB_CONFIG = {
    "host": "localhost",
    "user": "root",
    "password": "",  
    "database": "solar_monitoring",
}

# Tạo Connection Pool
cnxpool = pooling.MySQLConnectionPool(pool_name="mypool", pool_size=5, **DB_CONFIG)

def get_conn(): return cnxpool.get_connection()

# Cấu hình kết nối cloud
THINGSBOARD_HOST = "https://ui.easylorawan.com"  
TARGET_DEVICE_ID = "eb80f6b0-bf03-11f0-bcaf-c764231e522e"  

TB_USERNAME = "vhminecraftvn@gmail.com"
TB_PASSWORD = "0365373445Hoang@"

def get_jwt_token():
    """Hàm tự động đăng nhập để lấy chìa khóa (Token)"""
    url = f"{THINGSBOARD_HOST}/api/auth/login"
    try:
        response = requests.post(url, json={"username": TB_USERNAME, "password": TB_PASSWORD})
        if response.status_code == 200:
            return response.json().get("token")
        else:
            print(f"❌ LOGIN FAILED: {response.text}")
            return None
    except Exception as e:
        print(f"❌ NETWORK ERROR (LOGIN): {e}")
        return None

# =========================
# 2. API MỚI CHO DASHBOARD NÂNG CẤP
# =========================

# 1. API Lấy dữ liệu lịch sử (Để vẽ biểu đồ)
@app.route('/api/get-history', methods=['GET'])
def get_history():
    limit = request.args.get('limit', 20) # Mặc định lấy 20 điểm dữ liệu gần nhất
    conn = get_conn()
    cursor = conn.cursor(dictionary=True)
    
    # Lấy dữ liệu cảm biến
    cursor.execute(f"SELECT * FROM sensor_data ORDER BY id DESC LIMIT {limit}")
    data = cursor.fetchall()
    
    # Đảo ngược lại để vẽ biểu đồ từ trái qua phải (Cũ -> Mới)
    data.reverse()
    
    conn.close()
    return jsonify(data)

# API Lấy thống kê tổng quan
@app.route('/api/get-stats', methods=['GET'])
def get_stats():
    conn = get_conn()
    cursor = conn.cursor(dictionary=True)
    
    timeout_minutes = 10 
    
    sql_check_offline = f"""
        UPDATE devices 
        SET is_active = 0 
        WHERE updated_at < (NOW() - INTERVAL {timeout_minutes} MINUTE)
    """
    cursor.execute(sql_check_offline)
    conn.commit()
    
    # Đếm số thiết bị
    cursor.execute("SELECT COUNT(*) as total FROM devices")
    total_dev = cursor.fetchone()['total']
    
    # Đếm thiết bị đang hoạt động (Active)
    cursor.execute("SELECT COUNT(*) as active FROM devices WHERE is_active=1")
    active_dev = cursor.fetchone()['active']
    
    # Lấy danh sách thiết bị
    cursor.execute("SELECT * FROM devices")
    devices = cursor.fetchall()
    
    conn.close()
    
    # Trả về kết quả cho Dashboard
    return jsonify({
        "total": total_dev,
        "active": active_dev,
        "inactive": total_dev - active_dev,
        "devices": devices
    })
    
# 3. API Cài đặt Ngưỡng (Threshold)
@app.route('/api/threshold', methods=['POST'])
def set_threshold():
    conn = get_conn()
    cursor = conn.cursor()
    try:
        req = request.json
        eui = req.get('device_eui')
        param = req.get('param_name')
        min_v = req.get('min_val')
        max_v = req.get('max_val')

        if not eui or not param: return jsonify({"msg": "Missing params"}), 400

        # Kiểm tra xem đã có ngưỡng này chưa
        cursor.execute("SELECT id FROM sensor_thresholds WHERE device_eui=%s AND param_name=%s", (eui, param))
        row = cursor.fetchone()

        if row:
            # Update
            sql = "UPDATE sensor_thresholds SET min_val=%s, max_val=%s WHERE id=%s"
            cursor.execute(sql, (min_v, max_v, row[0]))
        else:
            # Insert
            sql = "INSERT INTO sensor_thresholds (device_eui, param_name, min_val, max_val) VALUES (%s, %s, %s, %s)"
            cursor.execute(sql, (eui, param, min_v, max_v))
        
        conn.commit()
        return jsonify({"status": "ok"}), 200

    except Exception as e:
        print(f"❌ THRESHOLD ERROR: {e}")
        return jsonify({"error": str(e)}), 500
    finally:
        cursor.close()
        conn.close()  
          
# 4. API Lấy lịch sử tín hiệu (RSSI, SNR) từ bảng uplink_messages
@app.route('/api/get-signal-history', methods=['GET'])
def get_signal_history():
    limit = request.args.get('limit', 20)
    conn = get_conn()
    cursor = conn.cursor(dictionary=True)
    
    # Lấy dữ liệu sóng mới nhất
    cursor.execute(f"SELECT * FROM uplink_messages ORDER BY id DESC LIMIT {limit}")
    data = cursor.fetchall()
    
    data.reverse() # Đảo chiều để vẽ biểu đồ từ trái qua phải
    conn.close()
    return jsonify(data)

# 5. API Gửi lệnh điều khiển thiết bị (Downlink Command)
@app.route('/api/control', methods=['POST'])
def send_command():
    conn = get_conn()
    cursor = conn.cursor()
    try:
        req = request.json
        # Lấy lệnh từ Dashboard
        cmd = req.get('command') 
        # (Ở đây ta dùng Device ID cố định trong config, hoặc bạn có thể map từ DB)
        
        if not cmd: return jsonify({"msg": "Missing command"}), 400
        
        # 1. LƯU LOG VÀO DB (PENDING)
        sql_pending = """
            INSERT INTO downlink_messages (device_eui, command, status, created_by, created_at) 
            VALUES (%s, %s, 'PENDING', 'admin', NOW())
        """
        # Lưu ý: device_eui ở đây chỉ để lưu log vào DB cho khớp khóa ngoại
        # Bạn cần đảm bảo biến 'eui' bên dưới khớp với thiết bị thực tế
        eui_for_log = "d7a92b1f77ced2e0" # Hoặc lấy từ req.get('device_eui')
        
        cursor.execute(sql_pending, (eui_for_log, cmd))
        conn.commit()
        msg_id = cursor.lastrowid 

        # 2. GỬI LỆNH SANG CLOUD
        print(f"🚀 PREPARING TO SEND: {cmd}...")
        
        # Bước A: Đăng nhập lấy Token
        jwt_token = get_jwt_token()
        if not jwt_token:
            raise Exception("Cannot Login to Cloud")

        # Bước B: Gửi lệnh RPC (One-way)
        # URL chuẩn: /api/plugins/rpc/oneway/{deviceId}
        rpc_url = f"{THINGSBOARD_HOST}/api/plugins/rpc/oneway/{TARGET_DEVICE_ID}"
        
        headers = {
            "X-Authorization": f"Bearer {jwt_token}",
            "Content-Type": "application/json"
        }
        
        # Payload chuẩn RPC
        payload = {
            "method": "setValue",
            "params": {"cmd": cmd} 
        }

        resp = requests.post(rpc_url, json=payload, headers=headers, timeout=10)
        
        if resp.status_code == 200:
            print(f"✅ CLOUD ACCEPTED: {resp.text}")
            final_status = 'SENT'
        else:
            print(f"❌ CLOUD REJECTED ({resp.status_code}): {resp.text}")
            final_status = 'FAILED'

        # 3. CẬP NHẬT DB
        cursor.execute("UPDATE downlink_messages SET status=%s, sent_at=NOW() WHERE id=%s", (final_status, msg_id))
        conn.commit()

        return jsonify({"status": final_status, "cmd": cmd}), 200

    except Exception as e:
        print(f"❌ ERROR: {e}")
        # Cập nhật DB là Failed nếu lỗi
        if 'msg_id' in locals():
            cursor.execute("UPDATE downlink_messages SET status='FAILED' WHERE id=%s", (msg_id,))
            conn.commit()
        return jsonify({"error": str(e)}), 500
    finally:
        if cursor: cursor.close()
        if conn: conn.close()
        
# 6. Các Routes cho giao diện Web (Render Template)
@app.route('/')
def page_overview():
    return render_template('overview.html', title='Tổng quan')

@app.route('/sensors')
def page_sensors():
    return render_template('sensors.html', title='Dữ liệu Cảm biến')

@app.route('/devices')
def page_devices():
    return render_template('devices.html', title='Danh sách Thiết bị')

@app.route('/control')
def page_control():
    return render_template('control.html', title='Điều khiển')

@app.route('/settings')
def page_settings():
    return render_template('settings.html', title='Cài đặt')

# =========================
# 3. XỬ LÝ WEBHOOK (FULL LOGIC)
# =========================
@app.route('/api/uplink', methods=['POST'])
def handle_uplink():
    conn = None
    cursor = None
    try:
        raw = request.json
        if not raw: return jsonify({"msg": "No Data"}), 400

        conn = get_conn()
        conn.start_transaction()
        cursor = conn.cursor()

        # [1] Ghi log thô (để debug nếu cần)
        event_type = raw.get("eventType", "UPLINK")
        cursor.execute("INSERT INTO webhook_logs (event_type, raw_body) VALUES (%s, %s)", (event_type, json.dumps(raw)))

        # [2] Lấy thông tin thiết bị
        dev_info = raw.get("deviceInfo", {})
        dev_eui = dev_info.get("devEui") or raw.get("devEui") or raw.get("dev_eui")
        
        if not dev_eui:
            conn.commit()
            print("⚠️ Cảnh báo: Gói tin thiếu DevEUI")
            return jsonify({"msg": "Logged but missing DevEUI"}), 200

        # [3] Cập nhật bảng DEVICES (Luôn chạy để giữ kết nối)
        dev_name = dev_info.get("deviceName") or "Unknown"
        app_name = dev_info.get("applicationName") or "Solar App"
        
        cursor.execute("SELECT 1 FROM devices WHERE device_eui=%s", (dev_eui,))
        if cursor.fetchone():
            cursor.execute("UPDATE devices SET name=%s, application_name=%s, is_active=1, updated_at=NOW() WHERE device_eui=%s", 
                           (dev_name, app_name, dev_eui))
        else:
            cursor.execute("INSERT INTO devices (device_eui, name, application_name, is_active) VALUES (%s, %s, %s, 1)", 
                           (dev_eui, dev_name, app_name))
        
        

        # =========================================================
        # [A] XỬ LÝ DOWNLINK (LỆNH ĐIỀU KHIỂN)
        # =========================================================
        if event_type == "DOWNLINK":
            dl_data = raw.get("downlinkData", {})
            
            # 1. Lấy dữ liệu lệnh và Hex (Quan trọng)
            cmd = dl_data.get("command") or "UNKNOWN"
            if len(cmd) > 50: cmd = cmd[:50] # Cắt ngắn cho vừa DB

            hex_code = dl_data.get("payload_hex") or "" 
            if len(hex_code) > 50: hex_code = hex_code[:50]

            status = dl_data.get("status") or "SENT"
            if len(status) > 50: status = status[:50]
            
            # 2. Người tạo lệnh (Mặc định là admin vì bạn không dùng tenantName)
            creator = "admin" 

            # 3. LƯU VÀO DB (Đã có sent_at và payload_hex)
            # Lưu ý: Nếu bạn ĐÃ XÓA cột created_by trong DB, hãy bỏ "created_by" và biến "creator" trong câu lệnh dưới đây
            sql_down = """
                INSERT INTO downlink_messages 
                (device_eui, command, payload_hex, status, created_by, created_at, sent_at) 
                VALUES (%s, %s, %s, %s, %s, NOW(), NOW())
            """
            
            cursor.execute(sql_down, (dev_eui, cmd, hex_code, status, creator))
            print(f"🚀 DOWNLINK SAVED: {dev_name} | Cmd: {cmd} | Hex: {hex_code}")
        

        # =========================================================
        # [B] XỬ LÝ UPLINK (DỮ LIỆU CẢM BIẾN)
        # =========================================================
        else: 
            # Logic lấy dữ liệu từ object (như bạn đã xác nhận trước đó)
            data_obj = raw.get("object", {})
            if not data_obj: data_obj = raw.get("data", {})
            
            # --- 1. XỬ LÝ VỊ TRÍ (LOCATION UPDATE) ---
            # Tìm xem gói tin có gửi kèm tọa độ không
            lat = data_obj.get("latitude") or data_obj.get("lat")
            lon = data_obj.get("longitude") or data_obj.get("lon") or data_obj.get("lng")
            
            # Nếu có tọa độ, cập nhật vào bảng DEVICES ngay lập tức
            if lat is not None and lon is not None:
                # Lưu dưới dạng chuỗi "10.123,106.456" vào cột location
                loc_str = f"{lat},{lon}"
                cursor.execute("UPDATE devices SET location=%s WHERE device_eui=%s", (loc_str, dev_eui))
                print(f"📍 Location Update: {dev_name} -> {loc_str}")

            # --- Lưu Sensor Data ---
            val_volt = data_obj.get("bus_voltage")
            if val_volt is not None:
                # Lấy các thông số...
                val_p = data_obj.get("power_W")
                val_i = data_obj.get("current_A")
                val_eff = data_obj.get("solar_eff") or 0
                val_pm25 = data_obj.get("pm25")
                val_temp = data_obj.get("temp_c")
                val_hum = data_obj.get("humidity")
                val_bat = data_obj.get("battery_pct")
                
                vm = 1 if data_obj.get("motor_active") else 0
                vp = 1 if data_obj.get("pump_active") else 0

                sql_sensor = """
                    INSERT INTO sensor_data 
                    (device_eui, bus_voltage, current_A, power_W, solar_eff, pm25, temp_c, humidity, battery_pct, motor_status, pump_status, measured_at)
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, NOW())
                """
                cursor.execute(sql_sensor, (dev_eui, val_volt, val_i, val_p, val_eff, val_pm25, val_temp, val_hum, val_bat, vm, vp))
                print(f"✅ UPLINK SAVED: {dev_name} | P={val_p}W")

            # --- Lưu Uplink Messages (Thông số sóng) ---
            rssi = data_obj.get("rssi")
            if rssi is not None:
                snr = data_obj.get("snr")
                f_cnt = data_obj.get("fcnt") or data_obj.get("fCnt") or 0
                payload_hex = data_obj.get("data") or ""
                
                cursor.execute("INSERT INTO uplink_messages (device_eui, f_cnt, rssi, snr, payload_hex) VALUES (%s, %s, %s, %s, %s)", 
                               (dev_eui, f_cnt, rssi, snr, payload_hex))

        conn.commit()
        return jsonify({"status": "ok"}), 200

    except Exception as e:
        if conn: conn.rollback()
        # In lỗi chi tiết ra màn hình để dễ sửa
        print(f"❌ ERROR: {e}")
        return jsonify({"error": str(e)}), 500
    finally:
        if cursor: cursor.close()
        if conn: conn.close()

# API Lấy dữ liệu mới nhất
@app.route('/api/get-latest-data', methods=['GET'])
def get_latest_data():
    conn = get_conn()
    cursor = conn.cursor(dictionary=True)
    cursor.execute("SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1")
    data = cursor.fetchone()
    conn.close()
    return jsonify(data)

#4. Giao diện web đơn giản để xem dữ liệu
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/control')
def control_page():
    return render_template('control.html')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)