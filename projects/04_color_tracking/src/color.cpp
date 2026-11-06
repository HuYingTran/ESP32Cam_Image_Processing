#include "color.h"

#include <stdlib.h>
#include <string.h>

void rgbToHsv(int r, int g, int b, int *h, int *s, int *v) {
  int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
  int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
  int d = max - min;
  *v = max;
  *s = max ? d * 255 / max : 0;
  if (d == 0) {
    *h = 0;  // xám: không có màu
  } else if (max == r) {
    *h = 60 * (g - b) / d;
    if (*h < 0) {
      *h += 360;
    }
  } else if (max == g) {
    *h = 120 + 60 * (b - r) / d;
  } else {
    *h = 240 + 60 * (r - g) / d;
  }
}

void rgb565At(const uint8_t *buf, int width, int x, int y, int *r, int *g, int *b) {
  const uint8_t *p = buf + 2 * (y * width + x);
  uint16_t px = (p[0] << 8) | p[1];  // camera trả byte cao trước
  *r = ((px >> 11) & 0x1F) << 3;
  *g = ((px >> 5) & 0x3F) << 2;
  *b = (px & 0x1F) << 3;
}

static inline bool hueInRange(int h, int h_min, int h_max) {
  return h_min <= h_max ? (h >= h_min && h <= h_max) : (h >= h_min || h <= h_max);
}

ColorTracker::~ColorTracker() {
  free(mask_);
  free(tmp_);
}

bool ColorTracker::begin(int width, int height, const ColorParams &p) {
  width_ = width;
  height_ = height;
  params = p;
  // Trên ESP32 có PSRAM, malloc khối > 4 KB tự vào PSRAM
  mask_ = (uint8_t *)calloc(width * height, 1);
  tmp_ = (uint8_t *)calloc(width * height, 1);
  return mask_ && tmp_;
}

void ColorTracker::buildMask(const uint8_t *rgb565) {
  const ColorParams &p = params;
  const int n = width_ * height_;
  for (int i = 0; i < n; i++) {
    uint16_t px = (rgb565[2 * i] << 8) | rgb565[2 * i + 1];
    int r = ((px >> 11) & 0x1F) << 3;
    int g = ((px >> 5) & 0x3F) << 2;
    int b = (px & 0x1F) << 3;
    // Tính V, S trước: đa số pixel bị loại ở đây, khỏi tính H
    int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int s = max ? (max - min) * 255 / max : 0;
    if (max < p.v_min || max > p.v_max || s < p.s_min || s > p.s_max) {
      mask_[i] = 0;
      continue;
    }
    int h, s2, v2;
    rgbToHsv(r, g, b, &h, &s2, &v2);
    mask_[i] = hueInRange(h, p.h_min, p.h_max) ? 255 : 0;
  }
}

// Erode 3x3 = AND của 9 điểm lân cận, tách thành 2 lượt: ngang rồi dọc.
// Ngoài biên coi là 0, nên viền ảnh luôn bị xóa
void ColorTracker::erode() {
  const int w = width_, h = height_;
  for (int y = 0; y < h; y++) {
    const uint8_t *m = mask_ + y * w;
    uint8_t *t = tmp_ + y * w;
    t[0] = 0;
    t[w - 1] = 0;
    for (int x = 1; x < w - 1; x++) {
      t[x] = m[x - 1] & m[x] & m[x + 1];
    }
  }
  memset(mask_, 0, w);
  memset(mask_ + (h - 1) * w, 0, w);
  for (int y = 1; y < h - 1; y++) {
    for (int x = 0; x < w; x++) {
      int i = y * w + x;
      mask_[i] = tmp_[i - w] & tmp_[i] & tmp_[i + w];
    }
  }
}

// Dilate 3x3 = OR của 9 điểm lân cận
void ColorTracker::dilate() {
  const int w = width_, h = height_;
  for (int y = 0; y < h; y++) {
    const uint8_t *m = mask_ + y * w;
    uint8_t *t = tmp_ + y * w;
    t[0] = m[0] | m[1];
    t[w - 1] = m[w - 2] | m[w - 1];
    for (int x = 1; x < w - 1; x++) {
      t[x] = m[x - 1] | m[x] | m[x + 1];
    }
  }
  for (int x = 0; x < w; x++) {
    mask_[x] = tmp_[x] | tmp_[w + x];
    mask_[(h - 1) * w + x] = tmp_[(h - 2) * w + x] | tmp_[(h - 1) * w + x];
  }
  for (int y = 1; y < h - 1; y++) {
    for (int x = 0; x < w; x++) {
      int i = y * w + x;
      mask_[i] = tmp_[i - w] | tmp_[i] | tmp_[i + w];
    }
  }
}

ColorResult ColorTracker::process(const uint8_t *rgb565, uint32_t (*micros)()) {
  ColorResult r = {};
  uint32_t t0 = micros ? micros() : 0;
  buildMask(rgb565);
  uint32_t t1 = micros ? micros() : 0;
  if (params.morph) {
    // Erode rồi dilate (phép mở): xóa chấm nhỏ hơn 3x3, giữ nguyên kích thước vùng lớn
    erode();
    dilate();
  }
  uint32_t t2 = micros ? micros() : 0;

  uint32_t n = 0;
  uint64_t sx = 0, sy = 0;
  int x0 = width_, y0 = height_, x1 = -1, y1 = -1;
  for (int y = 0; y < height_; y++) {
    const uint8_t *m = mask_ + y * width_;
    uint32_t row_n = 0;
    for (int x = 0; x < width_; x++) {
      if (m[x]) {
        row_n++;
        sx += x;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
      }
    }
    if (row_n) {
      n += row_n;
      sy += (uint64_t)y * row_n;
      if (y < y0) y0 = y;
      y1 = y;
    }
  }
  uint32_t t3 = micros ? micros() : 0;

  r.area = n;
  r.found = (int)n >= params.min_area;
  if (n) {
    r.cx = sx / n;
    r.cy = sy / n;
    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1;
    r.y1 = y1;
  }
  r.mask_us = t1 - t0;
  r.morph_us = t2 - t1;
  r.moment_us = t3 - t2;
  return r;
}

void ColorTracker::sampleHsv(const uint8_t *rgb565, int x, int y, int *h, int *s, int *v) const {
  // Lấy trung bình RGB rồi mới đổi sang HSV (trung bình H trực tiếp sai khi vòng qua 0)
  int sr = 0, sg = 0, sb = 0, cnt = 0;
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      int px = x + dx, py = y + dy;
      if (px < 0 || py < 0 || px >= width_ || py >= height_) {
        continue;
      }
      int r, g, b;
      rgb565At(rgb565, width_, px, py, &r, &g, &b);
      sr += r;
      sg += g;
      sb += b;
      cnt++;
    }
  }
  if (!cnt) {
    *h = *s = *v = 0;
    return;
  }
  rgbToHsv(sr / cnt, sg / cnt, sb / cnt, h, s, v);
}
