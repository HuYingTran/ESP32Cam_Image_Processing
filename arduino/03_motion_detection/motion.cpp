#include "motion.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

MotionDetector::~MotionDetector() {
  free(blocks_);
  free(background_);
  free(row_sum_);
  free(mask_);
}

bool MotionDetector::begin(int width, int height, int block, const MotionParams &p) {
  width_ = width;
  height_ = height;
  block_ = block;
  // Phần lẻ ở mép phải/dưới không chia hết cho block thì bỏ qua
  cols_ = width / block;
  rows_ = height / block;
  params = p;

  int n = cols_ * rows_;
  blocks_ = (float *)calloc(n, sizeof(float));
  background_ = (float *)calloc(n, sizeof(float));
  row_sum_ = (uint32_t *)calloc(cols_, sizeof(uint32_t));
  mask_ = (uint8_t *)calloc(n, 1);
  has_background_ = false;
  streak_ = 0;
  alarmed_ = false;
  return n > 0 && blocks_ && background_ && row_sum_ && mask_;
}

// Trung bình mỗi block block_ x block_: duyệt từng hàng pixel một lần,
// cộng dồn theo cột block, đủ block_ hàng thì chia ra giá trị trung bình
void MotionDetector::downsample(const uint8_t *gray) {
  const float area = (float)(block_ * block_);
  for (int by = 0; by < rows_; by++) {
    memset(row_sum_, 0, cols_ * sizeof(uint32_t));
    for (int y = by * block_; y < (by + 1) * block_; y++) {
      const uint8_t *row = gray + y * width_;
      for (int bx = 0; bx < cols_; bx++) {
        const uint8_t *p = row + bx * block_;
        uint32_t s = 0;
        for (int i = 0; i < block_; i++) {
          s += p[i];
        }
        row_sum_[bx] += s;
      }
    }
    for (int bx = 0; bx < cols_; bx++) {
      blocks_[by * cols_ + bx] = row_sum_[bx] / area;
    }
  }
}

MotionResult MotionDetector::process(const uint8_t *gray, uint32_t now_ms) {
  MotionResult r = {};
  const int n = cols_ * rows_;
  downsample(gray);

  float mean = 0, bg_mean = 0;
  for (int i = 0; i < n; i++) {
    mean += blocks_[i];
    bg_mean += background_[i];
  }
  mean /= n;
  bg_mean /= n;
  r.mean = mean;

  if (!has_background_) {
    memcpy(background_, blocks_, n * sizeof(float));
    memset(mask_, 0, n);
    has_background_ = true;
    streak_ = 0;
    return r;
  }

  const float offset = params.normalize_brightness ? mean - bg_mean : 0;
  for (int i = 0; i < n; i++) {
    float d = fabsf(blocks_[i] - background_[i] - offset);
    mask_[i] = d > params.pixel_threshold;
    r.changed += mask_[i];
  }
  r.changed_percent = 100.0f * r.changed / n;
  r.light_change = r.changed_percent >= params.light_change_percent;
  r.motion = !r.light_change && r.changed_percent >= params.area_percent;

  streak_ = r.motion ? streak_ + 1 : 0;
  if (streak_ >= params.confirm_frames && (!alarmed_ || now_ms - last_alarm_ms_ >= params.cooldown_ms)) {
    r.alarm = true;
    alarmed_ = true;
    last_alarm_ms_ = now_ms;
  }

  if (r.light_change) {
    // Đổi ánh sáng đột ngột: lấy luôn khung này làm nền thay vì chờ running average
    memcpy(background_, blocks_, n * sizeof(float));
    streak_ = 0;
  } else {
    const float a = params.bg_alpha;
    for (int i = 0; i < n; i++) {
      background_[i] += a * (blocks_[i] - background_[i]);
    }
  }
  return r;
}
