#include "web.h"

#include <Arduino.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "lesson_config.h"

static MotionDetector *detector = nullptr;
static int frame_w = 0, frame_h = 0, block_count = 0;

// Dữ liệu dùng chung giữa loop() và task web server, bảo vệ bằng lock
static SemaphoreHandle_t lock = nullptr;
static uint8_t *frame = nullptr;  // bản sao khung xám mới nhất
static bool has_frame = false;
static char *mask_str = nullptr;  // "0101...", block_count ký tự
static MotionResult last_result = {};
static float last_fps = 0;
static uint32_t last_process_us = 0;
static uint32_t alarm_count = 0;
static uint32_t last_alarm_ms = 0;
// Tham số sửa trên web được loop() áp dụng ở khung kế tiếp, để không đổi giữa lúc đang xử lý
static MotionParams params;
static bool params_dirty = false;
static bool reset_requested = false;

static const char INDEX_HTML[] = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Motion Detection</title>
<style>
body{font-family:system-ui,sans-serif;margin:16px;background:#181818;color:#eee}
.view{position:relative;width:100%;max-width:640px}
.view img,.view canvas{width:100%;display:block;image-rendering:pixelated}
.view canvas{position:absolute;left:0;top:0}
#state{font-size:20px;font-weight:600;margin:8px 0}
.on{color:#ff5252}.light{color:#ffd740}
table{border-collapse:collapse;margin-top:8px}td{padding:3px 8px}
input{width:80px}
</style></head><body>
<h2>#3 Motion Detection</h2>
<div class="view"><img id="img"><canvas id="ov"></canvas></div>
<div id="state">-</div>
<div id="stats"></div>
<table>
<tr><td>Ngưỡng pixel (0-255)</td><td><input id="pixel" type="number"></td></tr>
<tr><td>Ngưỡng diện tích (%)</td><td><input id="area" type="number" step="0.5"></td></tr>
<tr><td>Alpha nền</td><td><input id="alpha" type="number" step="0.01"></td></tr>
<tr><td>Số khung xác nhận</td><td><input id="confirm" type="number"></td></tr>
<tr><td>Cooldown (ms)</td><td><input id="cooldown" type="number" step="500"></td></tr>
<tr><td>Đổi ánh sáng (%)</td><td><input id="light" type="number"></td></tr>
<tr><td>Bù độ sáng</td><td><input id="normalize" type="checkbox"></td></tr>
</table>
<p><button id="reset">Lấy lại nền</button></p>
<script>
const $=id=>document.getElementById(id);
let info=null,url=null;
const params=['pixel','area','alpha','confirm','cooldown','light','normalize'];
function set(v,val){fetch(`/set?var=${v}&val=${val}`)}
params.forEach(p=>$(p).onchange=e=>set(p,e.target.type=='checkbox'?(e.target.checked?1:0):e.target.value));
$('reset').onclick=()=>set('reset',1);
async function status(){
  try{
    const s=await (await fetch('/status')).json();
    if(!info){
      info=s;$('ov').width=s.w;$('ov').height=s.h;
      params.forEach(p=>{if(p=='normalize')$(p).checked=s[p];else $(p).value=s[p]});
    }
    const st=$('state');
    st.textContent=s.light_change?'Đổi ánh sáng':s.motion?'CÓ CHUYỂN ĐỘNG':'Yên tĩnh';
    st.className=s.light_change?'light':s.motion?'on':'';
    $('stats').textContent=`${s.fps.toFixed(1)} fps · xử lý ${s.process_us} µs · độ sáng ${s.mean.toFixed(0)} · thay đổi ${s.changed.toFixed(1)}% · đã báo ${s.alarms} lần`;
  }catch(e){}
  setTimeout(status,500);
}
async function frame(){
  try{
    const r=await fetch('/frame.jpg?'+Date.now());
    const mask=r.headers.get('X-Mask')||'';
    const b=await r.blob();
    if(url)URL.revokeObjectURL(url);
    url=URL.createObjectURL(b);$('img').src=url;
    if(info){
      const c=$('ov').getContext('2d');c.clearRect(0,0,info.w,info.h);
      c.fillStyle='rgba(255,40,40,0.45)';
      for(let i=0;i<mask.length;i++)if(mask[i]=='1')
        c.fillRect((i%info.cols)*info.block,Math.floor(i/info.cols)*info.block,info.block,info.block);
    }
  }catch(e){}
  setTimeout(frame,100);
}
status();frame();
</script></body></html>)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

static esp_err_t frame_handler(httpd_req_t *req) {
  // Chép khung ra rồi nhả lock ngay, nén JPEG ngoài lock để không chặn loop()
  uint8_t *copy = (uint8_t *)ps_malloc(frame_w * frame_h);
  char *mask = (char *)malloc(block_count + 1);
  if (!copy || !mask) {
    free(copy);
    free(mask);
    return httpd_resp_send_500(req);
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = has_frame;
  memcpy(copy, frame, frame_w * frame_h);
  memcpy(mask, mask_str, block_count + 1);
  xSemaphoreGive(lock);

  uint8_t *jpg = nullptr;
  size_t jpg_len = 0;
  if (ok) {
    ok = fmt2jpg(copy, frame_w * frame_h, frame_w, frame_h, PIXFORMAT_GRAYSCALE, 80, &jpg, &jpg_len);
  }
  free(copy);
  if (!ok) {
    free(mask);
    return httpd_resp_send_500(req);
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "X-Mask", mask);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t res = httpd_resp_send(req, (const char *)jpg, jpg_len);
  free(jpg);
  free(mask);
  return res;
}

static esp_err_t status_handler(httpd_req_t *req) {
  char json[512];
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(json, sizeof(json),
           "{\"w\":%d,\"h\":%d,\"cols\":%d,\"rows\":%d,\"block\":%d,"
           "\"fps\":%.1f,\"process_us\":%u,\"mean\":%.1f,\"changed\":%.1f,\"motion\":%d,\"light_change\":%d,"
           "\"alarms\":%u,\"last_alarm_ms\":%u,"
           "\"pixel\":%d,\"area\":%.1f,\"alpha\":%.3f,\"confirm\":%d,\"cooldown\":%u,"
           "\"light\":%.0f,\"normalize\":%d}",
           frame_w, frame_h, detector->cols(), detector->rows(), MOTION_BLOCK_SIZE,
           last_fps, last_process_us, last_result.mean, last_result.changed_percent,
           last_result.motion, last_result.light_change, alarm_count, last_alarm_ms,
           params.pixel_threshold, params.area_percent, params.bg_alpha, params.confirm_frames,
           params.cooldown_ms, params.light_change_percent,
           params.normalize_brightness);
  xSemaphoreGive(lock);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t set_handler(httpd_req_t *req) {
  char query[64], var[16], val[16];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) {
    return httpd_resp_send_404(req);
  }
  float v = atof(val);
  bool ok = true;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!strcmp(var, "pixel")) params.pixel_threshold = (int)v;
  else if (!strcmp(var, "area")) params.area_percent = v;
  else if (!strcmp(var, "alpha") && v >= 0 && v <= 1) params.bg_alpha = v;
  else if (!strcmp(var, "confirm") && v >= 1) params.confirm_frames = (int)v;
  else if (!strcmp(var, "cooldown") && v >= 0) params.cooldown_ms = (uint32_t)v;
  else if (!strcmp(var, "light")) params.light_change_percent = v;
  else if (!strcmp(var, "normalize")) params.normalize_brightness = v != 0;
  else if (!strcmp(var, "reset")) reset_requested = true;
  else ok = false;
  params_dirty = ok;
  xSemaphoreGive(lock);
  if (!ok) {
    return httpd_resp_send_404(req);
  }
  Serial.printf("Set %s = %s\n", var, val);
  return httpd_resp_send(req, "OK", 2);
}

void webStart(MotionDetector *det, int width, int height) {
  detector = det;
  frame_w = width;
  frame_h = height;
  block_count = det->cols() * det->rows();
  params = det->params;
  lock = xSemaphoreCreateMutex();
  frame = (uint8_t *)ps_malloc(width * height);
  mask_str = (char *)calloc(block_count + 1, 1);
  memset(mask_str, '0', block_count);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;  // fmt2jpg cần nhiều stack hơn mặc định 4096
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("Web server start failed");
    return;
  }
  httpd_uri_t uris[] = {
      {"/", HTTP_GET, index_handler, nullptr},
      {"/frame.jpg", HTTP_GET, frame_handler, nullptr},
      {"/status", HTTP_GET, status_handler, nullptr},
      {"/set", HTTP_GET, set_handler, nullptr},
  };
  for (auto &u : uris) {
    httpd_register_uri_handler(server, &u);
  }
}

void webPublish(const camera_fb_t *fb, const MotionResult &r, float fps, uint32_t process_us) {
  if (!lock || !frame) {
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(frame, fb->buf, frame_w * frame_h);
  has_frame = true;
  const uint8_t *mask = detector->mask();
  for (int i = 0; i < block_count; i++) {
    mask_str[i] = mask[i] ? '1' : '0';
  }
  last_result = r;
  last_fps = fps;
  last_process_us = process_us;
  if (r.alarm) {
    alarm_count++;
    last_alarm_ms = millis();
  }
  if (params_dirty) {
    detector->params = params;
    params_dirty = false;
  }
  if (reset_requested) {
    detector->reset();
    reset_requested = false;
  }
  xSemaphoreGive(lock);
}
