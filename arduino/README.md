# Sketch Arduino IDE

Mỗi thư mục ở đây là một sketch Arduino IDE của một bài, tự chứa đủ code: `config.h`,
`camera_pins.h`, `camera_init`/`wifi_init` (nếu bài dùng), và quirc cho bài #5. Không cần cài thêm
thư viện nào ngoài core esp32.

| Sketch | Bài | Core esp32 |
|---|---|:-:|
| `01_face_detection` | [#1 Phát hiện khuôn mặt](../projects/01_face_detection/README.md) | 2.0.17 |
| `02_face_recognition` | [#2 Nhận dạng khuôn mặt](../projects/02_face_recognition/README.md) | **1.0.6** |
| `03_motion_detection` | [#3 Phát hiện chuyển động](../projects/03_motion_detection/README.md) | 2.0.17 |
| `04_color_tracking` | [#4 Bám màu](../projects/04_color_tracking/README.md) | 2.0.17 |
| `05_qr_reader` | [#5 Đọc mã QR](../projects/05_qr_reader/README.md) | 2.0.17 |
| `07_capture` | [#7 Thu ảnh train](../projects/07_image_classification/README.md) | 2.0.17 |

Bài #6 và #10 dùng ESP-IDF nên không có sketch ở đây.

## 1. Cài core esp32 đúng phiên bản

> [!IMPORTANT]
> Các bài **không chạy trên core 3.x** (core 3.x bỏ face detection, đổi API `ledcSetup`/`ledcAttachPin`...).
> Boards Manager chỉ giữ **một** phiên bản core esp32 tại một thời điểm, nên đổi bài #2 ↔ các bài khác
> là phải cài lại core.

1. **File → Preferences → Additional boards manager URLs**, thêm:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   (index này còn đủ các bản cũ như 1.0.6).
2. **Tools → Board → Boards Manager**, tìm `esp32` (by Espressif Systems), chọn phiên bản **2.0.17**
   (hoặc **1.0.6** cho bài #2) rồi Install.

## 2. Mở sketch và chọn board

1. **File → Open**, chọn file `.ino` trong thư mục bài (ví dụ `03_motion_detection/03_motion_detection.ino`).
   File `.ino` chỉ có ghi chú; code chính nằm ở tab `main.cpp`.
2. Menu **Tools**:

   | Mục | Giá trị |
   |---|---|
   | Board | `AI Thinker ESP32-CAM` |
   | Partition Scheme | `Huge APP (3MB No OTA/1MB SPIFFS)` (core 1.0.6 không có mục này, bài #2 dùng `partitions.csv` trong sketch) |
   | Core Debug Level | `Info` (giống `-DCORE_DEBUG_LEVEL=3` trong `platformio.ini`) |
   | Port | cổng COM của mạch nạp |

   Board `AI Thinker ESP32-CAM` đã tự bật PSRAM (`-DBOARD_HAS_PSRAM`).

## 3. WiFi

Thêm tab mới (nút `⋯` cạnh các tab → **New Tab**), đặt tên `secrets.h`, chép nội dung từ
`secrets.example.h` rồi điền:

```c
#pragma once
#define WIFI_SSID     "ten-wifi"
#define WIFI_PASSWORD "mat-khau"
```

Chưa có `secrets.h` thì vẫn build được (có warning). Board không vào được WiFi nhà sẽ tự phát WiFi
`ESP32-CAM`, mật khẩu `12345678`, vào `http://192.168.4.1`. `secrets.h` trong `arduino/` đã có trong
`.gitignore`.

## 4. Nạp và xem Serial

- Mạch nạp USB-TTL: nối GPIO0 xuống GND, bấm reset, Upload, xong thì bỏ GPIO0 ra và reset lại.
  Đế ESP32-CAM-MB tự làm việc này.
- Serial Monitor **115200** baud. Địa chỉ web được in ra sau khi kết nối WiFi.

## Cấu hình

| Muốn đổi | Tab |
|---|---|
| WiFi | `secrets.h` |
| AP dự phòng, board camera, độ phân giải, JPEG, lật ảnh | `config.h` |
| Mô hình, ngưỡng của bài | `lesson_config.h` (bài #7: `capture_config.h`) |

## Lỗi thường gặp

| Lỗi | Cách sửa |
|---|---|
| `'ledcSetup' was not declared`, thiếu `human_face_detect_msr01.hpp`, `fd_forward.h`... | Sai phiên bản core: cài đúng 2.0.17 (bài #2: 1.0.6) |
| `Sketch too big` | Partition Scheme chưa chọn Huge APP |
| `Brownout detector was triggered` | Nguồn yếu: dùng nguồn 5V ≥ 1A |
| `Camera init failed with error 0x105` / `0x20001` | Cáp camera lỏng, hoặc sai board trong `config.h` |
| `Failed to connect to ESP32: Timed out waiting for packet header` | Chưa đưa GPIO0 xuống GND khi nạp |

## Đồng bộ với code gốc

Code gốc nằm ở `projects/` và `common/` (dùng cho PlatformIO). Thư mục `arduino/` được sinh bằng:

```bash
python tools/make_arduino_sketches.py          # sinh lại toàn bộ (giữ secrets.h và README này)
python tools/make_arduino_sketches.py --check  # chỉ báo sketch nào lệch với nguồn
```

Sửa trực tiếp trong sketch thì nhớ chép ngược về `projects/`; lần sinh lại sau sẽ ghi đè.
