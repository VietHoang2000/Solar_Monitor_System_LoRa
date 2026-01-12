/******************* HELTEC ESP32 LoRa V3.x – SOLAR PANEL NODE (DUAL OLED FINAL) *******************
 * Downlink (FPort 50):
 * A0        = STOP tất cả
 * A1        = CLEAN mặc định (THỦ CÔNG)
 * A3 <ss>   = CLEAN <ss> giây
 * B0        = Pump OFF
 * B1        = Pump ON
 * C0        = Auto-clean OFF
 * C1        = Auto-clean ON
 *
 * Uplink (FPort 50): 15 Bytes (RAW DATA)
 * Byte 0-1:   U_mv (mV)
 * Byte 2-3:   I_mA (mA) - Signed
 * Byte 4-5:   P_mW (mW)  <-- Gửi mW (số nguyên) để chính xác
 * Byte 6-7:   T_c10 (Tem * 10)
 * Byte 8-9:   RH_p10 (Hum * 10)
 * Byte 10-11: PM2.5 (ug/m3)
 * Byte 12-13: Batt_mV (mV)
 * Byte 14:    Status
 **************************************************************************************************/

#include "LoRaWan_APP.h"
#include "DHT.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "DFRobot_INA219.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <L298N.h>

//====================== CẤU HÌNH PHẦN CỨNG ======================
#define DHTPIN      2
#define DHTTYPE     DHT22
#define EN_PIN      46
#define BAT_PIN     1      // Chân ADC đọc pin từ mạch DFRobot
#define DUST_LED_PIN    4
#define DUST_ANALOG_PIN 3

// I2C Mở rộng (Cho INA219 và OLED rời)
#define SDA_EXT     19
#define SCL_EXT     20

#define RELAY_PUMP    38        // Relay máy bơm
#define RELAY_MOTOR   39        // Relay cấp nguồn L298N

// L298N (kênh B)
#define ENA 5
#define IN1 41
#define IN2 42

//====================== ĐỐI TƯỢNG ===============================
DHT dht(DHTPIN, DHTTYPE);

// 1. Màn hình tích hợp Heltec (Bus 0 mặc định)
SSD1306Wire display1(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// 2. Bus I2C mở rộng (Bus 1)
TwoWire I2C_EXT = TwoWire(1);

// 3. INA219 trên Bus I2C mở rộng
DFRobot_INA219_IIC ina219(&I2C_EXT, INA219_I2C_ADDRESS4);

// 4. Màn hình OLED rời trên Bus I2C mở rộng
Adafruit_SSD1306 display2(128, 64, &I2C_EXT, -1);

L298N motorB(ENA, IN1, IN2);

// === THAM SỐ HIỆU CHỈNH CẢM BIẾN DÒNG ĐIỆN ===
float ina219Reading_mA   = 23.0;   
float extMeterReading_mA = 22.65;  

//====================== HẰNG SỐ & BIẾN =========================
const float DUST_THRESHOLD    = 50.0;   // µg/m3

float prevDust  = 0;
bool  needClean        = false;
bool  autoCleanEnabled = true;
bool  forceClean       = false;
uint16_t forceCleanSec = 0;

// Manual brush control
bool    manualBrushEnabled       = false;
uint8_t manualBrushPhase         = 0;
int8_t  manualBrushDir           = 0;
uint32_t manualBrushLastTs       = 0;
bool    manualBrushStopRequested = false;

// ---- THÔNG SỐ MOTOR ----
const uint8_t  BRUSH_SPEED       = 160;
const uint16_t BRUSH_FORWARD_MS  = 1800;
const uint16_t BRUSH_BACKWARD_MS = 2250;
const uint16_t BRUSH_PAUSE_MS    = 1000;

//====================== OTAA CREDENTIALS =======================
uint8_t devEui[] = { 0xD7, 0xA9, 0x2B, 0x1F, 0x77, 0xCE, 0xD2, 0xE0 };
uint8_t appEui[] = { 0x19, 0x98, 0x63, 0x5F, 0xBC, 0xE1, 0x74, 0x69 };
uint8_t appKey[] = { 0x67, 0xA5, 0x58, 0x1A, 0x02, 0x29, 0x2A, 0xE5,
                     0x13, 0xD5, 0x0B, 0x86, 0x43, 0x8D, 0xB8, 0x35 };

uint8_t nwkSKey[] = { 0x15,0xb1,0xd0,0xef,0xa4,0x63,0xdf,0xbe,0x3d,0x11,0x18,0x1e,0x1e,0xc7,0xda,0x85 };
uint8_t appSKey[] = { 0xd7,0x2c,0x78,0x75,0x8c,0xdc,0xca,0xbf,0x55,0xee,0x4a,0x77,0x8d,0x16,0xef,0x67 };
uint32_t devAddr = (uint32_t)0x007e6ae1;

//====================== LoRaWAN SETTINGS =======================
uint16_t userChannelsMask[6]={ 0x00FF,0x0000,0x0000,0x0000,0x0000,0x0000 };
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;
DeviceClass_t   loraWanClass  = CLASS_C;
bool overTheAirActivation     = true;
bool loraWanAdr               = true;
bool isTxConfirmed            = false;
uint8_t appPort               = 50;

const uint32_t NORMAL_TX_INTERVAL_MS = 30000; // 30s
const uint32_t FAST_TX_INTERVAL_MS   = 500;   // 0.5s sau lệnh

uint32_t appTxDutyCycle       = NORMAL_TX_INTERVAL_MS;
uint8_t  confirmedNbTrials    = 4;
bool pendingFastUplink = false;

//====================== HÀM PHỤ TRỢ ============================
static inline void put16(uint8_t *b, uint16_t v){ b[0] = v >> 8; b[1] = v & 0xFF; }
static inline void put16s(uint8_t *b, int16_t v){ b[0] = (uint16_t)v >> 8; b[1] = (uint16_t)v & 0xFF; }
void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }
float smooth(float newValue, float prevValue, float alpha = 0.2) {
  return alpha * newValue + (1 - alpha) * prevValue;
}

