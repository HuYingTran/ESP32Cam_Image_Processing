#include "wifi_init.h"

#include "config.h"

IPAddress wifiInit() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  Serial.printf("Connecting to '%s'", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    return WiFi.localIP();
  }

  Serial.printf("WiFi failed, starting AP '%s' (password '%s')\n", WIFI_AP_SSID, WIFI_AP_PASSWORD);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  return WiFi.softAPIP();
}
