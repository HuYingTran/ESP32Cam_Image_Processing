// Kết nối WiFi dùng chung: thử vào mạng WIFI_SSID, quá WIFI_CONNECT_TIMEOUT_MS thì
// tự phát WiFi riêng WIFI_AP_SSID để vẫn vào được web. Cấu hình trong common/config.h.
#pragma once

#include <WiFi.h>

// Trả về địa chỉ IP để mở web
IPAddress wifiInit();
