// Cấu hình chung cho mọi bài: WiFi, board camera, thông số chụp ảnh.
// Cấu hình riêng của từng bài (mô hình, ngưỡng...) nằm trong projects/NN_xxx/src/lesson_config.h
// Code khởi tạo camera/WiFi dùng chung: common/esp32cam/
#pragma once

// WIFI_SSID, WIFI_PASSWORD trong secrets.h (không commit).
// Chưa tạo secrets.h thì dùng tạm file mẫu: không vào được WiFi nhà, board sẽ tự phát AP
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "Chua co secrets.h: copy secrets.example.h thanh secrets.h va dien WiFi"
#include "secrets.example.h"
#endif

// ===== WiFi =====
#define WIFI_CONNECT_TIMEOUT_MS 20000  // hết thời gian thì phát WiFi riêng (AP)
#define WIFI_AP_SSID            "ESP32-CAM"
#define WIFI_AP_PASSWORD        "12345678"  // tối thiểu 8 ký tự

// ===== Board camera =====
// Chọn 1 trong các model có trong camera_pins.h
#if !defined(CAMERA_MODEL_AI_THINKER) && !defined(CAMERA_MODEL_WROVER_KIT) && \
    !defined(CAMERA_MODEL_ESP_EYE) && !defined(CAMERA_MODEL_M5STACK_PSRAM)
#define CAMERA_MODEL_AI_THINKER
#endif

// ===== Thông số camera =====
#define CAM_XCLK_FREQ_HZ  20000000
#define CAM_FRAME_SIZE    FRAMESIZE_QVGA  // QQVGA, QVGA, CIF, VGA, SVGA, XGA, UXGA...
#define CAM_JPEG_QUALITY  10              // 0-63, càng nhỏ càng nét
#define CAM_VFLIP         0
#define CAM_HMIRROR       0
