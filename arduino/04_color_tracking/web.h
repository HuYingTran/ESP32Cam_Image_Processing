// Trang web xem ảnh (hoặc mask), bấm vào ảnh để lấy màu, chỉnh ngưỡng HSV.
// Khung bao và trọng tâm do trình duyệt vẽ lên canvas, nên nét ở mọi độ phân giải.
//   /           trang web
//   /frame.jpg  khung mới nhất (chờ tối đa 1 s), header X-Result = "found,cx,cy,x0,y0,x1,y1"
//               theo tọa độ ảnh xử lý, của đúng khung đó
//   /status     JSON kết quả, thời gian từng bước, tham số
//   /set?var=hmin|hmax|smin|smax|vmin|vmax|area|morph|view|awb&val=...
//   /pick?x=..&y=..  lấy màu tại điểm (x, y) của ảnh làm ngưỡng
#pragma once

#include <stddef.h>
#include "color.h"

struct FrameStats {
  float fps;
  uint32_t decode_us;  // giải nén JPEG → RGB565 (chế độ COLOR_CAPTURE_JPEG)
  uint32_t jpeg_us;    // nén JPEG cho web (mask hoặc chế độ RGB565)
  int center_h, center_s, center_v;  // HSV giữa khung, để chọn ngưỡng
};

// width, height: kích thước ảnh xử lý (tọa độ kết quả, điểm bấm)
void webStart(ColorTracker *tracker, int width, int height);

// Gọi trong loop() trước process(): áp dụng tham số sửa trên web và điểm vừa bấm (cần khung hiện tại)
void webApply(const uint8_t *rgb565);

// Có trình duyệt đang xem không; view: 0 = ảnh, 1 = mask
bool webWantsFrame(int *view);

// jpg: ảnh cho web (web giữ và tự giải phóng bằng free), nullptr nếu khung này không gửi
void webPublish(const ColorResult &r, const FrameStats &s, uint8_t *jpg, size_t jpg_len);