//====================== LOGIC VỆ SINH ==========================
void cleanSolarPanelCore(bool withPump, uint16_t seconds) {
  Serial.println("🚿 Cleaning Cycle...");
  if (withPump) {
    uint16_t pump_s = (seconds > 0) ? seconds : 5;
    if (pump_s > 20) pump_s = 20;
    digitalWrite(RELAY_PUMP, HIGH);
    delay(pump_s * 1000UL);
    digitalWrite(RELAY_PUMP, LOW);
    delay(1000);
  }
  digitalWrite(RELAY_MOTOR, HIGH);
  delay(200);
  if (seconds > 0) {
    uint32_t remain = (uint32_t)seconds * 1000UL;
    motorB.forward(); motorB.setSpeed(BRUSH_SPEED); delay(remain / 2);
    motorB.backward(); motorB.setSpeed(BRUSH_SPEED); delay(remain - (remain / 2));
  } else {
    for (int i = 0; i < 3; i++) {
      motorB.forward(); motorB.setSpeed(BRUSH_SPEED); delay(BRUSH_FORWARD_MS);
      motorB.stop(); delay(BRUSH_PAUSE_MS);
      motorB.backward(); motorB.setSpeed(BRUSH_SPEED); delay(BRUSH_BACKWARD_MS);
      motorB.stop(); delay(BRUSH_PAUSE_MS);
    }
  }
  motorB.stop();
  digitalWrite(RELAY_MOTOR, LOW);
  Serial.println("✅ Done.");
}

void manualBrushStopAll() {
  motorB.stop();
  digitalWrite(RELAY_MOTOR, LOW);
  manualBrushEnabled = false; manualBrushStopRequested = false;
}

void updateManualBrush() {
  if (!manualBrushEnabled) {
    if (manualBrushDir != 0) manualBrushStopAll();
    return;
  }
  uint32_t now = millis();
  switch (manualBrushPhase) {
    case 0: // BACKWARD
      if (manualBrushDir != -1) {
        digitalWrite(RELAY_MOTOR, HIGH);
        motorB.backward(); motorB.setSpeed(BRUSH_SPEED);
        manualBrushDir = -1; manualBrushLastTs = now;
      } else if (now - manualBrushLastTs >= BRUSH_BACKWARD_MS) {
        motorB.stop(); manualBrushDir = 0; manualBrushPhase = 1; manualBrushLastTs = now;
      }
      break;
    case 1: // PAUSE
      if (now - manualBrushLastTs >= BRUSH_PAUSE_MS) {
        digitalWrite(RELAY_MOTOR, HIGH);
        motorB.forward(); motorB.setSpeed(BRUSH_SPEED);
        manualBrushDir = 1; manualBrushPhase = 2; manualBrushLastTs = now;
      }
      break;
    case 2: // FORWARD
      if (now - manualBrushLastTs >= BRUSH_FORWARD_MS) {
        motorB.stop(); manualBrushDir = 0; manualBrushPhase = 3; manualBrushLastTs = now;
      }
      break;
    case 3: // PAUSE & CHECK
      if (now - manualBrushLastTs >= BRUSH_PAUSE_MS) {
        if (manualBrushStopRequested) manualBrushStopAll();
        else manualBrushPhase = 0;
      }
      break;
  }
}

