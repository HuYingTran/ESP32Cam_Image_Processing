// Thông số cố định của model person_detect (Visual Wake Words, MobileNet int8).
// Đổi model khác thì phải sửa theo model đó
#pragma once

constexpr int kNumCols = 96;
constexpr int kNumRows = 96;
constexpr int kNumChannels = 1;
constexpr int kMaxImageSize = kNumCols * kNumRows * kNumChannels;

constexpr int kCategoryCount = 2;
constexpr int kNotAPersonIndex = 0;
constexpr int kPersonIndex = 1;
