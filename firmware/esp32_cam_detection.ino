/*
  ESP32-CAM Accident Detection (Object Detection) + UART Output + HTML + Bounding Boxes
  Author: ChatGPT (Custom Build)
  For AI Thinker ESP32-CAM with PSRAM
  
  Features:
  ✓ Object Detection using Edge Impulse model
  ✓ Bounding boxes overlay
  ✓ MJPEG Streaming
  ✓ UART Serial1 output -> "ACCIDENT" / "NORMAL"
  ✓ Beautiful HTML interface
  ✓ Status API (/status)
*/

#include <WiFi.h>
#include "esp_camera.h"
#include "Arduino.h"

// ==== Edge Impulse Model ====
#include <unitedhands_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

// ==== Camera Pins ====
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// ==== WiFi Access Point ====
const char* ssid = "ESP32CAM_AI";
const char* password = "12345678";

// Webserver
WiFiServer server(80);

// Last detection label (for HTML status box)
String last_detection_label = "Waiting…";

// Buffers
uint8_t* rgb_frame = nullptr;
uint8_t* ei_frame = nullptr;

static bool cameraReady = false;

// EI global pointer
uint8_t* snapshot_buf;

// Boundary for MJPEG
const char* boundary = "frame";

// ====================== DRAWING HELPERS ======================
void drawRect(uint8_t* img, int w, int h, int x, int y, int bw, int bh,
              uint8_t r, uint8_t g, uint8_t b, int t = 2) {
  if (bw <= 0 || bh <= 0) return;

  // Clip
  if (x < 0) { bw += x; x = 0; }
  if (y < 0) { bh += y; y = 0; }
  if (x + bw > w) bw = w - x;
  if (y + bh > h) bh = h - y;

  for (int i = 0; i < t; i++) {
    // top
    for (int xx = x; xx < x + bw; xx++) {
      int idx = ((y + i) * w + xx) * 3;
      img[idx] = r; img[idx + 1] = g; img[idx + 2] = b;
    }
    // bottom
    for (int xx = x; xx < x + bw; xx++) {
      int idx = ((y + bh - 1 - i) * w + xx) * 3;
      img[idx] = r; img[idx + 1] = g; img[idx + 2] = b;
    }
    // left
    for (int yy = y; yy < y + bh; yy++) {
      int idx = (yy * w + x + i) * 3;
      img[idx] = r; img[idx + 1] = g; img[idx + 2] = b;
    }
    // right
    for (int yy = y; yy < y + bh; yy++) {
      int idx = (yy * w + x + bw - 1 - i) * 3;
      img[idx] = r; img[idx + 1] = g; img[idx + 2] = b;
    }
  }
}

// Simple label box
void drawLabel(uint8_t* img, int w, int h, int x, int y,
               String txt, uint8_t r, uint8_t g, uint8_t b) {
  int W = txt.length() * 7 + 8;
  int H = 16;

  if (x + W > w) x = w - W - 2;
  if (y + H > h) y = h - H - 2;

  for (int yy = y; yy < y + H; yy++) {
    for (int xx = x; xx < x + W; xx++) {
      int idx = (yy * w + xx) * 3;
      img[idx] = r * 0.2;
      img[idx + 1] = g * 0.2;
      img[idx + 2] = b * 0.2;
    }
  }

  drawRect(img, w, h, x, y, W, H, r, g, b, 2);
}

// ====================== CAMERA INITIALIZATION ======================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  return esp_camera_init(&config) == ESP_OK;
}

// ====================== EI GET DATA ======================
extern "C" {
  int ei_camera_get_data(size_t offset, size_t length, float* out_ptr) {
    size_t ix = offset * 3;
    for (size_t i = 0; i < length; i++) {
      out_ptr[i] = (ei_frame[ix + 2] << 16) |
                   (ei_frame[ix + 1] << 8) |
                    ei_frame[ix];
      ix += 3;
    }
    return 0;
  }
}

