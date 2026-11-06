// Bài #4 - Bám màu, đếm pixel theo ngưỡng
// Lọc pixel theo ngưỡng HSV, erode/dilate, tính trọng tâm và khung bao.
// Nguồn ảnh: JPEG độ phân giải cao giải nén thu nhỏ (mặc định), hoặc RGB565 trực tiếp từ camera.
// Cấu hình: common/config.h (WiFi, camera) và src/lesson_config.h (nguồn ảnh, ngưỡng màu).

#include <Arduino.h>
#include "camera_init.h"  // common/esp32cam
#include "esp_jpg_decode.h"
#include "img_converters.h"
#include "lesson_config.h"
#include "color.h"
#if COLOR_WEB
#include "wifi_init.h"
#include "web.h"
#endif

static ColorTracker tracker;
static int proc_w = 0, proc_h = 0;  // kích thước ảnh xử lý

// Thống kê in mỗi giây
static uint32_t stat_start_ms = 0;
static uint32_t stat_frames = 0;
static float fps = 0;

static uint32_t clockUs() {
  return micros();
}

#if COLOR_CAPTURE_JPEG
// Ảnh RGB565 (byte cao trước, giống camera) giải nén từ JPEG, để ColorTracker dùng chung một định dạng
static uint8_t *rgb_buf = nullptr;

struct JpegDecodeCtx {
  const uint8_t *src;
  uint8_t *out;
  int out_w, out_h;  // kích thước sau khi thu nhỏ
};

static size_t jpegRead(void *arg, size_t index, uint8_t *buf, size_t len) {
  JpegDecodeCtx *ctx = (JpegDecodeCtx *)arg;
  if (buf) {
    memcpy(buf, ctx->src + index, len);
  }
  return len;
}

// Bộ giải nén gọi lần lượt từng khối pixel RGB888 (R, G, B); data == nullptr ở lần gọi đầu (báo kích thước) và cuối
static bool jpegWrite(void *arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
  JpegDecodeCtx *ctx = (JpegDecodeCtx *)arg;
  if (!data) {
    if (x == 0 && y == 0) {
      ctx->out_w = w;
      ctx->out_h = h;
    }
    return true;
  }
  if (!ctx->out) {
    return true;  // chỉ đang đo kích thước
  }
  // Khối ở mép phải/dưới có thể tràn ra ngoài ảnh: chỉ ghi phần nằm trong
  int cols = ctx->out_w - x < w ? ctx->out_w - x : w;
  int rows = ctx->out_h - y < h ? ctx->out_h - y : h;
  for (int j = 0; j < rows; j++) {
    const uint8_t *s = data + 3 * j * w;
    uint8_t *o = ctx->out + 2 * ((y + j) * ctx->out_w + x);
    for (int i = 0; i < cols; i++, s += 3, o += 2) {
      uint16_t c = ((s[0] & 0xF8) << 8) | ((s[1] & 0xFC) << 3) | (s[2] >> 3);
      o[0] = c >> 8;
      o[1] = c & 0xFF;
    }
  }
  return true;
}

static bool jpegDecode(const camera_fb_t *fb, uint8_t *out, int *out_w, int *out_h) {
  JpegDecodeCtx ctx = {fb->buf, out, out_w ? *out_w : 0, out_h ? *out_h : 0};
  if (esp_jpg_decode(fb->len, COLOR_DECODE_SCALE, jpegRead, jpegWrite, &ctx) != ESP_OK) {
    return false;
  }
  if (out_w) *out_w = ctx.out_w;
  if (out_h) *out_h = ctx.out_h;
  return true;
}
#endif

