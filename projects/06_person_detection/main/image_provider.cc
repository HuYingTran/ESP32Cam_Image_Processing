#include "image_provider.h"

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lesson_config.h"
#include "camera_pins.h"  // common/camera_pins.h, sau lesson_config.h: cần CAMERA_MODEL_*
#include "model_settings.h"

static const char *TAG = "image";

bool imageProviderInit() {
  camera_config_t config = {};
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
  config.xclk_freq_hz = PD_XCLK_HZ;
  // Model nhận ảnh xám: chụp thẳng GRAYSCALE, khỏi đổi từ RGB565 hay giải nén JPEG
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = PD_FRAME_SIZE;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  // Luôn lấy khung mới nhất: suy luận chậm hơn tốc độ camera, khung cũ không còn ý nghĩa
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) {
    ESP_LOGW(TAG, "PSRAM not found, using 1 frame buffer in DRAM");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, PD_VFLIP);
  s->set_hmirror(s, PD_HMIRROR);
  return true;
}

// Cắt hình vuông ở giữa khung w x h rồi thu nhỏ về 96x96: mỗi điểm đích là trung bình
// một ô vuông điểm nguồn (giảm nhiễu, không méo tỉ lệ như kéo giãn cả khung)
static void cropResize(const uint8_t *src, int w, int h, int8_t *dst) {
  int side = w < h ? w : h;
  int x0 = (w - side) / 2;
  int y0 = (h - side) / 2;
  for (int oy = 0; oy < kNumRows; oy++) {
    int sy0 = y0 + oy * side / kNumRows;
    int sy1 = y0 + (oy + 1) * side / kNumRows;
    for (int ox = 0; ox < kNumCols; ox++) {
      int sx0 = x0 + ox * side / kNumCols;
      int sx1 = x0 + (ox + 1) * side / kNumCols;
      uint32_t sum = 0;
      for (int y = sy0; y < sy1; y++) {
        const uint8_t *row = src + y * w;
        for (int x = sx0; x < sx1; x++) {
          sum += row[x];
        }
      }
      uint8_t v = sum / ((sy1 - sy0) * (sx1 - sx0));
      // Đầu vào model là int8 (zero point -128): uint8 - 128, tương đương đảo bit cao
      dst[oy * kNumCols + ox] = (int8_t)(v ^ 0x80);
    }
  }
}

bool imageProviderGet(int8_t *dst, ImageTiming *timing) {
  int64_t t0 = esp_timer_get_time();
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGE(TAG, "Camera capture failed");
    return false;
  }
  int64_t t1 = esp_timer_get_time();

  bool ok = true;
  if (fb->width == kNumCols && fb->height == kNumRows) {
    for (int i = 0; i < kMaxImageSize; i++) {
      dst[i] = (int8_t)(fb->buf[i] ^ 0x80);
    }
  } else if ((int)fb->width >= kNumCols && (int)fb->height >= kNumRows) {
    cropResize(fb->buf, fb->width, fb->height, dst);
  } else {
    ESP_LOGE(TAG, "Frame %dx%d smaller than %dx%d", fb->width, fb->height, kNumCols, kNumRows);
    ok = false;
  }
  esp_camera_fb_return(fb);

  if (timing) {
    timing->capture_us = t1 - t0;
    timing->preprocess_us = esp_timer_get_time() - t1;
  }
  return ok;
}
