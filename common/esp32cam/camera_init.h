// Khởi tạo camera dùng chung cho các bài chạy Arduino core 2.0.x (từ bài #3).
// Chân camera lấy theo board chọn trong common/config.h, lật/gương theo CAM_VFLIP/CAM_HMIRROR.
#pragma once

#include "esp_camera.h"

// format: PIXFORMAT_JPEG, PIXFORMAT_GRAYSCALE, PIXFORMAT_RGB565...
// frame_size: độ phân giải chụp
// fb_count: số buffer; >= 2 thì luôn lấy khung mới nhất (cần PSRAM)
// alloc_size: cấp buffer theo độ phân giải này để sau đổi lên được (mặc định = frame_size).
//   Chỉ dùng được với JPEG. Ảnh thô (GRAYSCALE, RGB565...) bị driver cố định cỡ khung lúc init;
//   muốn đổi cỡ phải esp_camera_deinit() rồi cameraInit() lại
// xclk_hz: 0 = CAM_XCLK_FREQ_HZ. Ảnh thô lớn (GRAYSCALE/RGB565 từ VGA) nên giảm còn 10 MHz,
//   nếu không DMA chép vào PSRAM không kịp, log báo "EV-VSYNC-OVF" / "FB-OVF" và mất khung
// Gọi lại sau esp_camera_deinit() được (cùng chân, LEDC kênh/timer 0 cho XCLK)
bool cameraInit(pixformat_t format, framesize_t frame_size, int fb_count = 2,
                framesize_t alloc_size = FRAMESIZE_INVALID, int xclk_hz = 0);
