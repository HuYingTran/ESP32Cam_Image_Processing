// Phát hiện chuyển động trên ảnh xám bằng background subtraction theo block.
// Không phụ thuộc Arduino/camera để có thể chạy thử trên máy tính.
#pragma once

#include <stdint.h>

struct MotionParams {
  int pixel_threshold;        // chênh lệch độ sáng của một block để coi là thay đổi
  float area_percent;         // % block thay đổi để coi khung có chuyển động
  float bg_alpha;             // tốc độ cập nhật nền
  int confirm_frames;         // số khung chuyển động liên tiếp mới báo
  uint32_t cooldown_ms;       // thời gian chờ giữa hai lần báo
  float light_change_percent; // quá ngưỡng này coi là đổi ánh sáng, không phải chuyển động
  bool normalize_brightness;  // bù chênh lệch độ sáng trung bình giữa khung và nền
};

struct MotionResult {
  float mean;            // độ sáng trung bình của khung (0-255)
  int changed;           // số block thay đổi
  float changed_percent; // changed / tổng số block * 100
  bool light_change;     // quá nhiều block đổi cùng lúc → coi là đổi ánh sáng
  bool motion;           // khung này vượt ngưỡng diện tích
  bool alarm;            // đủ confirm_frames khung liên tiếp và hết cooldown → báo
};

class MotionDetector {
 public:
  ~MotionDetector();

  // width, height: kích thước ảnh xám; block: cạnh block khi thu nhỏ
  bool begin(int width, int height, int block, const MotionParams &params);

  // gray: width*height byte, mỗi pixel 1 byte, lưu theo hàng
  MotionResult process(const uint8_t *gray, uint32_t now_ms);

  // Lấy khung kế tiếp làm nền mới
  void reset() { has_background_ = false; }

  int cols() const { return cols_; }
  int rows() const { return rows_; }
  const uint8_t *mask() const { return mask_; }  // cols*rows, 1 = block thay đổi

  MotionParams params;

 private:
  void downsample(const uint8_t *gray);

  int width_ = 0, height_ = 0, block_ = 0;
  int cols_ = 0, rows_ = 0;
  float *blocks_ = nullptr;      // độ sáng trung bình từng block của khung hiện tại
  float *background_ = nullptr;  // nền, cùng kích thước
  uint32_t *row_sum_ = nullptr;  // tổng tạm theo cột khi thu nhỏ
  uint8_t *mask_ = nullptr;
  bool has_background_ = false;
  int streak_ = 0;
  bool alarmed_ = false;
  uint32_t last_alarm_ms_ = 0;
};
