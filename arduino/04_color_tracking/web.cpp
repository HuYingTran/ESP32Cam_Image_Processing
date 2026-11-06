#include "web.h"

#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "lesson_config.h"

static ColorTracker *tracker = nullptr;
static int frame_w = 0, frame_h = 0;

// Dữ liệu dùng chung giữa loop() và task web server, bảo vệ bằng lock
static SemaphoreHandle_t lock = nullptr;
static uint8_t *jpg_buf = nullptr;
static size_t jpg_len = 0;
static uint32_t jpg_seq = 0;            // tăng mỗi khi có ảnh mới
static ColorResult jpg_result = {};     // kết quả của đúng khung trong jpg_buf
static uint32_t last_request_ms = 0;    // lần cuối trình duyệt xin ảnh
static int view = 0;                    // 0 = ảnh, 1 = mask
static ColorResult last_result = {};
static FrameStats last_stats = {};
static bool awb = COLOR_AWB;
// Tham số sửa trên web được loop() áp dụng ở khung kế tiếp
static ColorParams params;
static bool params_dirty = false;
static bool pick_pending = false;
static int pick_x = 0, pick_y = 0;

static const char INDEX_HTML[] = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Color Tracking</title>
<style>
body{font-family:system-ui,sans-serif;margin:16px;background:#181818;color:#eee}
.view{position:relative;width:100%;max-width:640px}
.view img,.view canvas{width:100%;display:block}
.view canvas{position:absolute;left:0;top:0;height:100%;cursor:crosshair}
#state{font-size:20px;font-weight:600;margin:8px 0}.on{color:#69f0ae}
table{border-collapse:collapse;margin-top:8px}td{padding:3px 8px}input{width:70px}
small{color:#aaa}
</style></head><body>
<h2>#4 Color Tracking</h2>
<div class="view"><img id="img"><canvas id="ov" title="Bấm vào vật để lấy màu"></canvas></div>
<div id="state">-</div>
<div id="stats"></div>
<p><small>Bấm vào vật trên ảnh để lấy màu làm ngưỡng. HSV giữa khung: <span id="center"></span></small></p>
<p>Hiển thị <select id="view"><option value="0">Ảnh</option><option value="1">Mask</option></select>
 &nbsp; <label><input id="morph" type="checkbox" style="width:auto"> Erode/Dilate</label>
 &nbsp; <label><input id="awb" type="checkbox" style="width:auto"> Auto WB</label></p>
<table>
<tr><td>H (0-359)</td><td><input id="hmin" type="number"> → <input id="hmax" type="number"></td></tr>
<tr><td>S (0-255)</td><td><input id="smin" type="number"> → <input id="smax" type="number"></td></tr>
<tr><td>V (0-255)</td><td><input id="vmin" type="number"> → <input id="vmax" type="number"></td></tr>
<tr><td>Diện tích tối thiểu</td><td><input id="area" type="number"></td></tr>
</table>
<script>
const $=id=>document.getElementById(id);
let info=null,url=null;
const nums=['hmin','hmax','smin','smax','vmin','vmax','area'],checks=['morph','awb'];
const set=(v,val)=>fetch(`/set?var=${v}&val=${val}`);
nums.forEach(p=>$(p).onchange=e=>set(p,e.target.value));
checks.forEach(p=>$(p).onchange=e=>set(p,e.target.checked?1:0));
$('view').onchange=e=>set('view',e.target.value);
$('ov').onclick=e=>{
  if(!info)return;
  const x=Math.round(e.offsetX*info.w/e.target.clientWidth),y=Math.round(e.offsetY*info.h/e.target.clientHeight);
  fetch(`/pick?x=${x}&y=${y}`).then(()=>setTimeout(()=>info=null,300));
};
async function status(){
  try{
    const s=await (await fetch('/status')).json();
    if(!info){
      info=s;
      nums.forEach(p=>$(p).value=s[p=='area'?'area_min':p]);checks.forEach(p=>$(p).checked=s[p]);$('view').value=s.view;
    }
    $('state').textContent=s.found?`Thấy vật: tâm (${s.cx}, ${s.cy}), ${s.area} px`:`Không thấy (${s.area} px)`;
    $('state').className=s.found?'on':'';
    $('stats').textContent=`${s.fps.toFixed(1)} fps · giải nén ${(s.decode_us/1000).toFixed(1)} ms · mask ${(s.mask_us/1000).toFixed(1)} ms · morph ${(s.morph_us/1000).toFixed(1)} ms · moment ${(s.moment_us/1000).toFixed(1)} ms · JPEG ${(s.jpeg_us/1000).toFixed(1)} ms`;
    $('center').textContent=`H ${s.ch}, S ${s.cs}, V ${s.cv}`;
  }catch(e){}
  setTimeout(status,500);
}
// Vẽ bbox + tâm; kết quả theo tọa độ ảnh xử lý (info.w x info.h), ảnh web có thể lớn hơn
function draw(res,w,h){
  const c=$('ov');if(c.width!=w)c.width=w;if(c.height!=h)c.height=h;
  const g=c.getContext('2d');g.clearRect(0,0,w,h);
  if(!info||!res[0])return;
  const k=w/info.w,[,cx,cy,x0,y0,x1,y1]=res;
  g.lineWidth=Math.max(2,k*1.5);g.strokeStyle='#00e676';
  g.strokeRect(x0*k,y0*k,(x1-x0+1)*k,(y1-y0+1)*k);
  g.strokeStyle='#ff4081';const d=8*k;
  g.beginPath();g.moveTo(cx*k-d,cy*k);g.lineTo(cx*k+d,cy*k);g.moveTo(cx*k,cy*k-d);g.lineTo(cx*k,cy*k+d);g.stroke();
}
async function frame(){
  try{
    const r=await fetch('/frame.jpg?'+Date.now());
    if(r.ok){
      const res=(r.headers.get('X-Result')||'0').split(',').map(Number);
      const b=await r.blob();if(url)URL.revokeObjectURL(url);url=URL.createObjectURL(b);
      const img=$('img');img.src=url;await img.decode().catch(()=>{});
      draw(res,img.naturalWidth,img.naturalHeight);
    }
  }catch(e){}
  setTimeout(frame,20);
}
status();frame();
</script></body></html>)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

static esp_err_t frame_handler(httpd_req_t *req) {
  // Báo loop() có người xem, rồi chờ ảnh mới (tối đa 1 s)
  xSemaphoreTake(lock, portMAX_DELAY);
  last_request_ms = millis();
  uint32_t seq = jpg_seq;
  xSemaphoreGive(lock);
  for (int i = 0; i < 100; i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
    xSemaphoreTake(lock, portMAX_DELAY);
    bool fresh = jpg_seq != seq;
    xSemaphoreGive(lock);
    if (fresh) {
      break;
    }
  }

  // Chép ảnh ra rồi nhả lock, gửi qua mạng ngoài lock
  xSemaphoreTake(lock, portMAX_DELAY);
  size_t len = jpg_len;
  uint8_t *copy = len ? (uint8_t *)malloc(len) : nullptr;
  if (copy) {
    memcpy(copy, jpg_buf, len);
  }
  const ColorResult &jr = jpg_result;
  char result[64];
  snprintf(result, sizeof(result), "%d,%d,%d,%d,%d,%d,%d", jr.found, jr.cx, jr.cy, jr.x0, jr.y0, jr.x1, jr.y1);
  xSemaphoreGive(lock);
  if (!copy) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Result", result);
  esp_err_t res = httpd_resp_send(req, (const char *)copy, len);
  free(copy);
  return res;
}

static esp_err_t status_handler(httpd_req_t *req) {
  char json[640];
  xSemaphoreTake(lock, portMAX_DELAY);
  const ColorResult &r = last_result;
  const FrameStats &s = last_stats;
  snprintf(json, sizeof(json),
           "{\"w\":%d,\"h\":%d,\"found\":%d,\"area\":%d,\"cx\":%d,\"cy\":%d,"
           "\"x0\":%d,\"y0\":%d,\"x1\":%d,\"y1\":%d,"
           "\"fps\":%.1f,\"decode_us\":%u,\"mask_us\":%u,\"morph_us\":%u,\"moment_us\":%u,\"jpeg_us\":%u,"
           "\"ch\":%d,\"cs\":%d,\"cv\":%d,"
           "\"hmin\":%d,\"hmax\":%d,\"smin\":%d,\"smax\":%d,\"vmin\":%d,\"vmax\":%d,"
           "\"area_min\":%d,\"morph\":%d,\"view\":%d,\"awb\":%d}",
           frame_w, frame_h, r.found, r.area, r.cx, r.cy, r.x0, r.y0, r.x1, r.y1,
           s.fps, s.decode_us, r.mask_us, r.morph_us, r.moment_us, s.jpeg_us,
           s.center_h, s.center_s, s.center_v,
           params.h_min, params.h_max, params.s_min, params.s_max, params.v_min, params.v_max,
           params.min_area, params.morph, view, awb);
  xSemaphoreGive(lock);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static esp_err_t set_handler(httpd_req_t *req) {
  char query[64], var[16], val[16];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) {
    return httpd_resp_send_404(req);
  }
  int v = atoi(val);
  bool ok = true;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!strcmp(var, "hmin")) params.h_min = clampi(v, 0, 359);
  else if (!strcmp(var, "hmax")) params.h_max = clampi(v, 0, 359);
  else if (!strcmp(var, "smin")) params.s_min = clampi(v, 0, 255);
  else if (!strcmp(var, "smax")) params.s_max = clampi(v, 0, 255);
  else if (!strcmp(var, "vmin")) params.v_min = clampi(v, 0, 255);
  else if (!strcmp(var, "vmax")) params.v_max = clampi(v, 0, 255);
  else if (!strcmp(var, "area")) params.min_area = v < 0 ? 0 : v;
  else if (!strcmp(var, "morph")) params.morph = v != 0;
  else if (!strcmp(var, "view")) view = v ? 1 : 0;
  else if (!strcmp(var, "awb")) {
    awb = v != 0;
    sensor_t *sensor = esp_camera_sensor_get();
    sensor->set_whitebal(sensor, awb);
    sensor->set_awb_gain(sensor, awb);
  }
  else ok = false;
  params_dirty = params_dirty || ok;
  xSemaphoreGive(lock);
  if (!ok) {
    return httpd_resp_send_404(req);
  }
  Serial.printf("Set %s = %d\n", var, v);
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t pick_handler(httpd_req_t *req) {
  char query[48], x[8], y[8];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "x", x, sizeof(x)) != ESP_OK ||
      httpd_query_key_value(query, "y", y, sizeof(y)) != ESP_OK) {
    return httpd_resp_send_404(req);
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  pick_x = clampi(atoi(x), 0, frame_w - 1);
  pick_y = clampi(atoi(y), 0, frame_h - 1);
  pick_pending = true;
  xSemaphoreGive(lock);
  return httpd_resp_send(req, "OK", 2);
}

