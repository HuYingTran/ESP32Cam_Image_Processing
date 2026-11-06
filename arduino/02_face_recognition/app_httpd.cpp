// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "camera_index.h"
#include "Arduino.h"

#include "fb_gfx.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "fr_flash.h"
#include "lesson_config.h"

// Lấy từ lesson_config.h thay vì hằng số cố định của ví dụ gốc
#define ENROLL_CONFIRM_TIMES FACE_ENROLL_SAMPLES
#define FACE_ID_SAVE_NUMBER FACE_ID_MAX

#define FACE_COLOR_WHITE  0x00FFFFFF
#define FACE_COLOR_BLACK  0x00000000
#define FACE_COLOR_RED    0x000000FF
#define FACE_COLOR_GREEN  0x0000FF00
#define FACE_COLOR_BLUE   0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#define FACE_COLOR_CYAN   (FACE_COLOR_BLUE | FACE_COLOR_GREEN)
#define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)

typedef struct {
        size_t size; //number of values used for filtering
        size_t index; //current value index
        size_t count; //value count
        int sum;
        int * values; //array to be filled with values
} ra_filter_t;

typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static mtmn_config_t mtmn_config = {0};
static int8_t detection_enabled = 0;
static int8_t recognition_enabled = 0;
static int8_t is_enrolling = 0;
static face_id_list id_list = {0};
// Stream (cổng 81) và Get Still (cổng 80) chạy ở 2 task khác nhau; esp-face và id_list
// không an toàn khi dùng song song nên mỗi lúc chỉ cho một task xử lý khuôn mặt
static SemaphoreHandle_t face_mutex = NULL;

