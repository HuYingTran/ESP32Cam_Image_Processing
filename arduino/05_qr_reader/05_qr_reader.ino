// Bài #5 - Đọc mã QR
//
// Sketch Arduino IDE sinh từ projects/05_qr_reader/src/ bằng tools/make_arduino_sketches.py.
// Code chính ở tab main.cpp; file .ino này để trống, chỉ để Arduino IDE mở được sketch.
// Sửa code ở projects/ rồi chạy lại script (sinh lại sẽ ghi đè thay đổi trong thư mục này,
// trừ secrets.h).
//
// Cài đặt:
//   Boards Manager: "esp32" by Espressif Systems, đúng phiên bản 2.0.17
//   Tools > Board: "AI Thinker ESP32-CAM"
//   Tools > Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
//   Tools > Core Debug Level: "Info"
//   Serial Monitor: 115200 baud
//
// WiFi: thêm tab secrets.h (chép nội dung secrets.example.h) rồi điền WIFI_SSID, WIFI_PASSWORD.
// Chưa có secrets.h thì vẫn build được, board phát WiFi "ESP32-CAM" (mật khẩu 12345678),
// vào http://192.168.4.1
// Cấu hình chung: config.h · cấu hình riêng của bài: lesson_config.h
//
// quirc (ISC) và openmv (MIT) nằm sẵn trong src/quirc/, không cần cài thêm thư viện.