void setup() {
  Serial.begin(115200);
  Serial.println();

#if COLOR_CAPTURE_JPEG
  if (!cameraInit(PIXFORMAT_JPEG, COLOR_CAPTURE_SIZE)) {
    return;
  }
#else
  if (!cameraInit(PIXFORMAT_RGB565, COLOR_FRAME_SIZE)) {
    return;
  }
#endif
  sensor_t *sensor = esp_camera_sensor_get();
  sensor->set_saturation(sensor, COLOR_SATURATION);
  sensor->set_contrast(sensor, COLOR_CONTRAST);
#if COLOR_CAPTURE_JPEG
  sensor->set_quality(sensor, COLOR_JPEG_QUALITY);
#endif

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  int cap_w = fb->width, cap_h = fb->height;
  Serial.printf("Frame: %dx%d, %u bytes, format %d\n", cap_w, cap_h, (unsigned)fb->len, fb->format);
#if COLOR_CAPTURE_JPEG
  // Giải nén thử một lần (không ghi ra) để biết kích thước sau khi thu nhỏ
  bool ok = jpegDecode(fb, nullptr, &proc_w, &proc_h);
  esp_camera_fb_return(fb);
  if (!ok) {
    Serial.println("JPEG decode failed");
    proc_w = 0;
    return;
  }
  if (proc_w <= 0 || proc_h <= 0) {
    proc_w = cap_w >> COLOR_DECODE_SCALE;
    proc_h = cap_h >> COLOR_DECODE_SCALE;
  }
  rgb_buf = (uint8_t *)ps_malloc(proc_w * proc_h * 2);
  if (!rgb_buf) {
    Serial.println("Out of memory for RGB565 buffer");
    proc_w = 0;
    return;
  }
  Serial.printf("Source: JPEG %dx%d -> decode 1/%d -> process %dx%d RGB565\n",
                cap_w, cap_h, 1 << COLOR_DECODE_SCALE, proc_w, proc_h);
#else
  proc_w = fb->width;
  proc_h = fb->height;
  if (fb->len != (size_t)(proc_w * proc_h * 2)) {
    Serial.println("WARNING: fb->len != width * height * 2, not an RGB565 frame");
  }
  esp_camera_fb_return(fb);
  Serial.printf("Source: RGB565 %dx%d (PIXFORMAT_RGB565 = %d)\n", proc_w, proc_h, PIXFORMAT_RGB565);
#endif

#if !COLOR_AWB
  // Để AWB chạy một lúc cho màu đúng, rồi khóa lại
  delay(2000);
  sensor->set_whitebal(sensor, 0);
  sensor->set_awb_gain(sensor, 0);
  Serial.println("AWB locked");
#endif

  ColorParams params;
  params.h_min = COLOR_H_MIN;
  params.h_max = COLOR_H_MAX;
  params.s_min = COLOR_S_MIN;
  params.s_max = COLOR_S_MAX;
  params.v_min = COLOR_V_MIN;
  params.v_max = COLOR_V_MAX;
  params.min_area = COLOR_MIN_AREA;
  params.morph = COLOR_MORPH;
  if (!tracker.begin(proc_w, proc_h, params)) {
    Serial.println("Color tracker init failed (out of memory)");
    proc_w = 0;
    return;
  }
  Serial.printf("Color: H %d..%d, S %d..%d, V %d..%d, min area %d, morph %d\n",
                params.h_min, params.h_max, params.s_min, params.s_max,
                params.v_min, params.v_max, params.min_area, params.morph);

#if COLOR_WEB
  IPAddress ip = wifiInit();
  webStart(&tracker, proc_w, proc_h);
  Serial.print("Web: http://");
  Serial.println(ip);
#endif
  stat_start_ms = millis();
}

void loop() {
  if (!proc_w) {
    delay(1000);  // setup() lỗi
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(100);
    return;
  }

  FrameStats st = {};
  st.fps = fps;
#if COLOR_CAPTURE_JPEG
  uint32_t t0 = micros();
  int w = proc_w, h = proc_h;
  if (!jpegDecode(fb, rgb_buf, &w, &h)) {
    Serial.println("JPEG decode failed");
    esp_camera_fb_return(fb);
    return;
  }
  st.decode_us = micros() - t0;
  const uint8_t *rgb = rgb_buf;
#else
  const uint8_t *rgb = fb->buf;
#endif

#if COLOR_WEB
  webApply(rgb);
#endif
  ColorResult r = tracker.process(rgb, clockUs);
  tracker.sampleHsv(rgb, proc_w / 2, proc_h / 2, &st.center_h, &st.center_s, &st.center_v);

#if COLOR_WEB
  // Chỉ chuẩn bị ảnh khi có trình duyệt đang xem; khung bao do trình duyệt tự vẽ
  int view = 0;
  uint8_t *jpg = nullptr;
  size_t jpg_len = 0;
  if (webWantsFrame(&view)) {
    uint32_t t1 = micros();
    bool ok;
    if (view == 1) {
      ok = fmt2jpg((uint8_t *)tracker.mask(), proc_w * proc_h, proc_w, proc_h,
                   PIXFORMAT_GRAYSCALE, COLOR_ENCODE_QUALITY, &jpg, &jpg_len);
    } else {
#if COLOR_CAPTURE_JPEG
      // Gửi thẳng ảnh JPEG gốc của camera, không nén lại
      jpg = (uint8_t *)malloc(fb->len);
      ok = jpg != nullptr;
      if (ok) {
        memcpy(jpg, fb->buf, fb->len);
        jpg_len = fb->len;
      }
#else
      ok = frame2jpg(fb, COLOR_ENCODE_QUALITY, &jpg, &jpg_len);
#endif
    }
    st.jpeg_us = micros() - t1;
    if (!ok) {
      Serial.println("JPEG for web failed");
      jpg = nullptr;
    }
  }
  webPublish(r, st, jpg, jpg_len);
#endif
  esp_camera_fb_return(fb);

  stat_frames++;
  uint32_t now = millis();
  if (now - stat_start_ms >= 1000) {
    fps = stat_frames * 1000.0f / (now - stat_start_ms);
    Serial.printf("%.1f fps | decode %.1f ms, mask %.1f ms, morph %.1f ms, moment %.1f ms, web %.1f ms | ",
                  fps, st.decode_us / 1000.0f, r.mask_us / 1000.0f, r.morph_us / 1000.0f,
                  r.moment_us / 1000.0f, st.jpeg_us / 1000.0f);
    if (r.found) {
      Serial.printf("FOUND center (%d, %d) box (%d,%d)-(%d,%d) area %d",
                    r.cx, r.cy, r.x0, r.y0, r.x1, r.y1, r.area);
    } else {
      Serial.printf("not found (%d px)", r.area);
    }
    Serial.printf(" | center HSV %d %d %d\n", st.center_h, st.center_s, st.center_v);
    stat_start_ms = now;
    stat_frames = 0;
  }
}