// ====================== CAPTURE & INFERENCE ======================
bool runDetection(uint8_t** out_jpg, size_t* out_len) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;

  int W = fb->width;
  int H = fb->height;

  size_t rgbSize = W * H * 3;
  rgb_frame = (uint8_t*)malloc(rgbSize);
  if (!rgb_frame) return false;

  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_frame);
  esp_camera_fb_return(fb);

  if (!ok) return false;

  int eiW = EI_CLASSIFIER_INPUT_WIDTH;
  int eiH = EI_CLASSIFIER_INPUT_HEIGHT;

  ei_frame = (uint8_t*)malloc(eiW * eiH * 3);
  if (!ei_frame) return false;

  ei::image::processing::crop_and_interpolate_rgb888(
    ei_frame, W, H, rgb_frame, eiW, eiH);

  snapshot_buf = ei_frame;

  ei_impulse_result_t result;
  ei::signal_t signal;
  signal.total_length = eiW * eiH;
  signal.get_data = &ei_camera_get_data;

  run_classifier(&signal, &result, false);

  // Best label
  float best = 0;
  String bestLabel = "normal";
  int x=0,y=0,w=0,h=0;
  uint8_t R=0, G=255, B=0;

  for (int i = 0; i < result.bounding_boxes_count; i++) {
    auto bb = result.bounding_boxes[i];
    if (bb.value > best) {
      best = bb.value;
      bestLabel = String(bb.label);
      x = bb.x; y = bb.y; w = bb.width; h = bb.height;
    }
  }

  // Map EI input -> real image
  float sx = (float)W / eiW;
  float sy = (float)H / eiH;

  x = x * sx;
  y = y * sy;
  w = w * sx;
  h = h * sy;

  bestLabel += " (" + String(best,2) + ")";
  last_detection_label = bestLabel;

  // Draw box
  if (bestLabel.indexOf("accident") >= 0) {
    R=255; G=40; B=40;
  }

  drawRect(rgb_frame, W, H, x, y, w, h, R,G,B,3);
  drawLabel(rgb_frame, W, H, x, y-18, bestLabel, R,G,B);

  // UART output
  if (bestLabel.indexOf("accident") >= 0) {
      Serial1.println("ACCIDENT");
      Serial.println("ACCIDENT Sent via UART");
  } else {
      Serial1.println("NORMAL");
      Serial.println("NORMAL Sent via UART");
  }

  // Encode JPG
  frame2jpg(rgb_frame, rgbSize, W, H, PIXFORMAT_RGB888, 90, out_jpg, out_len);

  free(rgb_frame);
  free(ei_frame);

  return true;
}

// ====================== HTML PAGE ======================
void sendHTML(WiFiClient& client) {
  String page =
  "<html><head><title>Accident Detection</title>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<style>"
  "body{background:#111;color:#fff;text-align:center;font-family:Arial;}"
  ".header{padding:15px;font-size:22px;color:#00eaff;border-bottom:2px solid #00eaff;}"
  "#stream{width:96%;max-width:480px;border:2px solid #00eaff;border-radius:10px;}"
  ".status{margin-top:12px;padding:10px;background:#181818;border:1px solid #444;border-radius:10px;}"
  "</style></head>"
  "<body>"
  "<div class='header'>🚗 ESP32-CAM Accident Detection</div>"
  "<img id='stream' src='/stream'>"
  "<div class='status' id='stat'>Loading...</div>"
  "<script>"
  "setInterval(()=>{fetch('/status').then(r=>r.text()).then(t=>{document.getElementById('stat').innerHTML=t;});},1000);"
  "</script>"
  "</body></html>";

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println();
  client.print(page);
}

// ====================== STREAM HANDLER ======================
void handleStream(WiFiClient& client) {
  client.printf("HTTP/1.1 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n\r\n",
                boundary);

  while (client.connected()) {
    uint8_t* jpg;
    size_t len;

    if (!runDetection(&jpg, &len)) continue;

    client.printf("--%s\r\n", boundary);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %u\r\n\r\n", (unsigned)len);
    client.write(jpg, len);
    client.print("\r\n");

    free(jpg);
    delay(50);
  }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 3, 1);   // UART for accident transmission

  if (!initCamera()) {
    Serial.println("Camera init failed!");
    while (1);
  }

  WiFi.softAP(ssid, password);
  Serial.println("AP Ready:");
  Serial.println(WiFi.softAPIP());

  server.begin();
}

// ====================== LOOP ======================
void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  String req = client.readStringUntil('\r');
  while (client.available()) client.read(); // clear header

  if (req.indexOf("GET /stream") >= 0) {
    handleStream(client);
  }
  else if (req.indexOf("GET /status") >= 0) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println();
    client.print(last_detection_label);
  }
  else {
    sendHTML(client);
  }

  client.stop();
}