void webStart(ColorTracker *t, int width, int height) {
  tracker = t;
  frame_w = width;
  frame_h = height;
  params = t->params;
  lock = xSemaphoreCreateMutex();

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
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
      {"/pick", HTTP_GET, pick_handler, nullptr},
  };
  for (auto &u : uris) {
    httpd_register_uri_handler(server, &u);
  }
}

void webApply(const uint8_t *rgb565) {
  if (!lock) {
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  if (pick_pending) {
    int h, s, v;
    tracker->sampleHsv(rgb565, pick_x, pick_y, &h, &s, &v);
    params.h_min = (h - COLOR_PICK_H_TOL + 360) % 360;
    params.h_max = (h + COLOR_PICK_H_TOL) % 360;
    params.s_min = s / 2;
    params.s_max = 255;
    params.v_min = v / 2;
    params.v_max = 255;
    params_dirty = true;
    pick_pending = false;
    Serial.printf("Pick (%d, %d): H %d S %d V %d → H %d..%d, S >= %d, V >= %d\n",
                  pick_x, pick_y, h, s, v, params.h_min, params.h_max, params.s_min, params.v_min);
  }
  if (params_dirty) {
    tracker->params = params;
    params_dirty = false;
  }
  xSemaphoreGive(lock);
}

bool webWantsFrame(int *v) {
  if (!lock) {
    return false;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  bool wants = last_request_ms && millis() - last_request_ms < 2000;
  *v = view;
  xSemaphoreGive(lock);
  return wants;
}

void webPublish(const ColorResult &r, const FrameStats &s, uint8_t *jpg, size_t len) {
  if (!lock) {
    free(jpg);
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  last_result = r;
  last_stats = s;
  if (jpg) {
    free(jpg_buf);
    jpg_buf = jpg;
    jpg_len = len;
    jpg_result = r;
    jpg_seq++;
  }
  xSemaphoreGive(lock);
}
