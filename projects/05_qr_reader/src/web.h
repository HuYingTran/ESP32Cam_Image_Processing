// Trang web xem ảnh xám + khung QR (để chỉnh nét, ánh sáng), nội dung vừa đọc, đổi độ phân giải/đèn.
//   /           trang web
//   /frame.jpg  khung mới nhất (chờ tối đa 1 s), header X-Codes = "x0,y0,...,x3,y3;..." 4 góc mỗi mã
//   /status     JSON thống kê, nội dung QR gần nhất
//   /set?var=framesize|flash|contrast&val=...
#pragma once

#include "esp_camera.h"
#include "scanner.h"

void webStart(int max_width, int max_height);

// Callback cho scanner: chép khung khi có trình duyệt đang xem
void webOnFrame(const camera_fb_t *fb, const QrCorners &codes);

// Báo kết quả loop() đã chấp nhận (sau lọc trùng). granted: 1 mở cửa, 0 từ chối, -1 không dùng
void webOnResult(const QrResult &r, uint32_t count, int granted);
void webOnFailure(const char *error);

// main.cpp cung cấp: đặt độ sáng đèn flash 0-255
void flashSet(int duty);
int flashGet();
