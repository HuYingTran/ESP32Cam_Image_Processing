// Lấy ảnh từ camera và đưa về đúng đầu vào của model: 96x96, 1 kênh xám, int8
#pragma once

#include <stdint.h>

struct ImageTiming {
  uint32_t capture_us;     // esp_camera_fb_get
  uint32_t preprocess_us;  // cắt / thu nhỏ / uint8 -> int8
};

bool imageProviderInit();

// Ghi kNumCols x kNumRows byte int8 vào dst
bool imageProviderGet(int8_t *dst, ImageTiming *timing);
