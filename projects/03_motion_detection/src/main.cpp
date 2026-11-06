// Bài #3 - Phát hiện chuyển động
// Chụp ảnh xám QQVGA, thu nhỏ theo block, so với ảnh nền (running average) để tìm vùng thay đổi.
// Cấu hình: common/config.h (WiFi, camera) và src/lesson_config.h (ngưỡng).

#include <Arduino.h>
#include "camera_init.h"  // common/esp32cam
#include "lesson_config.h"
#include "camera_pins.h"
#include "motion.h"
#if MOTION_WEB
#include "wifi_init.h"
#include "web.h"
#endif

static MotionDetector detector;
static uint32_t start_ms = 0;
static uint32_t led_off_ms = 0;

// Thống kê in mỗi giây
static uint32_t stat_start_ms = 0;
static uint32_t stat_frames = 0;
static uint64_t stat_process_us = 0;
static float stat_max_changed = 0;
static float fps = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (!cameraInit(PIXFORMAT_GRAYSCALE, MOTION_FRAME_SIZE)) {
    return;
  }

  // Kiểm tra định dạng: ảnh xám thì fb->len phải bằng width * height
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  int width = fb->width, height = fb->height;
  Serial.printf("Frame: %dx%d, %u bytes, format %d (GRAYSCALE = %d)\n",
                width, height, (unsigned)fb->len, fb->format, PIXFORMAT_GRAYSCALE);
  if (fb->len != (size_t)(width * height)) {
    Serial.println("WARNING: fb->len != width * height, not a grayscale frame");
  }
  esp_camera_fb_return(fb);

  MotionParams params;
  params.pixel_threshold = MOTION_PIXEL_THRESHOLD;
  params.area_percent = MOTION_AREA_PERCENT;
  params.bg_alpha = MOTION_BG_ALPHA;
  params.confirm_frames = MOTION_CONFIRM_FRAMES;
  params.cooldown_ms = MOTION_COOLDOWN_MS;
  params.light_change_percent = MOTION_LIGHT_CHANGE_PERCENT;
  params.normalize_brightness = MOTION_NORMALIZE_BRIGHTNESS;
  if (!detector.begin(width, height, MOTION_BLOCK_SIZE, params)) {
    Serial.println("Motion detector init failed");
    return;
  }
  Serial.printf("Motion: grid %dx%d (block %d), pixel %d, area %.1f%%, alpha %.3f, confirm %d, cooldown %u ms\n",
                detector.cols(), detector.rows(), MOTION_BLOCK_SIZE, params.pixel_threshold,
                params.area_percent, params.bg_alpha, params.confirm_frames, params.cooldown_ms);

#if MOTION_FLASH_LED && defined(LED_GPIO_NUM)
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);
#endif

#if MOTION_WEB
  IPAddress ip = wifiInit();
  webStart(&detector, width, height);
  Serial.print("Web: http://");
  Serial.println(ip);
#endif

  Serial.printf("Warming up %d ms...\n", MOTION_WARMUP_MS);
  start_ms = millis();
  stat_start_ms = start_ms;
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(100);
    return;
  }
  uint32_t now = millis();

  // Chờ auto exposure ổn định; nền lấy từ khung đầu tiên sau giai đoạn này
  if (now - start_ms < MOTION_WARMUP_MS) {
    esp_camera_fb_return(fb);
    return;
  }

  uint32_t t0 = micros();
  MotionResult r = detector.process(fb->buf, now);
  uint32_t process_us = micros() - t0;

#if MOTION_WEB
  webPublish(fb, r, fps, process_us);
#endif
  esp_camera_fb_return(fb);

  if (r.alarm) {
    Serial.printf("MOTION! %.1f%% blocks changed (%d), brightness %.0f\n", r.changed_percent, r.changed, r.mean);
#if MOTION_FLASH_LED && defined(LED_GPIO_NUM)
    digitalWrite(LED_GPIO_NUM, HIGH);
    led_off_ms = now + MOTION_LED_ON_MS;
#endif
  }
  if (r.light_change) {
    Serial.printf("Light change: %.1f%% blocks changed, background reset\n", r.changed_percent);
  }
#if MOTION_FLASH_LED && defined(LED_GPIO_NUM)
  if (led_off_ms && (int32_t)(now - led_off_ms) >= 0) {
    digitalWrite(LED_GPIO_NUM, LOW);
    led_off_ms = 0;
  }
#endif

  stat_frames++;
  stat_process_us += process_us;
  if (r.changed_percent > stat_max_changed) {
    stat_max_changed = r.changed_percent;
  }
#if MOTION_PLOT
  // Serial Plotter: mỗi khung một dòng, vẽ % block thay đổi so với ngưỡng
  Serial.printf("changed:%.1f threshold:%.1f\n", r.changed_percent, detector.params.area_percent);
#endif
  if (now - stat_start_ms >= 1000) {
    fps = stat_frames * 1000.0f / (now - stat_start_ms);
#if !MOTION_PLOT
    Serial.printf("%.1f fps, process %u us/frame, brightness %.0f, changed max %.1f%%\n",
                  fps, (unsigned)(stat_process_us / stat_frames), r.mean, stat_max_changed);
#endif
    stat_start_ms = now;
    stat_frames = 0;
    stat_process_us = 0;
    stat_max_changed = 0;
  }
}
