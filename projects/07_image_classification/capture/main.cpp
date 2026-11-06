// Bài #7 - Firmware thu ảnh để train trên Edge Impulse (env 07_capture)
// Thu ảnh bằng chính ESP32-CAM: màu, độ nét, nhiễu giống hệt lúc chạy model.
//   /             trang web: xem trước, khung vuông giữa ảnh (phần còn lại sau khi cắt về 1:1), đèn flash
//   /capture.jpg  một ảnh JPEG mới (tools/capture_images.py gọi endpoint này)
//   /flash?val=0..255  độ sáng đèn flash
// Cấu hình: common/config.h (WiFi, camera) và capture/capture_config.h.

#include <Arduino.h>
#include "camera_init.h"  // common/esp32cam
#include "wifi_init.h"
#include "capture_config.h"
#include "camera_pins.h"  // sau capture_config.h: cần CAMERA_MODEL_* trong config.h
#include "esp_http_server.h"

// Đèn flash GPIO4 điều khiển bằng PWM; timer 0 đã dùng cho XCLK camera nên dùng kênh 2 (timer 1)
#define FLASH_LEDC_CHANNEL 2

static int flash_duty = CAP_FLASH_DUTY;
static uint32_t captured = 0;

static const char INDEX_HTML[] = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Capture</title>
<style>
body{font-family:system-ui,sans-serif;margin:16px;background:#181818;color:#eee}
.view{position:relative;width:100%;max-width:640px}
.view img{width:100%;display:block}
#sq{position:absolute;top:0;height:100%;border:2px dashed #69f0ae;box-sizing:border-box}
small{color:#aaa}
</style></head><body>
<h2>#7 Thu ảnh</h2>
<div class="view"><img id="img"><div id="sq"></div></div>
<p id="info">-</p>
<p>Đèn flash <input id="flash" type="range" min="0" max="255"> <span id="fv"></span></p>
<p><small>Khung nét đứt: phần giữ lại nếu Edge Impulse cắt ảnh về hình vuông (Resize mode "Fit shortest axis").
Đặt vật vào trong khung. Tải ảnh theo nhãn bằng <code>tools/capture_images.py</code>.</small></p>
<script>
const $=id=>document.getElementById(id);
$('flash').value=%FLASH%;$('fv').textContent=%FLASH%;
$('flash').oninput=e=>{$('fv').textContent=e.target.value;fetch('/flash?val='+e.target.value)};
let url=null;
async function loop(){
  try{
    const r=await fetch('/capture.jpg?'+Date.now());
    const b=await r.blob();
    if(url)URL.revokeObjectURL(url);
    url=URL.createObjectURL(b);
    const img=$('img');
    img.onload=()=>{
      const w=img.naturalWidth,h=img.naturalHeight,s=Math.min(w,h);
      $('sq').style.left=((w-s)/2/w*100)+'%';$('sq').style.width=(s/w*100)+'%';
      $('info').textContent=`${w}×${h}, ${(b.size/1024).toFixed(1)} KB`;
    };
    img.src=url;
  }catch(e){}
  setTimeout(loop,300);
}
loop();
</script></body></html>)rawliteral";

static void flashSet(int duty) {
  flash_duty = constrain(duty, 0, 255);
#if defined(LED_GPIO_NUM)
  ledcWrite(FLASH_LEDC_CHANNEL, flash_duty);
#endif
}

static esp_err_t index_handler(httpd_req_t *req) {
  // Chèn độ sáng hiện tại vào trang (thay %FLASH%)
  String html(INDEX_HTML);
  html.replace("%FLASH%", String(flash_duty));
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html.c_str(), html.length());
}

static esp_err_t capture_handler(httpd_req_t *req) {
  // Bỏ khung đang nằm sẵn trong buffer (có thể chụp trước khi vật được đặt xong), lấy khung kế tiếp
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return httpd_resp_send_500(req);
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  captured++;
  return res;
}

static esp_err_t flash_handler(httpd_req_t *req) {
  char query[32], val[8];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) {
    return httpd_resp_send_404(req);
  }
  flashSet(atoi(val));
  return httpd_resp_send(req, "OK", 2);
}

static void webStart() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("Web server start failed");
    return;
  }
  httpd_uri_t uris[] = {
      {"/", HTTP_GET, index_handler, nullptr},
      {"/capture.jpg", HTTP_GET, capture_handler, nullptr},
      {"/flash", HTTP_GET, flash_handler, nullptr},
  };
  for (auto &u : uris) {
    httpd_register_uri_handler(server, &u);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (!cameraInit(PIXFORMAT_JPEG, CAP_FRAME_SIZE)) {
    Serial.println("Camera init failed");
    return;
  }
  sensor_t *s = esp_camera_sensor_get();
  s->set_quality(s, CAP_JPEG_QUALITY);
#if CAP_LOCK_EXPOSURE
  // Cho camera tự chỉnh 2 s rồi khóa, để mọi ảnh cùng độ sáng / màu
  delay(2000);
  s->set_whitebal(s, 0);
  s->set_exposure_ctrl(s, 0);
  s->set_gain_ctrl(s, 0);
#endif

#if defined(LED_GPIO_NUM)
  ledcSetup(FLASH_LEDC_CHANNEL, 5000, 8);
  ledcAttachPin(LED_GPIO_NUM, FLASH_LEDC_CHANNEL);
  flashSet(flash_duty);
#endif

  IPAddress ip = wifiInit();
  webStart();
  Serial.printf("Capture ready: http://%s  (ảnh: http://%s/capture.jpg)\n", ip.toString().c_str(),
                ip.toString().c_str());
}

void loop() {
  static uint32_t last_print = 0;
  static uint32_t last_count = 0;
  if (millis() - last_print > 10000) {
    if (captured != last_count) {
      Serial.printf("Captured %lu frames\n", (unsigned long)captured);
      last_count = captured;
    }
    last_print = millis();
  }
  delay(100);
}
