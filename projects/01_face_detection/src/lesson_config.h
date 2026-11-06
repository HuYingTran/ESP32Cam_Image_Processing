// Cấu hình riêng của bài #1
#pragma once

#include "config.h"  // common/config.h

// Mô hình phát hiện khuôn mặt (ESP-DL):
//   1: hai tầng MSR01 + MNP01, chính xác hơn, có 5 điểm mốc, chậm hơn
//   0: một tầng MSR01, nhanh hơn, không có điểm mốc
#define FACE_DETECT_TWO_STAGE 1