//====================== DOWNLINK HANDLER =======================
extern "C" void downLinkDataHandle(McpsIndication_t *mcpsIndication) {
  uint8_t port = mcpsIndication->Port;
  uint8_t *b   = mcpsIndication->Buffer;
  uint8_t len  = mcpsIndication->BufferSize;
  if (port != 50 || len == 0) return;
  
  switch (b[0]) {
    case 0xA0: manualBrushStopAll(); digitalWrite(RELAY_PUMP, LOW); forceClean=false; needClean=false; pendingFastUplink=true; break;
    case 0xA1: manualBrushEnabled=true; manualBrushStopRequested=false; manualBrushPhase=0; pendingFastUplink=true; break;
    case 0xA2: if (manualBrushEnabled) manualBrushStopRequested=true; pendingFastUplink=true; break;
    case 0xB0: digitalWrite(RELAY_PUMP, LOW); pendingFastUplink=true; break;
    case 0xB1: digitalWrite(RELAY_PUMP, HIGH); pendingFastUplink=true; break;
    case 0xC0: autoCleanEnabled=false; pendingFastUplink=true; break;
    case 0xC1: autoCleanEnabled=true; pendingFastUplink=true; break;
  }
}

//====================== PREPARE PAYLOAD (RAW DATA) =============
static void prepareTxFrame(uint8_t port) {
  // 1. Đọc INA219 (Dữ liệu nguyên bản)
  float bus_V      = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  float power_mW   = ina219.getPower_mW(); 

  // 2. Đọc Pin (mV)
  int batRaw = analogRead(BAT_PIN);
  const float CAL_FACTOR = 1.25; 
  float bat_V = (batRaw / 4095.0f) * 3.3f * CAL_FACTOR;
  
  // 3. Đọc môi trường
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();

  // 4. Đọc Bụi
  digitalWrite(DUST_LED_PIN, LOW); delayMicroseconds(280);
  int dustRaw = analogRead(DUST_ANALOG_PIN); delayMicroseconds(40);
  digitalWrite(DUST_LED_PIN, HIGH); delayMicroseconds(9680);
  float dustVoltage = (dustRaw / 4095.0f) * 3.3f;
  float dustDensity = (dustVoltage - 0.9f) * 1000.0f / 5.0f;
  if (dustDensity < 0) dustDensity = 0;
  dustDensity = smooth(dustDensity, prevDust); prevDust = dustDensity;
  needClean = (dustDensity >= DUST_THRESHOLD);

  // 5. HIỂN THỊ OLED

  // ============= MÀN HÌNH HELTEC TÍCH HỢP (DISPLAY 1) =============
  display1.clear();
  // Dòng 1: Trạng thái hệ thống (Clean/Pump/Idle)
  String stt = "IDLE";
  if(motorB.getSpeed() > 0) stt = "CLEANING";
  else if(digitalRead(RELAY_PUMP)) stt = "PUMPING";
  display1.drawString(0, 0, "St: " + stt);
  
  // Dòng 2: 1 byte cuối của DevEUI để nhận diện
  display1.drawString(0, 16, "ID: ..." + String(devEui[7], HEX));
    
  // Dòng 3: Nhiệt độ & Độ ẩm
  display1.drawString(0, 32, "T: " + String(temp, 1) + "C  H: " + String(humi, 0) + "%");
  
  // Dòng 4: Bụi PM2.5
  display1.drawString(0, 48, "PM2.5: " + String(dustDensity, 0) + " ug");
  
  display1.display();

  // B. OLED Rời (Chi tiết thông số điện)
  display2.clearDisplay();
  display2.setCursor(0,0);
  display2.printf("U: %.2fV  I: %.0fmA\n", bus_V, current_mA);
  display2.setCursor(0,16);
  display2.printf("P: %.0fmW\n", power_mW);
  display2.setCursor(0,32);
  display2.printf("Bat: %.2fV\n", bat_V);
  display2.display();

  // 6. ĐÓNG GÓI PAYLOAD (Gửi số nguyên Raw)
  uint16_t U_mV_send   = (uint16_t)(bus_V * 1000.0f);
  int16_t  I_mA_send   = (int16_t) current_mA;
  uint16_t P_mW_send   = (uint16_t) power_mW; 
  int16_t  T_val       = (int16_t) (isnan(temp) ? 0 : temp * 10.0f);
  uint16_t RH_val      = (uint16_t)(isnan(humi) ? 0 : humi * 10.0f);
  uint16_t PM_val      = (uint16_t)dustDensity;
  uint16_t Bat_mV_send = (uint16_t)(bat_V * 1000.0f);

  uint8_t status = 0;
  if (digitalRead(RELAY_MOTOR)) status |= 0x01;
  if (digitalRead(RELAY_PUMP))  status |= 0x02;
  if (needClean)                status |= 0x04;
  if (autoCleanEnabled)         status |= 0x08;

  appDataSize = 15;
  int idx = 0;
  put16(&appData[idx], U_mV_send);    idx+=2;
  put16s(&appData[idx], I_mA_send);   idx+=2; 
  put16(&appData[idx], P_mW_send);    idx+=2; 
  put16s(&appData[idx], T_val);       idx+=2;
  put16(&appData[idx], RH_val);       idx+=2;
  put16(&appData[idx], PM_val);       idx+=2;
  put16(&appData[idx], Bat_mV_send);  idx+=2;
  appData[idx] = status;

  Serial.printf("UP RAW: UmV=%u, ImA=%d, PmW=%u, BatmV=%u\n", U_mV_send, I_mA_send, P_mW_send, Bat_mV_send);
}

