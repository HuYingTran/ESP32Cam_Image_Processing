// Cấu hình riêng của bài #6 (ESP-IDF, không dùng common/config.h của các bài Arduino)
// Cấu hình hệ thống (PSRAM, CPU, ESP-NN): sdkconfig.defaults / idf.py menuconfig
#pragma once

// ===== Board camera =====
// Sơ đồ chân lấy từ common/camera_pins.h
#define CAMERA_MODEL_AI_THINKER

// ===== Ảnh =====
// FRAMESIZE_96X96: cảm biến tự thu nhỏ đúng cỡ đầu vào, nhanh nhất.
// FRAMESIZE_QQVGA / QVGA: cắt hình vuông ở giữa rồi thu nhỏ trung bình về 96x96 bằng phần mềm
// (góc nhìn hẹp hơn 96X96 vì bỏ hai bên trái/phải)
#define PD_FRAME_SIZE FRAMESIZE_96X96
#define PD_XCLK_HZ 15000000  // ví dụ gốc dùng 15 MHz cho ảnh xám
#define PD_VFLIP 0
#define PD_HMIRROR 0

// ===== Quyết định =====
// Báo PERSON khi điểm person (0..1) >= ngưỡng. Tăng thì ít báo nhầm hơn nhưng dễ bỏ sót
#define PD_THRESHOLD 0.60f

// ===== Đèn =====
// LED đỏ GPIO33 ở mặt sau, mức thấp là sáng; -1 = không dùng.
// Không dùng đèn flash GPIO4: quá sáng, rọi vào người làm sai ảnh
#define PD_LED_GPIO 33
#define PD_LED_ACTIVE_LOW 1

// ===== Model =====
// Tensor arena: đo được used 82 296 byte (có ESP-NN). Khối RAM trong liền lớn nhất chỉ ~110 KB,
// nên arena phải nhỏ hơn mức đó mới nằm được trong RAM trong (160 KB thì rơi sang PSRAM, chậm hơn).
// Lúc khởi động có in số byte thật sự dùng (arena used) để chỉnh lại
#define PD_ARENA_SIZE (88 * 1024)
// 1: thử RAM trong trước (nhanh hơn), không đủ thì dùng PSRAM. 0: luôn dùng PSRAM
#define PD_ARENA_PREFER_INTERNAL 1
// 1: chép model (300 KB) từ flash sang PSRAM lúc khởi động, Invoke đọc trọng số nhanh hơn. 0: đọc thẳng từ flash
#define PD_MODEL_IN_PSRAM 1

// ===== Log =====
#define PD_PRINT_EVERY_FRAME 1  // 1: in điểm mỗi khung; 0: chỉ in khi kết quả đổi
#define PD_STATS_EVERY 20       // in thời gian trung bình sau mỗi N khung
// Chạy model trên 10 ảnh mẫu (7 có người, 3 không) lúc khởi động: kiểm tra model và đo
// thời gian suy luận không phụ thuộc camera, dùng để so sánh có/không ESP-NN
#define PD_SELF_TEST 1
// 1: nhận lệnh 'c' qua Serial, in ảnh 96x96 mà model thấy (xem bằng tools/person_snapshot.py)
#define PD_SNAPSHOT 1
