// Bài #1 - Phát hiện khuôn mặt
// Dựa trên ví dụ CameraWebServer của arduino-esp32 2.0.17.
// Face detection được bật trong app_httpd.cpp khi có BOARD_HAS_PSRAM.
// Cấu hình: common/config.h (WiFi, camera) và src/lesson_config.h (mô hình).

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "lesson_config.h"
#include "camera_pins.h"   // common/, chọn board trong config.h

void startCameraServer();
void setupLedFlash(int pin);

static bool initCamera() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
  // Cấp phát buffer lớn (UXGA) để sau này đổi độ phân giải trên web không bị thiếu
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // stream JPEG, face detect sẽ giải nén từng khung
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.fb_count = 2;

  if (!psramFound()) {
    // Không có PSRAM thì face detection cũng bị tắt
    Serial.println("WARNING: PSRAM not found, face detection disabled");
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  // Giao diện web chỉ cho bật face detection khi độ phân giải <= QVGA (320x240)
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, CAM_FRAME_SIZE);
  s->set_vflip(s, CAM_VFLIP);
  s->set_hmirror(s, CAM_HMIRROR);
  return true;
}

// Kết nối WiFi; quá thời gian thì tự phát WiFi riêng để vẫn vào được web
static IPAddress initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  Serial.printf("Connecting to '%s'", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    return WiFi.localIP();
  }

  Serial.printf("WiFi failed, starting AP '%s' (password '%s')\n", WIFI_AP_SSID, WIFI_AP_PASSWORD);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  return WiFi.softAPIP();
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  if (!initCamera()) {
    return;
  }

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  IPAddress ip = initWiFi();
  startCameraServer();

  Serial.printf("Face detect model: %s\n", FACE_DETECT_TWO_STAGE ? "MSR01 + MNP01 (two-stage)" : "MSR01 (one-stage)");
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(ip);
  Serial.println("' to connect");
}

void loop() {
  // Web server chạy trong task riêng
  delay(10000);
}