//====================== SETUP ==================================
void setup() {
  Serial.begin(115200);
  dht.begin();
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  VextON(); delay(100);

  // Init Heltec OLED
  display1.init(); display1.clear(); display1.drawString(0,0,"Booting..."); display1.display();

  pinMode(EN_PIN, OUTPUT); digitalWrite(EN_PIN, HIGH);
  pinMode(BAT_PIN, INPUT);
  pinMode(DUST_LED_PIN, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT); digitalWrite(RELAY_PUMP, LOW);
  pinMode(RELAY_MOTOR, OUTPUT); digitalWrite(RELAY_MOTOR, LOW);

  // Init I2C Ext
  I2C_EXT.begin(SDA_EXT, SCL_EXT, 30000); 
  
  // Init INA219
  Serial.print("Init INA219... ");
  if(ina219.begin() != true) {
      Serial.println("FAILED!");
      display1.drawString(0, 16, "INA219 Fail"); display1.display();
  } else {
      Serial.println("OK");
      // AP DỤNG HIỆU CHỈNH
      ina219.linearCalibrate(ina219Reading_mA, extMeterReading_mA);
      Serial.println("Calibrated.");
  }

  // Init OLED Rời
  if (!display2.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED 2 not found");
  } else {
    display2.clearDisplay(); display2.setTextSize(1); display2.setTextColor(SSD1306_WHITE);
    display2.setCursor(0,0); display2.println("Monitor Ready"); display2.display();
  }

  motorB.setSpeed(0); motorB.stop();
  Serial.println("SYSTEM READY");
}

//====================== LOOP ===================================
void loop() {
  switch (deviceState) {
    case DEVICE_STATE_INIT:
      LoRaWAN.init(loraWanClass, loraWanRegion);
      LoRaWAN.setDefaultDR(3);
      deviceState = DEVICE_STATE_JOIN;
      break;
    case DEVICE_STATE_JOIN:
      LoRaWAN.join();
      break;
    case DEVICE_STATE_SEND:
      prepareTxFrame(appPort);
      LoRaWAN.send();
      deviceState = DEVICE_STATE_CYCLE;
      break;
    case DEVICE_STATE_CYCLE:
      if (forceClean) {
        cleanSolarPanelCore(false, forceCleanSec);
        forceClean = false; forceCleanSec = 0;
      }
      else if (autoCleanEnabled && needClean && !manualBrushEnabled) {
        cleanSolarPanelCore(true, 0);
        needClean = false;
      }
      if (pendingFastUplink) {
        txDutyCycleTime = FAST_TX_INTERVAL_MS;
        appTxDutyCycle = NORMAL_TX_INTERVAL_MS;
        pendingFastUplink = false;
      } else {
        appTxDutyCycle = NORMAL_TX_INTERVAL_MS;
        txDutyCycleTime = appTxDutyCycle + randr(-APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND);
      }
      LoRaWAN.cycle(txDutyCycleTime);
      deviceState = DEVICE_STATE_SLEEP;
      break;
    case DEVICE_STATE_SLEEP:
      LoRaWAN.sleep(loraWanClass);
      break;
    default:
      deviceState = DEVICE_STATE_INIT;
      break;
  }
  updateManualBrush();
}
