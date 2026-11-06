// Cấu hình firmware thu ảnh của bài #7 (env 07_capture)
#pragma once

#include "config.h"  // common/config.h: WiFi, board camera

// ===== Ảnh =====
// Edge Impulse tự thu nhỏ về đầu vào model (96x96), nên chụp lớn hơn một chút là đủ.
// QVGA 320x240 ~10 KB/ảnh: tải nhanh qua WiFi, dư chi tiết cho 96x96 hoặc 160x160
#define CAP_FRAME_SIZE   FRAMESIZE_QVGA
#define CAP_JPEG_QUALITY 10  // 0-63, càng nhỏ càng nét
// Giữ cố định cân bằng trắng / phơi sáng khi thu dữ liệu? 0: để camera tự chỉnh như lúc chạy model
#define CAP_LOCK_EXPOSURE 0

// ===== Đèn flash GPIO4 =====
// Độ sáng 0-255 lúc khởi động (0 = tắt), đổi được trên web. Nếu bật khi thu ảnh thì lúc chạy model cũng phải bật
#define CAP_FLASH_DUTY 0
