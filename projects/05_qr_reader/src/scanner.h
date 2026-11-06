// Task quét QR: chụp ảnh xám → quirc tìm và giải mã → đẩy kết quả vào queue.
// Dùng quirc đi kèm thư viện ESP32QRCodeReader; không dùng lớp ESP32QRCodeReader vì lớp đó
// tự khởi tạo camera (10 MHz, 1 buffer, nghỉ 100 ms/khung) và không cho lấy ảnh ra để xem.
#pragma once

#include <stdint.h>
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define QR_TEXT_MAX 256  // nội dung dài hơn bị cắt khi lưu (len vẫn là độ dài thật)
#define QR_MAX_CODES 4   // số mã tối đa xử lý trong một khung

struct QrResult {
  bool valid;
  char text[QR_TEXT_MAX + 1];  // nội dung, hoặc thông báo lỗi của quirc khi !valid
  int len;
  int version;
  char ecc;  // 'L', 'M', 'Q', 'H'
  uint32_t decode_us;
};

struct QrCorners {
  int count;
  int pts[QR_MAX_CODES][8];  // x0,y0,...,x3,y3: 4 góc từ trên trái, theo chiều kim đồng hồ
};

struct QrStats {
  float fps;
  uint32_t identify_us;  // quirc_end: nhị phân hóa + tìm finder pattern + lưới (trung bình)
  uint32_t decode_us;    // quirc_decode (trung bình trên các khung có mã)
  int width, height;
  uint32_t frames_with_code;  // tổng số khung có ít nhất một mã
  uint32_t decoded;           // tổng số lần giải mã thành công
  uint32_t failed;            // tổng số lần thấy mã nhưng giải mã lỗi
};

// Gọi sau mỗi khung, trong task quét, khi buffer camera còn hợp lệ
typedef void (*QrFrameCallback)(const camera_fb_t *fb, const QrCorners &codes);

bool scannerStart(QueueHandle_t results, QrFrameCallback on_frame);
QrStats scannerStats();

// Chỉ task quét được chạm vào camera; các hàm này đặt yêu cầu, task áp dụng trước khung kế tiếp.
// Đổi độ phân giải ảnh xám phải khởi tạo lại camera (driver cố định cỡ khung lúc init)
void scannerSetFrameSize(framesize_t size);
void scannerSetContrast(int contrast);
framesize_t scannerFrameSize();
int scannerContrast();
