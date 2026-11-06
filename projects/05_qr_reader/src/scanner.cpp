#include "scanner.h"

#include <Arduino.h>
#include "camera_init.h"  // common/esp32cam
#include "quirc/quirc.h"  // trong thư viện ESP32QRCodeReader
#include "lesson_config.h"

static QueueHandle_t result_queue = nullptr;
static QrFrameCallback frame_callback = nullptr;

static portMUX_TYPE stats_mux = portMUX_INITIALIZER_UNLOCKED;
static QrStats stats = {};

// Cấu hình camera hiện tại và yêu cầu đổi (từ web), chỉ task quét áp dụng
static volatile framesize_t cur_size = QR_FRAME_SIZE, req_size = QR_FRAME_SIZE;
static volatile int cur_contrast = -100, req_contrast = QR_CONTRAST;  // -100: chưa đặt, áp dụng ở khung đầu

static void applyCameraRequests() {
  if (req_size != cur_size) {
    framesize_t size = req_size;
    Serial.printf("Scanner: reinit camera %dx%d\n", resolution[size].width, resolution[size].height);
    esp_camera_deinit();
    if (!cameraInit(PIXFORMAT_GRAYSCALE, size, 2, FRAMESIZE_INVALID, QR_XCLK_HZ)) {
      // Không đủ bộ nhớ chẳng hạn: quay về cỡ cũ
      size = cur_size;
      req_size = size;
      esp_camera_deinit();
      cameraInit(PIXFORMAT_GRAYSCALE, size, 2, FRAMESIZE_INVALID, QR_XCLK_HZ);
    }
    cur_size = size;
    cur_contrast = -100;  // camera mới: đặt lại tương phản
  }
  if (req_contrast != cur_contrast) {
    sensor_t *s = esp_camera_sensor_get();
    if (s && s->set_contrast(s, req_contrast) == 0) {
      cur_contrast = req_contrast;
    }
  }
}

void scannerSetFrameSize(framesize_t size) {
  req_size = size;
}

void scannerSetContrast(int contrast) {
  req_contrast = contrast;
}

framesize_t scannerFrameSize() {
  return cur_size;
}

int scannerContrast() {
  return cur_contrast;
}

static void scannerTask(void *) {
  struct quirc *q = quirc_new();
  if (!q) {
    Serial.println("quirc_new failed");
    vTaskDelete(nullptr);
    return;
  }
  // Lớn (~4 KB và ~9 KB) nên để static thay vì trên stack
  static struct quirc_code code;
  static struct quirc_data data;

  int w = 0, h = 0;
  uint32_t win_start = millis(), win_frames = 0, win_coded = 0;
  uint64_t win_identify = 0, win_decode = 0;

  for (;;) {
    applyCameraRequests();
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    // Đổi độ phân giải trên web → cấp lại bộ nhớ cho quirc
    if (fb->width != w || fb->height != h) {
      if (quirc_resize(q, fb->width, fb->height) < 0) {
        Serial.printf("quirc_resize %dx%d failed (out of memory)\n", fb->width, fb->height);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      w = fb->width;
      h = fb->height;
      Serial.printf("Scanner: %dx%d grayscale\n", w, h);
    }

    uint32_t t0 = micros();
    uint8_t *image = quirc_begin(q, nullptr, nullptr);
    memcpy(image, fb->buf, w * h);
    quirc_end(q);
    uint32_t t1 = micros();

    QrCorners corners = {};
    int count = quirc_count(q);
    uint32_t decode_total = 0;
    for (int i = 0; i < count && i < QR_MAX_CODES; i++) {
      quirc_extract(q, i, &code);
      for (int k = 0; k < 4; k++) {
        corners.pts[i][2 * k] = code.corners[k].x;
        corners.pts[i][2 * k + 1] = code.corners[k].y;
      }
      corners.count++;

      uint32_t d0 = micros();
      quirc_decode_error_t err = quirc_decode(&code, &data);
      QrResult r = {};
      r.decode_us = micros() - d0;
      decode_total += r.decode_us;
      if (err) {
        r.valid = false;
        strncpy(r.text, quirc_strerror(err), QR_TEXT_MAX);
      } else {
        r.valid = true;
        r.len = data.payload_len;
        int n = data.payload_len < QR_TEXT_MAX ? data.payload_len : QR_TEXT_MAX;
        memcpy(r.text, data.payload, n);
        r.text[n] = '\0';
        r.version = data.version;
        r.ecc = "MLHQ"[data.ecc_level & 3];  // thứ tự theo QUIRC_ECC_LEVEL_*
      }
      xQueueSend(result_queue, &r, 0);  // queue đầy thì bỏ, loop() đang bận
      portENTER_CRITICAL(&stats_mux);
      if (r.valid) stats.decoded++;
      else stats.failed++;
      portEXIT_CRITICAL(&stats_mux);
    }

    if (frame_callback) {
      frame_callback(fb, corners);
    }
    esp_camera_fb_return(fb);

    win_frames++;
    win_identify += t1 - t0;
    if (count) {
      win_coded++;
      win_decode += decode_total;
    }
    uint32_t now = millis();
    portENTER_CRITICAL(&stats_mux);
    if (count) stats.frames_with_code++;
    if (now - win_start >= 1000) {
      stats.fps = win_frames * 1000.0f / (now - win_start);
      stats.identify_us = win_identify / win_frames;
      stats.decode_us = win_coded ? win_decode / win_coded : 0;
      stats.width = w;
      stats.height = h;
    }
    portEXIT_CRITICAL(&stats_mux);
    if (now - win_start >= 1000) {
      win_start = now;
      win_frames = win_coded = 0;
      win_identify = win_decode = 0;
    }
  }
}

bool scannerStart(QueueHandle_t results, QrFrameCallback on_frame) {
  result_queue = results;
  frame_callback = on_frame;
  // Core 1 cùng loop(); WiFi chạy ở core 0
  return xTaskCreatePinnedToCore(scannerTask, "qr_scanner", QR_TASK_STACK, nullptr, 5, nullptr, 1) == pdPASS;
}

QrStats scannerStats() {
  portENTER_CRITICAL(&stats_mux);
  QrStats s = stats;
  portEXIT_CRITICAL(&stats_mux);
  return s;
}
