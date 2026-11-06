// Cấu hình riêng của bài #5
#pragma once

#include "config.h"  // common/config.h

// ===== Ảnh =====
// Ảnh xám; lớn thì đọc được QR nhỏ/xa hơn nhưng chậm hơn. Đổi được trên web (QVGA, CIF, VGA, SVGA)
#define QR_FRAME_SIZE FRAMESIZE_VGA
// Cỡ lớn nhất cho phép đổi lên trên web (quyết định buffer web cấp sẵn).
// Đổi cỡ ảnh xám phải khởi tạo lại camera, mất khoảng nửa giây
#define QR_MAX_FRAME_SIZE FRAMESIZE_SVGA
#define QR_CONTRAST 1  // -2..2, tăng giúp tách đen/trắng khi nhị phân hóa
// Ảnh xám VGA/SVGA ở XCLK 20 MHz làm DMA không kịp (log "EV-VSYNC-OVF"); ví dụ của thư viện cũng dùng 10 MHz
#define QR_XCLK_HZ 10000000

// ===== Đèn flash GPIO4 =====
// Độ sáng 0-255 khi quét (0 = tắt). Sáng quá dễ lóa trên giấy bóng; thử 10-40
#define QR_FLASH_DUTY 0

// ===== Kết quả =====
// Không báo lại cùng nội dung chừng nào QR còn trong khung, và chưa vắng quá khoảng này
#define QR_DEBOUNCE_MS 3000

// Ứng dụng mẫu: QR có đúng nội dung này thì "mở cửa" (nháy flash); chuỗi rỗng = tắt
#define QR_EXPECTED_TEXT "ESP32-CAM-OPEN"
#define QR_ACTION_LED 1  // 1: nháy flash 0.5 s khi mở cửa

// ===== Khác =====
#define QR_WEB 1  // 1: trang web xem ảnh xám + khung QR, đổi độ phân giải/đèn
// quirc_decode dùng ~18 KB stack (2 mảng QUIRC_MAX_PAYLOAD), ví dụ thư viện dùng 40 KB
#define QR_TASK_STACK (40 * 1024)
