// Bài #5 - Đọc mã QR
// Task quét (scanner.cpp) chụp ảnh xám và giải mã bằng quirc; loop() nhận kết quả qua queue,
// lọc trùng và chạy ứng dụng mẫu "mở cửa" khi QR đúng nội dung.
// Cấu hình: common/config.h (WiFi, camera) và src/lesson_config.h (độ phân giải, đèn, nội dung mở cửa).

#include <Arduino.h>
#include "camera_init.h"  // common/esp32cam
#include "lesson_config.h"
#include "camera_pins.h"  // sau lesson_config.h: cần CAMERA_MODEL_* trong config.h
#include "scanner.h"
#if QR_WEB
#include "wifi_init.h"
#include "web.h"
#endif

// Đèn flash GPIO4 điều khiển bằng PWM; timer 0 đã dùng cho XCLK camera nên dùng kênh 2 (timer 1)
#define FLASH_LEDC_CHANNEL 2

static QueueHandle_t results;
static int flash_duty = QR_FLASH_DUTY;
static uint32_t flash_pulse_until = 0;

static char last_text[QR_TEXT_MAX + 1] = "";
static uint32_t last_seen_ms = 0;
static uint32_t accepted = 0;
static uint32_t last_fail_print_ms = 0;
static uint32_t last_stats_ms = 0;

void flashSet(int duty) {
  flash_duty = duty;
#if defined(LED_GPIO_NUM)
  if (!flash_pulse_until) {
    ledcWrite(FLASH_LEDC_CHANNEL, duty);
  }
#endif
}

int flashGet() {
  return flash_duty;
}

static void flashPulse(uint32_t ms) {
#if defined(LED_GPIO_NUM)
  ledcWrite(FLASH_LEDC_CHANNEL, 255);
  flash_pulse_until = millis() + ms;
#endif
}

static void onFrame(const camera_fb_t *fb, const QrCorners &codes) {
#if QR_WEB
  webOnFrame(fb, codes);
#endif
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  // Ảnh xám: khởi tạo đúng cỡ dùng (không cấp dư như JPEG); đổi cỡ trên web thì task quét khởi tạo lại.
  // Tương phản do task quét đặt (scannerSetContrast)
  if (!cameraInit(PIXFORMAT_GRAYSCALE, QR_FRAME_SIZE, 2, FRAMESIZE_INVALID, QR_XCLK_HZ)) {
    return;
  }

#if defined(LED_GPIO_NUM)
  ledcSetup(FLASH_LEDC_CHANNEL, 5000, 8);
  ledcAttachPin(LED_GPIO_NUM, FLASH_LEDC_CHANNEL);
  ledcWrite(FLASH_LEDC_CHANNEL, flash_duty);
#endif

#if QR_WEB
  // Buffer web cấp theo cỡ lớn nhất để đổi độ phân giải lúc chạy
  IPAddress ip = wifiInit();
  webStart(resolution[QR_MAX_FRAME_SIZE].width, resolution[QR_MAX_FRAME_SIZE].height);
  Serial.print("Web: http://");
  Serial.println(ip);
#endif

  results = xQueueCreate(8, sizeof(QrResult));
  if (!results || !scannerStart(results, onFrame)) {
    Serial.println("Scanner start failed");
    return;
  }
  Serial.printf("QR reader ready: %dx%d, flash %d, debounce %d ms, expected \"%s\"\n",
                resolution[QR_FRAME_SIZE].width, resolution[QR_FRAME_SIZE].height,
                flash_duty, QR_DEBOUNCE_MS, QR_EXPECTED_TEXT);
}

static void handleResult(const QrResult &r) {
  uint32_t now = millis();
  if (!r.valid) {
    // Thấy mã nhưng không giải được (mờ, lóa, nghiêng): in tối đa 1 lần/giây
    if (now - last_fail_print_ms >= 1000) {
      Serial.printf("QR found but decode failed: %s\n", r.text);
      last_fail_print_ms = now;
    }
#if QR_WEB
    webOnFailure(r.text);
#endif
    return;
  }

  // Cùng nội dung và vẫn đang thấy (vắng chưa quá QR_DEBOUNCE_MS): không báo lại
  bool same = strcmp(r.text, last_text) == 0 && now - last_seen_ms < QR_DEBOUNCE_MS;
  last_seen_ms = now;
  if (same) {
    return;
  }
  strncpy(last_text, r.text, QR_TEXT_MAX);
  accepted++;
  Serial.printf("QR #%u: \"%s\" (%d chars, version %d, ECC %c, decode %.1f ms)\n",
                accepted, r.text, r.len, r.version, r.ecc, r.decode_us / 1000.0f);

  int granted = -1;
  if (QR_EXPECTED_TEXT[0]) {
    granted = strcmp(r.text, QR_EXPECTED_TEXT) == 0;
    Serial.println(granted ? "ACCESS GRANTED" : "ACCESS DENIED");
#if QR_ACTION_LED
    if (granted) {
      flashPulse(500);
    }
#endif
  }
#if QR_WEB
  webOnResult(r, accepted, granted);
#endif
}

void loop() {
  if (!results) {
    delay(1000);  // setup() lỗi
    return;
  }
  QrResult r;
  if (xQueueReceive(results, &r, pdMS_TO_TICKS(100)) == pdTRUE) {
    handleResult(r);
  }

  uint32_t now = millis();
#if defined(LED_GPIO_NUM)
  if (flash_pulse_until && (int32_t)(now - flash_pulse_until) >= 0) {
    flash_pulse_until = 0;
    ledcWrite(FLASH_LEDC_CHANNEL, flash_duty);
  }
#endif
  if (now - last_stats_ms >= 5000) {
    QrStats s = scannerStats();
    Serial.printf("%dx%d %.1f fps | identify %.0f ms, decode %.1f ms | frames with code %u, decoded %u, failed %u\n",
                  s.width, s.height, s.fps, s.identify_us / 1000.0f, s.decode_us / 1000.0f,
                  s.frames_with_code, s.decoded, s.failed);
    last_stats_ms = now;
  }
}
