// Cấu hình riêng của bài #3
#pragma once

#include "config.h"  // common/config.h

// ===== Ảnh =====
#define MOTION_FRAME_SIZE FRAMESIZE_QQVGA  // 160x120, 1 byte/pixel = 19.2 KB
#define MOTION_BLOCK_SIZE 8                // thu nhỏ theo block 8x8 → lưới 20x15

// ===== Ngưỡng (sửa được lúc chạy trên trang web) =====
#define MOTION_PIXEL_THRESHOLD 15    // chênh lệch độ sáng (0-255) để coi một block là thay đổi
#define MOTION_AREA_PERCENT 5        // % block thay đổi để coi khung có chuyển động
#define MOTION_BG_ALPHA 0.05f        // tốc độ cập nhật nền: bg = (1-a)*bg + a*khung
#define MOTION_CONFIRM_FRAMES 2      // số khung chuyển động liên tiếp mới báo
#define MOTION_COOLDOWN_MS 3000      // thời gian chờ giữa hai lần báo
// Quá % block cùng đổi một lúc thì coi là bật/tắt đèn: lấy luôn khung này làm nền, không báo
#define MOTION_LIGHT_CHANGE_PERCENT 80
// Trừ chênh lệch độ sáng trung bình giữa khung và nền trước khi so,
// để auto exposure hay ánh sáng đổi chậm không bị tính là chuyển động
#define MOTION_NORMALIZE_BRIGHTNESS 1

// Bỏ qua các khung đầu cho auto exposure / white balance ổn định
#define MOTION_WARMUP_MS 3000

// ===== Đầu ra =====
#define MOTION_FLASH_LED 0       // 1: nháy đèn flash GPIO4 khi báo chuyển động
#define MOTION_LED_ON_MS 200
#define MOTION_WEB 1             // 1: trang web xem ảnh + lưới block (cần WiFi)
// 1: in "changed:x threshold:y" mỗi khung cho Serial Plotter; 0: in thống kê mỗi giây
#define MOTION_PLOT 0
