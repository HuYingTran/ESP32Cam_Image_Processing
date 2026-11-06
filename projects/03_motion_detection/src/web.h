// Trang web xem ảnh xám, tô các block thay đổi và chỉnh ngưỡng lúc đang chạy.
//   /           trang web
//   /frame.jpg  khung mới nhất (JPEG), header X-Mask = chuỗi 0/1 của các block
//   /status     JSON thống kê và tham số
//   /set?var=pixel|area|alpha|confirm|cooldown|light|normalize|reset&val=...
#pragma once

#include "esp_camera.h"
#include "motion.h"

void webStart(MotionDetector *detector, int width, int height);

// Gọi từ loop() sau mỗi khung: lưu khung cho web, áp dụng tham số vừa sửa trên web
void webPublish(const camera_fb_t *fb, const MotionResult &r, float fps, uint32_t process_us);
