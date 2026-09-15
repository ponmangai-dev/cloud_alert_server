// ESP32 MAIN (TRANSMITTER)
// - Read ESP32-CAM status via UART1 (RX_CAM=GPIO4)
// - GPS on UART2 (RX2=16, TX2=17) using TinyGPSPlus
// - ADXL345 on I2C (SDA=21, SCL=22)
// - LoRa send packet
//
// Wiring summary:
// - CAM GPIO4(TX) -> MAIN GPIO4(RX1)
// - CAM GPIO15(RX) <- MAIN GPIO15(TX1)   (TX not required if CAM only sends)
// - GPS TX -> MAIN GPIO16 (RX2), GPS RX -> MAIN GPIO17 (TX2)
// - ADXL345 SDA->21, SCL->22, VCC->3.3V, GND->GND
// - LoRa: SCK=33, MISO=26, MOSI=27, SS=14, RST=5, DIO0=2
// - GNDs connected

#include <Wire.h>
#include <TinyGPSPlus.h>
#include <Adafruit_ADXL345_U.h>
#include <SPI.h>
#include <LoRa.h>

#define RX_CAM 33    // read from ESP32-CAM TX (GPIO4)
#define TX_CAM 32   // (optional CAM RX)
HardwareSerial SerialCAM(1); // UART1 to read camera status

// GPS on UART2
#define GPS_RX 16   // GPS TX -> ESP32 RX2
#define GPS_TX 17   // GPS RX -> ESP32 TX2
HardwareSerial SerialGPS(2);
TinyGPSPlus gps;

// ADXL345
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// LoRa pins (SX127x)
#define LORA_SCK 25
#define LORA_MISO 26
#define LORA_MOSI 27
#define LORA_SS 14
#define LORA_RST 5
#define LORA_DIO0 2
SPIClass SPI_LORA(VSPI);

String camStatus = "NORMAL";
double lat = 0.0, lon = 0.0;
float ax=0, ay=0, az=0;

void setup() {
  Serial.begin(115200);
  delay(200);

  // I2C for ADXL345
  Wire.begin(21, 22);
  if (!accel.begin()) {
    Serial.println("ADXL345 not found!");
    // continue but values will be zero
  } else {
    accel.setRange(ADXL345_RANGE_16_G);
  }

  // Serial1: read CAM on RX_CAM
  SerialCAM.begin(9600, SERIAL_8N1, RX_CAM, TX_CAM); // RX pin then TX pin

  // SerialGPS UART2
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  // LoRa init
  SPI_LORA.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setSPI(SPI_LORA);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if(!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    while(1);
  }

  Serial.println("MAIN ESP32 (transmitter) ready");
}

void loop() {
  // 1) Read camera status if available
  if (SerialCAM.available()) {
    camStatus = SerialCAM.readStringUntil('\n');
    camStatus.trim();
    if (camStatus.length() == 0) camStatus = "NORMAL";
  }

  // 2) Read GPS stream
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }
  if (gps.location.isValid()) {
    lat = gps.location.lat();
    lon = gps.location.lng();
  }

  // 3) Read ADXL345
  sensors_event_t event;
  accel.getEvent(&event);
  ax = event.acceleration.x;
  ay = event.acceleration.y;
  az = event.acceleration.z;

  // 4) Print debug locally
  Serial.println("==== LIVE DATA ====");
  Serial.println("CAM STATUS: " + camStatus);
  Serial.printf("GPS: %.6f , %.6f\n", lat, lon);
  Serial.printf("ACC: X=%.2f Y=%.2f Z=%.2f\n", ax, ay, az);

  // 5) Build LoRa packet
  String packet = "";
  packet += "STATUS:" + camStatus;
  packet += ",LAT:" + String(lat, 6);
  packet += ",LON:" + String(lon, 6);
  packet += ",AX:" + String(ax, 2);
  packet += ",AY:" + String(ay, 2);
  packet += ",AZ:" + String(az, 2);

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  Serial.print("LoRa sent: "); Serial.println(packet);

  delay(800); // adjust send rate as needed
}