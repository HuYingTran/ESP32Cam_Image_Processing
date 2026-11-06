// Ra quyết định từ điểm của model: in Serial, bật/tắt LED
#pragma once

void responderInit();

// Điểm 0..1 đã dequantize. Trả về true nếu kết luận có người
bool respondToDetection(float person, float no_person, bool print);
