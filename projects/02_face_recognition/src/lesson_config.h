// Cấu hình riêng của bài #2
#pragma once

#include "config.h"  // common/config.h

// Số người tối đa được lưu (mỗi người 1 vector 512 float trong phân vùng fr)
#define FACE_ID_MAX 7

// Số ảnh chụp cho mỗi lần enroll; vector lưu lại là trung bình các ảnh
#define FACE_ENROLL_SAMPLES 5

// Ngưỡng cosine similarity để chấp nhận là cùng một người (thư viện mặc định 0.55)
//   Tăng: ít nhận nhầm người lạ, nhưng hay từ chối nhầm người quen
//   Giảm: ngược lại
#define FACE_RECOGNITION_THRESHOLD 0.55f

// Lưu khuôn mặt đã enroll vào flash (phân vùng fr) để giữ sau khi khởi động lại
#define FACE_SAVE_TO_FLASH 1

// ===== Mô hình phát hiện MTMN (esp-face), mặc định giống ví dụ gốc =====
// Kích thước mặt nhỏ nhất (pixel) mà MTMN tìm; nhỏ hơn thì tìm được ở xa nhưng chậm hơn
#define FACE_MIN_SIZE 80

// FAST: nhanh, bỏ qua FACE_PYRAMID · NORMAL: dùng ảnh tháp theo FACE_PYRAMID, chậm hơn
#define FACE_DETECT_TYPE FAST
#define FACE_PYRAMID 0.707f       // tỉ lệ thu nhỏ giữa các tầng ảnh tháp
#define FACE_PYRAMID_TIMES 4      // số tầng ảnh tháp

// Ngưỡng điểm của 3 mạng P-Net → R-Net → O-Net; giảm thì dễ thấy mặt hơn nhưng dễ báo nhầm hơn
#define FACE_P_SCORE 0.6f
#define FACE_R_SCORE 0.7f
#define FACE_O_SCORE 0.7f