static ra_filter_t * ra_filter_init(ra_filter_t * filter, size_t sample_size){
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if(!filter->values){
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t * filter, int value){
    if(!filter->values){
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static void rgb_print(dl_matrix3du_t *image_matrix, uint32_t color, const char * str){
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    fb_gfx_print(&fb, (fb.width - (strlen(str) * 14)) / 2, 10, color, str);
}

static int rgb_printf(dl_matrix3du_t *image_matrix, uint32_t color, const char *format, ...){
    char loc_buf[64];
    char * temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, copy);
    va_end(copy);
    if(len >= sizeof(loc_buf)){
        temp = (char*)malloc(len+1);
        if(temp == NULL) {
            va_end(arg);
            return 0;
        }
    }
    vsnprintf(temp, len+1, format, arg);
    va_end(arg);
    rgb_print(image_matrix, color, temp);
    if(temp != loc_buf){
        free(temp);
    }
    return len;
}

// face_id: >= 0 nhận ra người đó (xanh lá), -1 người lạ (đỏ), FACE_ID_NONE chưa nhận dạng (vàng).
// Ví dụ gốc dùng 0 cho "chưa nhận dạng" nên người có ID 0 bị vẽ màu vàng
#define FACE_ID_NONE -2

static void draw_face_boxes(dl_matrix3du_t *image_matrix, box_array_t *boxes, int face_id){
    int x, y, w, h, i;
    uint32_t color = FACE_COLOR_YELLOW;
    if(face_id == -1){
        color = FACE_COLOR_RED;
    } else if(face_id >= 0){
        color = FACE_COLOR_GREEN;
    }
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    for (i = 0; i < boxes->len; i++){
        // rectangle box
        x = (int)boxes->box[i].box_p[0];
        y = (int)boxes->box[i].box_p[1];
        w = (int)boxes->box[i].box_p[2] - x + 1;
        h = (int)boxes->box[i].box_p[3] - y + 1;
        fb_gfx_drawFastHLine(&fb, x, y, w, color);
        fb_gfx_drawFastHLine(&fb, x, y+h-1, w, color);
        fb_gfx_drawFastVLine(&fb, x, y, h, color);
        fb_gfx_drawFastVLine(&fb, x+w-1, y, h, color);
#if 0
        // landmark
        int x0, y0, j;
        for (j = 0; j < 10; j+=2) {
            x0 = (int)boxes->landmark[i].landmark_p[j];
            y0 = (int)boxes->landmark[i].landmark_p[j+1];
            fb_gfx_fillRect(&fb, x0, y0, 3, 3, color);
        }
#endif
    }
}

// Cosine similarity giữa hai face ID (vector FACE_ID_SIZE float)
static float face_similarity(const dl_matrix3d_t *a, const dl_matrix3d_t *b){
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < FACE_ID_SIZE; i++) {
        dot += a->item[i] * b->item[i];
        na += a->item[i] * a->item[i];
        nb += b->item[i] * b->item[i];
    }
    if (na <= 0 || nb <= 0) {
        return 0;
    }
    return dot / (sqrtf(na) * sqrtf(nb));
}

// Thay cho recognize_face() của thư viện: tự so khớp để in được similarity
// và dùng ngưỡng FACE_RECOGNITION_THRESHOLD trong lesson_config.h
static int match_face(dl_matrix3du_t *aligned_face, float *best_similarity){
    *best_similarity = -1;
    if (id_list.count == 0) {
        return -1;
    }
    dl_matrix3d_t *face_id = get_face_id(aligned_face);
    if (!face_id) {
        return -1;
    }
    int best_id = -1;
    for (int i = 0; i < id_list.count; i++) {
        int slot = (id_list.head + i) % id_list.size;
        float s = face_similarity(face_id, id_list.id_list[slot]);
        if (s > *best_similarity) {
            *best_similarity = s;
            best_id = slot;
        }
    }
    dl_matrix3d_free(face_id);
    return (*best_similarity >= FACE_RECOGNITION_THRESHOLD) ? best_id : -1;
}

static int run_face_recognition(dl_matrix3du_t *image_matrix, box_array_t *net_boxes){
    dl_matrix3du_t *aligned_face = NULL;
    int matched_id = FACE_ID_NONE;

    aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
    if(!aligned_face){
        Serial.println("Could not allocate face recognition buffer");
        return matched_id;
    }
    if (align_face(net_boxes, image_matrix, aligned_face) == ESP_OK){
        if (is_enrolling == 1 && id_list.count >= id_list.size){
            Serial.printf("Face ID list full (%d), delete first\n", id_list.count);
            rgb_print(image_matrix, FACE_COLOR_RED, "ID list full");
            is_enrolling = 0;
        } else if (is_enrolling == 1){
            // Giữ slot trước khi enroll: thư viện tăng tail sau khi enroll xong
            int slot = id_list.tail;
#if FACE_SAVE_TO_FLASH
            int8_t left_sample_face = enroll_face_id_to_flash(&id_list, aligned_face);
#else
            int8_t left_sample_face = enroll_face(&id_list, aligned_face);
#endif
            if (left_sample_face < 0) {
                Serial.printf("Enroll failed (%d), check 'fr' partition\n", left_sample_face);
                is_enrolling = 0;
            } else {
                Serial.printf("Enrolling Face ID: %d sample %d/%d\n", slot, ENROLL_CONFIRM_TIMES - left_sample_face, ENROLL_CONFIRM_TIMES);
                rgb_printf(image_matrix, FACE_COLOR_CYAN, "ID[%u] Sample[%u]", slot, ENROLL_CONFIRM_TIMES - left_sample_face);
                if (left_sample_face == 0){
                    is_enrolling = 0;
                    Serial.printf("Enrolled Face ID: %d (total %d)\n", slot, id_list.count);
                }
            }
        } else {
            float similarity;
            matched_id = match_face(aligned_face, &similarity);
            if (matched_id >= 0) {
                Serial.printf("Match Face ID: %d, similarity %.2f\n", matched_id, similarity);
                rgb_printf(image_matrix, FACE_COLOR_GREEN, "ID %d (%.2f)", matched_id, similarity);
            } else {
                Serial.printf("No Match Found, best similarity %.2f < %.2f\n", similarity, FACE_RECOGNITION_THRESHOLD);
                rgb_print(image_matrix, FACE_COLOR_RED, "Intruder Alert!");
                matched_id = -1;
            }
        }
    } else {
        Serial.println("Face Not Aligned");
        //rgb_print(image_matrix, FACE_COLOR_YELLOW, "Human Detected");
    }

    dl_matrix3du_free(aligned_face);
    return matched_id;
}

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t out_len, out_width, out_height;
    uint8_t * out_buf;
    bool s;
    bool detected = false;
    int face_id = FACE_ID_NONE;
    if(!detection_enabled || fb->width > 400){
        size_t fb_len = 0;
        if(fb->format == PIXFORMAT_JPEG){
            fb_len = fb->len;
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        } else {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
            fb_len = jchunk.len;
        }
        esp_camera_fb_return(fb);
        int64_t fr_end = esp_timer_get_time();
        Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
        return res;
    }

    dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
    if (!image_matrix) {
        esp_camera_fb_return(fb);
        Serial.println("dl_matrix3du_alloc failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    out_buf = image_matrix->item;
    out_len = fb->width * fb->height * 3;
    out_width = fb->width;
    out_height = fb->height;

    s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
    esp_camera_fb_return(fb);
    if(!s){
        dl_matrix3du_free(image_matrix);
        Serial.println("to rgb888 failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    xSemaphoreTake(face_mutex, portMAX_DELAY);
    box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);

    // Kết quả phát hiện trong header X-Face để kiểm tra bằng curl mà không cần Serial
    static char face_hdr[64];
    snprintf(face_hdr, sizeof(face_hdr), "none");
    if (net_boxes){
        detected = true;
        float face_w, face_h;
        image_get_width_and_height(&net_boxes->box[0], &face_w, &face_h);
        Serial.printf("Face: %.0fx%.0f px in %ux%u frame, score %.2f\n",
                      face_w, face_h, (unsigned)out_width, (unsigned)out_height, net_boxes->score[0]);
        snprintf(face_hdr, sizeof(face_hdr), "%d face %.0fx%.0f score %.2f",
                 net_boxes->len, face_w, face_h, net_boxes->score[0]);
        if(recognition_enabled){
            face_id = run_face_recognition(image_matrix, net_boxes);
        }
        draw_face_boxes(image_matrix, net_boxes, face_id);
        // net_boxes do dl_lib_calloc cấp phát: phải giải phóng bằng dl_lib_free, free() làm hỏng heap (arduino-esp32 #5128)
        dl_lib_free(net_boxes->score);
        dl_lib_free(net_boxes->box);
        dl_lib_free(net_boxes->landmark);
        dl_lib_free(net_boxes);
    }
    xSemaphoreGive(face_mutex);
    httpd_resp_set_hdr(req, "X-Face", face_hdr);
    httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Face");

    jpg_chunking_t jchunk = {req, 0};
    s = fmt2jpg_cb(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);
    dl_matrix3du_free(image_matrix);
    if(!s){
        Serial.println("JPEG compression failed");
        return ESP_FAIL;
    }

    int64_t fr_end = esp_timer_get_time();
    // ID nhận dạng in ở dòng "Match Face ID" / "No Match Found"
    Serial.printf("FACE: %uB %ums %s\n", (uint32_t)(jchunk.len), (uint32_t)((fr_end - fr_start)/1000), detected?"DETECTED":"");
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];
    dl_matrix3du_t *image_matrix = NULL;
    bool detected = false;
    int face_id = FACE_ID_NONE;
    int64_t fr_start = 0;
    int64_t fr_ready = 0;
    int64_t fr_face = 0;
    int64_t fr_recognize = 0;
    int64_t fr_encode = 0;

    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while(true){
        detected = false;
        face_id = FACE_ID_NONE;
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        } else {
            fr_start = esp_timer_get_time();
            fr_ready = fr_start;
            fr_face = fr_start;
            fr_encode = fr_start;
            fr_recognize = fr_start;
            // Gợi ý khi đang enroll mà chưa có gì xảy ra (tối đa 1 lần/giây)
            static int64_t last_enroll_hint = 0;
            bool enroll_hint = is_enrolling && (fr_start - last_enroll_hint > 1000000);
            if (enroll_hint) {
                last_enroll_hint = fr_start;
                if (!detection_enabled || !recognition_enabled) {
                    Serial.println("Enroll waiting: turn on Face Detection and Face Recognition");
                } else if (fb->width > 400) {
                    Serial.printf("Enroll waiting: frame width %d > 400, choose CIF or lower\n", fb->width);
                }
            }
            if(!detection_enabled || fb->width > 400){
                if(fb->format != PIXFORMAT_JPEG){
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if(!jpeg_converted){
                        Serial.println("JPEG compression failed");
                        res = ESP_FAIL;
                    }
                } else {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
            } else {

                image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);

                if (!image_matrix) {
                    Serial.println("dl_matrix3du_alloc failed");
                    res = ESP_FAIL;
                } else {
                    if(!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)){
                        Serial.println("fmt2rgb888 failed");
                        res = ESP_FAIL;
                    } else {
                        fr_ready = esp_timer_get_time();
                        box_array_t *net_boxes = NULL;
                        xSemaphoreTake(face_mutex, portMAX_DELAY);
                        if(detection_enabled){
                            net_boxes = face_detect(image_matrix, &mtmn_config);
                        }
                        fr_face = esp_timer_get_time();
                        fr_recognize = fr_face;
                        if (enroll_hint && recognition_enabled && !net_boxes) {
                            Serial.printf("Enroll waiting: no face detected (face must be >= %d px, enough light)\n", FACE_MIN_SIZE);
                        }
                        if(net_boxes){
                            detected = true;
                            if(recognition_enabled){
                                face_id = run_face_recognition(image_matrix, net_boxes);
                            }
                            fr_recognize = esp_timer_get_time();
                            draw_face_boxes(image_matrix, net_boxes, face_id);
                            // net_boxes do dl_lib_calloc cấp phát: phải giải phóng bằng dl_lib_free, free() làm hỏng heap (arduino-esp32 #5128)
                            dl_lib_free(net_boxes->score);
                            dl_lib_free(net_boxes->box);
                            dl_lib_free(net_boxes->landmark);
                            dl_lib_free(net_boxes);
                        }
                        xSemaphoreGive(face_mutex);
                        if (detected || fb->format != PIXFORMAT_JPEG){
                            if(!fmt2jpg(image_matrix->item, fb->width*fb->height*3, fb->width, fb->height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len)){
                                Serial.println("fmt2jpg failed");
                                res = ESP_FAIL;
                            }
                            esp_camera_fb_return(fb);
                            fb = NULL;
                        } else {
                            _jpg_buf = fb->buf;
                            _jpg_buf_len = fb->len;
                        }
                        fr_encode = esp_timer_get_time();
                    }
                    dl_matrix3du_free(image_matrix);
                }
            }
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(fb){
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if(_jpg_buf){
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t ready_time = (fr_ready - fr_start)/1000;
        int64_t face_time = (fr_face - fr_ready)/1000;
        int64_t recognize_time = (fr_recognize - fr_face)/1000;
        int64_t encode_time = (fr_encode - fr_recognize)/1000;
        int64_t process_time = (fr_encode - fr_start)/1000;
        
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
        Serial.printf("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps), %u+%u+%u+%u=%u %s\n",
            (uint32_t)(_jpg_buf_len),
            (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
            avg_frame_time, 1000.0 / avg_frame_time,
            (uint32_t)ready_time, (uint32_t)face_time, (uint32_t)recognize_time, (uint32_t)encode_time, (uint32_t)process_time,
            (detected)?"DETECTED":""
        );
    }

    last_frame = 0;
    return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
    char*  buf;
    size_t buf_len;
    char variable[32] = {0,};
    char value[32] = {0,};

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if(!buf){
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
            } else {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        } else {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t * s = esp_camera_sensor_get();
    int res = 0;
    Serial.printf("Control: %s = %d\n", variable, val);

    if(!strcmp(variable, "framesize")) {
        if(s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
    }
    else if(!strcmp(variable, "quality")) res = s->set_quality(s, val);
    else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
    else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
    else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
    else if(!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
    else if(!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
    else if(!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
    else if(!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
    else if(!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
    else if(!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
    else if(!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
    else if(!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
    else if(!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
    else if(!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
    else if(!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
    else if(!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
    else if(!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
    else if(!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
    else if(!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
    else if(!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
    else if(!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
    else if(!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
    else if(!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
    else if(!strcmp(variable, "face_detect")) {
        detection_enabled = val;
        if(!detection_enabled) {
            recognition_enabled = 0;
        }
    }
    else if(!strcmp(variable, "face_enroll")) is_enrolling = val;
    // Chỉnh MTMN lúc đang chạy để thử tham số mà không phải nạp lại (không lưu sau khi khởi động lại).
    // Điểm số và pyramid truyền dạng số nguyên: score x100, pyramid x1000
    else if(!strcmp(variable, "mtmn_type")) mtmn_config.type = val ? NORMAL : FAST;
    else if(!strcmp(variable, "min_face") && val >= 12) mtmn_config.min_face = val;
    else if(!strcmp(variable, "pyramid") && val > 0 && val < 1000) mtmn_config.pyramid = val / 1000.0f;
    else if(!strcmp(variable, "pyramid_times") && val > 0) mtmn_config.pyramid_times = val;
    else if(!strcmp(variable, "p_score")) mtmn_config.p_threshold.score = val / 100.0f;
    else if(!strcmp(variable, "r_score")) mtmn_config.r_threshold.score = val / 100.0f;
    else if(!strcmp(variable, "o_score")) mtmn_config.o_threshold.score = val / 100.0f;
    else if(!strcmp(variable, "face_delete_all")) {
        // Không có nút trên giao diện web: gọi /control?var=face_delete_all&val=1
        xSemaphoreTake(face_mutex, portMAX_DELAY);
        for (int i = 0; i < FACE_ID_MAX && id_list.count > 0; i++) {
#if FACE_SAVE_TO_FLASH
            delete_face_id_in_flash(&id_list);
#else
            delete_face(&id_list);
#endif
        }
        xSemaphoreGive(face_mutex);
        Serial.printf("Deleted all face IDs, remaining %d\n", id_list.count);
    }
    else if(!strcmp(variable, "face_recognize")) {
        recognition_enabled = val;
        if(recognition_enabled){
            detection_enabled = val;
        }
    }
    else {
        res = -1;
    }

    if(res){
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req){
    static char json_response[1024];

    sensor_t * s = esp_camera_sensor_get();
    char * p = json_response;
    *p++ = '{';

    p+=sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p+=sprintf(p, "\"quality\":%u,", s->status.quality);
    p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p+=sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p+=sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p+=sprintf(p, "\"awb\":%u,", s->status.awb);
    p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p+=sprintf(p, "\"aec\":%u,", s->status.aec);
    p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p+=sprintf(p, "\"agc\":%u,", s->status.agc);
    p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p+=sprintf(p, "\"vflip\":%u,", s->status.vflip);
    p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p+=sprintf(p, "\"colorbar\":%u,", s->status.colorbar);
    p+=sprintf(p, "\"face_detect\":%u,", detection_enabled);
    p+=sprintf(p, "\"face_enroll\":%u,", is_enrolling);
    p+=sprintf(p, "\"face_recognize\":%u,", recognition_enabled);
    p+=sprintf(p, "\"face_ids\":%u,", id_list.count);
    p+=sprintf(p, "\"mtmn_type\":%u,\"min_face\":%.0f,\"pyramid\":%.0f,\"pyramid_times\":%d,",
               mtmn_config.type == NORMAL ? 1 : 0, mtmn_config.min_face,
               mtmn_config.pyramid * 1000, mtmn_config.pyramid_times);
    p+=sprintf(p, "\"p_score\":%.0f,\"r_score\":%.0f,\"o_score\":%.0f",
               mtmn_config.p_threshold.score * 100, mtmn_config.r_threshold.score * 100,
               mtmn_config.o_threshold.score * 100);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t * s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        return httpd_resp_send(req, (const char *)index_ov3660_html_gz, index_ov3660_html_gz_len);
    }
    return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };


    ra_filter_init(&ra_filter, 20);
    
    mtmn_config.type = FACE_DETECT_TYPE;
    mtmn_config.min_face = FACE_MIN_SIZE;
    mtmn_config.pyramid = FACE_PYRAMID;
    mtmn_config.pyramid_times = FACE_PYRAMID_TIMES;
    mtmn_config.p_threshold.score = FACE_P_SCORE;
    mtmn_config.p_threshold.nms = 0.7;
    mtmn_config.p_threshold.candidate_number = 20;
    mtmn_config.r_threshold.score = FACE_R_SCORE;
    mtmn_config.r_threshold.nms = 0.7;
    mtmn_config.r_threshold.candidate_number = 10;
    mtmn_config.o_threshold.score = FACE_O_SCORE;
    mtmn_config.o_threshold.nms = 0.7;
    mtmn_config.o_threshold.candidate_number = 1;
    
    Serial.printf("MTMN: %s, min_face %.0f, pyramid %.3f x%d, score P/R/O %.2f/%.2f/%.2f\n",
                  mtmn_config.type == FAST ? "FAST" : "NORMAL", mtmn_config.min_face,
                  mtmn_config.pyramid, mtmn_config.pyramid_times,
                  mtmn_config.p_threshold.score, mtmn_config.r_threshold.score, mtmn_config.o_threshold.score);
    face_mutex = xSemaphoreCreateMutex();
    face_id_init(&id_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
#if FACE_SAVE_TO_FLASH
    read_face_id_from_flash(&id_list);
    Serial.printf("Loaded %d face IDs from flash\n", id_list.count);
#endif
    
    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
