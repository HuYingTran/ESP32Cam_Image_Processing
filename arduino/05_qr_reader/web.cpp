#include "web.h"

#include <Arduino.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "lesson_config.h"

// Dữ liệu dùng chung giữa task quét, loop() và task web server, bảo vệ bằng lock
static SemaphoreHandle_t lock = nullptr;
static uint8_t *frame = nullptr;  // bản sao khung xám, cấp theo cỡ lớn nhất
static int frame_w = 0, frame_h = 0;
static uint32_t frame_seq = 0;
static QrCorners frame_codes = {};
static uint32_t last_request_ms = 0;

static QrResult last_result = {};
static uint32_t result_count = 0;
static int last_granted = -1;
static char last_error[40] = "";
static uint32_t last_error_ms = 0;

static const char INDEX_HTML[] = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>QR Reader</title>
<style>
body{font-family:system-ui,sans-serif;margin:16px;background:#181818;color:#eee}
.view{position:relative;width:100%;max-width:640px}
.view img,.view canvas{width:100%;display:block}
.view canvas{position:absolute;left:0;top:0;height:100%}
#text{font-size:22px;font-weight:600;margin:10px 0;word-break:break-all}
.ok{color:#69f0ae}.deny{color:#ff5252}.err{color:#ffd740}
#hist{font-family:monospace;font-size:13px;color:#bbb;white-space:pre-wrap}
td{padding:3px 8px}
</style></head><body>
<h2>#5 QR Reader</h2>
<div class="view"><img id="img"><canvas id="ov"></canvas></div>
<div id="text">Chưa đọc được mã nào</div>
<div id="err" class="err"></div>
<div id="stats"></div>
<table>
<tr><td>Độ phân giải</td><td><select id="framesize">
<option value="5">QVGA 320x240</option><option value="6">CIF 400x296</option>
<option value="8">VGA 640x480</option><option value="9">SVGA 800x600</option></select></td></tr>
<tr><td>Đèn flash</td><td><input id="flash" type="range" min="0" max="255"> <span id="flashv"></span></td></tr>
<tr><td>Tương phản</td><td><select id="contrast">
<option>-2</option><option>-1</option><option>0</option><option>1</option><option>2</option></select></td></tr>
</table>
<h3>Lịch sử</h3><div id="hist"></div>
<script>
const $=id=>document.getElementById(id);
let info=null,url=null,seq=0;const hist=[];
const set=(v,val)=>fetch(`/set?var=${v}&val=${val}`);
['framesize','contrast'].forEach(p=>$(p).onchange=e=>set(p,e.target.value));
$('flash').oninput=e=>{$('flashv').textContent=e.target.value};
$('flash').onchange=e=>set('flash',e.target.value);
async function status(){
  try{
    const s=await (await fetch('/status')).json();
    if(!info){info=s;$('framesize').value=s.framesize;$('flash').value=s.flash;$('flashv').textContent=s.flash;$('contrast').value=s.contrast;}
    if(s.count!=seq&&s.count>0){
      seq=s.count;
      const g=s.granted==1?' → MỞ CỬA':s.granted==0?' → TỪ CHỐI':'';
      $('text').textContent=s.text+g;$('text').className=s.granted==0?'deny':'ok';
      hist.unshift(`#${s.count} ${new Date().toLocaleTimeString()}  ${s.text}  (v${s.version} ${s.ecc}, ${s.len} ký tự)`);
      hist.length=Math.min(hist.length,10);$('hist').textContent=hist.join('\n');
    }
    $('err').textContent=s.error?`Thấy mã nhưng không giải được: ${s.error}`:'';
    $('stats').textContent=`${s.w}x${s.h} · ${s.fps.toFixed(1)} fps · tìm mã ${(s.identify_us/1000).toFixed(0)} ms · giải mã ${(s.decode_us/1000).toFixed(1)} ms · đọc được ${s.decoded}, lỗi ${s.failed}`;
  }catch(e){}
  setTimeout(status,500);
}
function draw(codes,sw,sh,w,h){
  const c=$('ov');if(c.width!=w)c.width=w;if(c.height!=h)c.height=h;
  const g=c.getContext('2d');g.clearRect(0,0,w,h);
  if(!codes)return;
  const k=w/sw;g.lineWidth=Math.max(2,k*2);g.strokeStyle='#00e676';
  codes.split(';').forEach(q=>{
    const p=q.split(',').map(Number);if(p.length<8)return;
    g.beginPath();g.moveTo(p[0]*k,p[1]*k);for(let i=2;i<8;i+=2)g.lineTo(p[i]*k,p[i+1]*k);g.closePath();g.stroke();
  });
}
async function frame(){
  try{
    const r=await fetch('/frame.jpg?'+Date.now());
    if(r.ok){
      const codes=r.headers.get('X-Codes')||'',size=(r.headers.get('X-Size')||'1,1').split(',').map(Number);
      const b=await r.blob();if(url)URL.revokeObjectURL(url);url=URL.createObjectURL(b);
      const img=$('img');img.src=url;await img.decode().catch(()=>{});
      draw(codes,size[0],size[1],img.naturalWidth,img.naturalHeight);
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
  xSemaphoreTake(lock, portMAX_DELAY);
  last_request_ms = millis();
  uint32_t seq = frame_seq;
  xSemaphoreGive(lock);
  for (int i = 0; i < 100; i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
    xSemaphoreTake(lock, portMAX_DELAY);
    bool fresh = frame_seq != seq;
    xSemaphoreGive(lock);
    if (fresh) {
      break;
    }
  }

  // Nén ngay trong lock; task quét dùng try-lock nên chỉ bỏ qua một khung, không bị chặn
  uint8_t *jpg = nullptr;
  size_t jpg_len = 0;
  char codes[QR_MAX_CODES * 48 + 1] = "";
  char size[24];
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = frame_seq > 0 &&
            fmt2jpg(frame, frame_w * frame_h, frame_w, frame_h, PIXFORMAT_GRAYSCALE, 80, &jpg, &jpg_len);
  int pos = 0;
  for (int i = 0; i < frame_codes.count; i++) {
    const int *p = frame_codes.pts[i];
    pos += snprintf(codes + pos, sizeof(codes) - pos, "%s%d,%d,%d,%d,%d,%d,%d,%d", i ? ";" : "",
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  }
  snprintf(size, sizeof(size), "%d,%d", frame_w, frame_h);
  xSemaphoreGive(lock);

  if (!ok) {
    free(jpg);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Codes", codes);
  httpd_resp_set_hdr(req, "X-Size", size);
  esp_err_t res = httpd_resp_send(req, (const char *)jpg, jpg_len);
  free(jpg);
  return res;
}

// Chép chuỗi vào JSON: thoát " \ và ký tự điều khiển; byte >= 0x80 giữ nguyên (coi là UTF-8)
static int jsonEscape(char *out, int size, const char *in) {
  int n = 0;
  for (; *in && n < size - 7; in++) {
    unsigned char c = *in;
    if (c == '"' || c == '\\') {
      out[n++] = '\\';
      out[n++] = c;
    } else if (c < 0x20) {
      n += snprintf(out + n, size - n, "\\u%04x", c);
    } else {
      out[n++] = c;
    }
  }
  out[n] = '\0';
  return n;
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json[QR_TEXT_MAX * 6 + 512];  // status_handler chỉ chạy trong task web server
  char text[QR_TEXT_MAX * 6 + 1], error[96];
  QrStats s = scannerStats();
  xSemaphoreTake(lock, portMAX_DELAY);
  jsonEscape(text, sizeof(text), last_result.text);
  // Lỗi giải mã chỉ hiện trong 2 s gần nhất
  jsonEscape(error, sizeof(error), millis() - last_error_ms < 2000 ? last_error : "");
  snprintf(json, sizeof(json),
           "{\"w\":%d,\"h\":%d,\"fps\":%.1f,\"identify_us\":%u,\"decode_us\":%u,"
           "\"frames_with_code\":%u,\"decoded\":%u,\"failed\":%u,"
           "\"count\":%u,\"text\":\"%s\",\"len\":%d,\"version\":%d,\"ecc\":\"%c\",\"granted\":%d,"
           "\"error\":\"%s\",\"framesize\":%d,\"flash\":%d,\"contrast\":%d}",
           s.width, s.height, s.fps, s.identify_us, s.decode_us, s.frames_with_code, s.decoded, s.failed,
           result_count, text, last_result.len, last_result.version, last_result.ecc ? last_result.ecc : '-',
           last_granted, error, scannerFrameSize(), flashGet(), scannerContrast());
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
  int v = atoi(val);
  bool ok = true;
  // Không gọi sensor ở đây: task quét có thể đang khởi tạo lại camera
  if (!strcmp(var, "framesize")) {
    // Chỉ cho các cỡ đã có trên trang và không vượt buffer web đã cấp
    ok = (v == FRAMESIZE_QVGA || v == FRAMESIZE_CIF || v == FRAMESIZE_VGA || v == FRAMESIZE_SVGA) &&
         v <= QR_MAX_FRAME_SIZE;
    if (ok) {
      scannerSetFrameSize((framesize_t)v);
    }
  } else if (!strcmp(var, "flash")) {
    flashSet(v < 0 ? 0 : v > 255 ? 255 : v);
  } else if (!strcmp(var, "contrast")) {
    ok = v >= -2 && v <= 2;
    if (ok) {
      scannerSetContrast(v);
    }
  } else {
    ok = false;
  }
  if (!ok) {
    return httpd_resp_send_404(req);
  }
  Serial.printf("Set %s = %d\n", var, v);
  return httpd_resp_send(req, "OK", 2);
}

void webStart(int max_width, int max_height) {
  lock = xSemaphoreCreateMutex();
  frame = (uint8_t *)ps_malloc(max_width * max_height);
  if (!frame) {
    Serial.println("Web: out of memory for frame buffer");
    return;
  }

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
  };
  for (auto &u : uris) {
    httpd_register_uri_handler(server, &u);
  }
}

void webOnFrame(const camera_fb_t *fb, const QrCorners &codes) {
  if (!lock || !frame) {
    return;
  }
  // Không chờ: đang nén khung trước thì bỏ khung này, để không làm chậm việc quét
  if (xSemaphoreTake(lock, 0) != pdTRUE) {
    return;
  }
  if (last_request_ms && millis() - last_request_ms < 2000) {
    memcpy(frame, fb->buf, fb->width * fb->height);
    frame_w = fb->width;
    frame_h = fb->height;
    frame_codes = codes;
    frame_seq++;
  }
  xSemaphoreGive(lock);
}

void webOnResult(const QrResult &r, uint32_t count, int granted) {
  if (!lock) {
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  last_result = r;
  result_count = count;
  last_granted = granted;
  xSemaphoreGive(lock);
}

void webOnFailure(const char *error) {
  if (!lock) {
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  strncpy(last_error, error, sizeof(last_error) - 1);
  last_error_ms = millis();
  xSemaphoreGive(lock);
}
