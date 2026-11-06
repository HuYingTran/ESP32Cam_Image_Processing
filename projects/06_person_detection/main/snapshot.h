// Gửi ảnh đầu vào của model (96x96 xám) qua Serial khi máy tính yêu cầu, xem tools/person_snapshot.py
#pragma once

#include <stdint.h>

void snapshotInit();

// Không chờ: true nếu đã nhận ký tự 'c' qua UART0 từ lần gọi trước
bool snapshotRequested();

// Giữ lại ảnh int8 (gọi trước Invoke: interpreter có thể dùng lại vùng nhớ của input)
void snapshotKeep(const int8_t *img);

// In ảnh đã giữ cùng điểm person (0..1) và kết luận theo PD_THRESHOLD:
//   SNAP 96 96 person 95 detected 1
//   <96 dòng, mỗi dòng 96 điểm ảnh uint8 dạng hex>
//   SNAP END
void snapshotSend(float person, bool detected);
