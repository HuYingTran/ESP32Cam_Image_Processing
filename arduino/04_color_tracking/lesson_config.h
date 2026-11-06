// Cấu hình riêng của bài #4
#pragma once

#include "config.h"  // common/config.h

// ===== Nguồn ảnh =====
// 1: chụp JPEG độ phân giải cao, web nhận thẳng ảnh gốc (nét, màu đẹp),
//    board giải nén thu nhỏ COLOR_DECODE_SCALE lần để xử lý
// 0: chụp thẳng RGB565 ở COLOR_FRAME_SIZE (ảnh web mờ, nhạt hơn, nhưng không tốn thời gian giải nén)
#define COLOR_CAPTURE_JPEG 1
// Ảnh lớn thì web nét hơn nhưng trễ hơn (truyền WiFi + giải nén lâu hơn):
//   QVGA + JPG_SCALE_NONE: web 320x240, xử lý 320x240 (trễ thấp nhất)
//   VGA  + JPG_SCALE_2X  : web 640x480, xử lý 320x240 (nét, trễ cao)
#define COLOR_CAPTURE_SIZE FRAMESIZE_QVGA  // khi COLOR_CAPTURE_JPEG = 1
#define COLOR_DECODE_SCALE JPG_SCALE_NONE  // thu nhỏ khi giải nén: NONE, 2X, 4X, 8X
#define COLOR_FRAME_SIZE FRAMESIZE_QVGA   // khi COLOR_CAPTURE_JPEG = 0: 320x240 RGB565 = 150 KB/khung

// ===== Cảm biến =====
#define COLOR_SATURATION 2  // -2..2, tăng cho màu đậm, dễ lọc hơn
#define COLOR_CONTRAST 1    // -2..2
// Auto white balance làm màu trôi theo cảnh; 0 = khóa WB sau khi khởi động
#define COLOR_AWB 1

// ===== Ngưỡng HSV (sửa được lúc chạy trên trang web) =====
// H: 0-359 độ. H_MIN > H_MAX nghĩa là vòng qua 0 (màu đỏ: 345 → 15)
#define COLOR_H_MIN 345
#define COLOR_H_MAX 15
// S, V: 0-255
#define COLOR_S_MIN 100
#define COLOR_S_MAX 255
#define COLOR_V_MIN 60
#define COLOR_V_MAX 255

#define COLOR_MIN_AREA 200  // số pixel tối thiểu của vùng màu (tính trên ảnh xử lý), nhỏ hơn coi như không thấy
#define COLOR_MORPH 1       // 1: erode rồi dilate 3x3 để bỏ chấm nhiễu

// Bấm vào ảnh trên web để lấy màu: H = H điểm bấm ± COLOR_PICK_H_TOL,
// S_MIN, V_MIN = một nửa S, V của điểm bấm
#define COLOR_PICK_H_TOL 15

// ===== Đầu ra =====
#define COLOR_WEB 1  // 1: trang web xem ảnh / mask, chỉnh ngưỡng
// Hai thang chất lượng JPEG ngược nhau:
//   camera (set_quality) 0-63, nhỏ = nét: ảnh camera khi COLOR_CAPTURE_JPEG = 1
//   board tự nén (frame2jpg/fmt2jpg) 1-100, lớn = nét: ảnh mask, ảnh ở chế độ RGB565
#define COLOR_JPEG_QUALITY 10
#define COLOR_ENCODE_QUALITY 80
