// Bám màu trên ảnh RGB565 (byte cao trước, như camera trả về): lọc pixel theo ngưỡng HSV, erode/dilate, tính trọng tâm và khung bao.
// Không phụ thuộc Arduino/camera để có thể chạy thử trên máy tính.
#pragma once

#include <stdint.h>

struct ColorParams {
  int h_min, h_max;  // 0-359, h_min > h_max: khoảng vòng qua 0 (màu đỏ)
  int s_min, s_max;  // 0-255
  int v_min, v_max;  // 0-255
  int min_area;      // số pixel tối thiểu
  bool morph;        // erode rồi dilate 3x3
};

struct ColorResult {
  bool found;
  int area;                // số pixel thuộc mask (sau lọc nhiễu)
  int cx, cy;              // trọng tâm
  int x0, y0, x1, y1;      // khung bao (gồm cả hai đầu)
  uint32_t mask_us;        // RGB565 → HSV → mask
  uint32_t morph_us;       // erode + dilate
  uint32_t moment_us;      // trọng tâm, khung bao
};

// r, g, b: 0-255 → h: 0-359, s, v: 0-255. Chỉ dùng số nguyên
void rgbToHsv(int r, int g, int b, int *h, int *s, int *v);

// Đọc pixel (x, y) của ảnh RGB565 do camera trả về (byte cao trước) ra r, g, b 0-255
void rgb565At(const uint8_t *buf, int width, int x, int y, int *r, int *g, int *b);

class ColorTracker {
 public:
  ~ColorTracker();
  bool begin(int width, int height, const ColorParams &params);

  // rgb565: width*height*2 byte. micros: hàm lấy thời gian (µs) để đo từng bước, có thể nullptr
  ColorResult process(const uint8_t *rgb565, uint32_t (*micros)());

  // Mask width*height byte: 255 = pixel đạt ngưỡng, 0 = không (xem được như ảnh xám)
  const uint8_t *mask() const { return mask_; }

  // HSV trung bình vùng 5x5 quanh (x, y), dùng để lấy ngưỡng
  void sampleHsv(const uint8_t *rgb565, int x, int y, int *h, int *s, int *v) const;

  ColorParams params;

 private:
  void buildMask(const uint8_t *rgb565);
  void erode();
  void dilate();

  int width_ = 0, height_ = 0;
  uint8_t *mask_ = nullptr;
  uint8_t *tmp_ = nullptr;
};
