// Bài #2 - Nhận dạng khuôn mặt
// Dựa trên ví dụ CameraWebServer của arduino-esp32 1.0.6 (thư viện esp-face):
// core 2.0.x đã tắt face recognition trên ESP32 thường nên bài này dùng core cũ.
// Cấu hình: common/config.h (WiFi, camera) và src/lesson_config.h (ngưỡng, số ID).

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "lesson_config.h"
#include "camera_pins.h"   // common/, chọn board trong config.h

void startCameraServer();

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;  // core 1.0.6 dùng tên cũ "sscb"
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = 2;
  } else {
    // Không có PSRAM thì không đủ RAM cho face recognition
    Serial.println("WARNING: PSRAM not found, face recognition will not work");
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  // Ở core 1.0.6, giao diện web cho bật face detect/recognize khi độ phân giải <= CIF
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

  IPAddress ip = initWiFi();
  startCameraServer();

  Serial.printf("Face recognition: max %d IDs, %d samples/enroll, threshold %.2f, flash %s\n",
                FACE_ID_MAX, FACE_ENROLL_SAMPLES, FACE_RECOGNITION_THRESHOLD,
                FACE_SAVE_TO_FLASH ? "on" : "off");
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(ip);
  Serial.println("' to connect");
  Serial.print("Delete all enrolled faces: http://");
  Serial.print(ip);
  Serial.println("/control?var=face_delete_all&val=1");
}

void loop() {
  // Web server chạy trong task riêng
  delay(10000);
}
