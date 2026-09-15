#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ---------- LoRa Pins ----------
#define LORA_SCK   33
#define LORA_MISO  26
#define LORA_MOSI  27
#define LORA_SS    14
#define LORA_RST   5
#define LORA_DIO0  2

SPIClass SPI_LORA(VSPI);

// ---------- Wi-Fi Credentials ----------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ---------- Cloud Endpoint ----------
const char* serverUrl = "https://cloud-alert-server-f1.onrender.com/alert";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================");
  Serial.println(" ESP32 LORA RECEIVER STARTING...");
  Serial.println("==================================");

  // ---------- Connect WiFi ----------
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
  }

  Serial.println("\n WiFi Connected!");
  Serial.print(" IP Address: ");
  Serial.println(WiFi.localIP());

  // ---------- Initialize LoRa ----------
  Serial.println("\n Initializing LoRa...");
  SPI_LORA.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

  LoRa.setSPI(SPI_LORA);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println(" LoRa Initialization FAILED!");
    while (1);
  }

  Serial.println(" LoRa Receiver Ready at 433MHz");
}

void loop() {

  int packetSize = LoRa.parsePacket();
  if (packetSize) {

    Serial.println("\n Packet Received!");

    String rx = LoRa.readString();

    Serial.println("----------------------------------");
    Serial.print(" RAW DATA: ");
    Serial.println(rx);
    Serial.println("----------------------------------");

    // ---------- Extract GPS ----------
    float lat = 0.0, lon = 0.0;

    if (rx.indexOf("LAT:") != -1 && rx.indexOf("LON:") != -1) {
      int latStart = rx.indexOf("LAT:") + 4;
      int commaPos = rx.indexOf(",", latStart);
      int lonStart = rx.indexOf("LON:") + 4;

      lat = rx.substring(latStart, commaPos).toFloat();
      lon = rx.substring(lonStart).toFloat();

      Serial.print(" Extracted Latitude: ");
      Serial.println(lat, 6);
      Serial.print(" Extracted Longitude: ");
      Serial.println(lon, 6);
    } else {
      Serial.println("⚠ No GPS Data Found");
    }

    // ---------- Accident Alert ----------
    if (rx.indexOf("ACCIDENT") != -1) {

      Serial.println("\n ACCIDENT ALERT RECEIVED!");

      // ---------- Send to Cloud ----------
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("🌐 Sending alert to cloud...");

        HTTPClient http;
        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/json");

        String json = "{\"message\":\"ACCIDENT detected at LAT:" 
                      + String(lat, 5) + ", LON:" 
                      + String(lon, 5) + "\"}";

        Serial.print(" JSON Sent: ");
        Serial.println(json);

        int code = http.POST(json);

        Serial.print(" Cloud Response Code: ");
        Serial.println(code);

        if (code > 0) {
          Serial.println(" Alert successfully sent!");
        } else {
          Serial.println(" Failed to send alert!");
        }

        http.end();
      }
      else {
        Serial.println(" WiFi NOT connected → Cannot send alert!");
      }
    } 
    else {
      Serial.println("ℹ Normal message (No Accident)");
    }
  }
}