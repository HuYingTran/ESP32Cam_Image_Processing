#include "camera_init.h"

#include <Arduino.h>
#include "config.h"
#include "camera_pins.h"

bool cameraInit(pixformat_t format, framesize_t frame_size, int fb_count, framesize_t alloc_size, int xclk_hz) {
  if (format != PIXFORMAT_JPEG && alloc_size != FRAMESIZE_INVALID && alloc_size != frame_size) {
    // Ảnh thô: driver cố định cỡ khung lúc init, đổi cỡ sau đó thì mọi khung báo "FB-SIZE ... != ..."
    Serial.println("cameraInit: alloc_size only works with JPEG, ignored");
    alloc_size = FRAMESIZE_INVALID;
  }
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
  config.xclk_freq_hz = xclk_hz > 0 ? xclk_hz : CAM_XCLK_FREQ_HZ;
  config.pixel_format = format;
  config.frame_size = alloc_size == FRAMESIZE_INVALID ? frame_size : alloc_size;
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.fb_count = fb_count;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  // Nhiều buffer: bỏ khung cũ, luôn lấy khung mới nhất
  config.grab_mode = fb_count > 1 ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;

  if (!psramFound()) {
    Serial.println("WARNING: PSRAM not found, using 1 frame buffer in DRAM");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (config.frame_size != frame_size) {
    s->set_framesize(s, frame_size);
  }
  s->set_vflip(s, CAM_VFLIP);
  s->set_hmirror(s, CAM_HMIRROR);
  return true;
}
