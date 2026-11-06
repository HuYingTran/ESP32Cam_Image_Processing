// Bài #6 - Phát hiện có người (ESP-IDF + esp-tflite-micro)
// Model person_detect int8 (mảng C trong flash) -> MicroInterpreter với tensor arena.
// Mỗi vòng: chụp ảnh xám -> 96x96 int8 -> Invoke -> dequantize -> so ngưỡng -> Serial/LED.
// Cấu hình: main/lesson_config.h (ảnh, ngưỡng, arena) và sdkconfig.defaults (PSRAM, ESP-NN).

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "detection_responder.h"
#include "image_provider.h"
#include "lesson_config.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#if PD_SNAPSHOT
#include "snapshot.h"
#endif

static const char *TAG = "person";

#if CONFIG_NN_OPTIMIZED
#define NN_NAME "ESP-NN"
#else
#define NN_NAME "ANSI C (no ESP-NN)"
#endif

static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input = nullptr;
static TfLiteTensor *output = nullptr;

static bool modelInit() {
  const uint8_t *model_data = g_person_detect_model_data;
#if PD_MODEL_IN_PSRAM
  // Mỗi lần Invoke đọc lại toàn bộ 300 KB trọng số: flash DIO 40 MHz chậm hơn PSRAM QIO 40 MHz
  uint8_t *copy = (uint8_t *)heap_caps_aligned_alloc(16, g_person_detect_model_data_len, MALLOC_CAP_SPIRAM);
  if (copy) {
    memcpy(copy, g_person_detect_model_data, g_person_detect_model_data_len);
    model_data = copy;
  }
#endif
  const tflite::Model *model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Model schema version %lu != supported %d", model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }

  // RAM trong nhanh hơn PSRAM đáng kể khi Conv đọc/ghi tensor trung gian liên tục
#if PD_ARENA_PREFER_INTERNAL
  uint8_t *arena = (uint8_t *)heap_caps_malloc_prefer(PD_ARENA_SIZE, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  uint8_t *arena = (uint8_t *)heap_caps_malloc(PD_ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if (!arena) {
    ESP_LOGE(TAG, "Couldn't allocate tensor arena of %d bytes", PD_ARENA_SIZE);
    return false;
  }
  bool in_psram = esp_ptr_external_ram(arena);

  // Chỉ đăng ký 5 op mà model dùng (AllOpsResolver kéo theo mọi kernel, firmware to hơn nhiều)
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddAveragePool2D();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddReshape();
  resolver.AddSoftmax();

  static tflite::MicroInterpreter static_interpreter(model, resolver, arena, PD_ARENA_SIZE);
  interpreter = &static_interpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(TAG, "AllocateTensors() failed: increase PD_ARENA_SIZE");
    return false;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);
  if (input->type != kTfLiteInt8 || input->bytes != kMaxImageSize) {
    ESP_LOGE(TAG, "Unexpected input tensor: type %d, %u bytes", input->type, (unsigned)input->bytes);
    return false;
  }

  printf("Model: %d bytes in %s, input %dx%dx%d int8, output %d classes\n", g_person_detect_model_data_len,
         esp_ptr_external_ram(model_data) ? "PSRAM" : "flash", input->dims->data[1], input->dims->data[2], input->dims->data[3], output->dims->data[1]);
  printf("Tensor arena: %d bytes in %s, used %u bytes (largest free internal block %u)\n", PD_ARENA_SIZE,
         in_psram ? "PSRAM" : "internal RAM", (unsigned)interpreter->arena_used_bytes(),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  printf("Kernels: %s\n", NN_NAME);
  return true;
}

// Chạy model trên input đã ghi sẵn, trả về điểm 0..1 của hai lớp
static bool runInference(float *person, float *no_person, uint32_t *invoke_us) {
  int64_t t0 = esp_timer_get_time();
  if (interpreter->Invoke() != kTfLiteOk) {
    ESP_LOGE(TAG, "Invoke failed");
    return false;
  }
  *invoke_us = esp_timer_get_time() - t0;

  // Dequantize: giá trị thực = (q - zero_point) * scale
  float scale = output->params.scale;
  int zero_point = output->params.zero_point;
  *person = (output->data.int8[kPersonIndex] - zero_point) * scale;
  *no_person = (output->data.int8[kNotAPersonIndex] - zero_point) * scale;
  return true;
}

#if PD_SELF_TEST
// 10 ảnh xám 96x96 uint8 của ví dụ gốc (main/sample_images/README.md), nhúng bằng EMBED_FILES
extern const uint8_t image0_start[] asm("_binary_image0_start");
extern const uint8_t image1_start[] asm("_binary_image1_start");
extern const uint8_t image2_start[] asm("_binary_image2_start");
extern const uint8_t image3_start[] asm("_binary_image3_start");
extern const uint8_t image4_start[] asm("_binary_image4_start");
extern const uint8_t image5_start[] asm("_binary_image5_start");
extern const uint8_t image6_start[] asm("_binary_image6_start");
extern const uint8_t image7_start[] asm("_binary_image7_start");
extern const uint8_t image8_start[] asm("_binary_image8_start");
extern const uint8_t image9_start[] asm("_binary_image9_start");

static void selfTest() {
  struct Sample {
    const uint8_t *data;
    const char *name;
    bool person;
  };
  static const Sample samples[] = {
      {image0_start, "person", true}, {image1_start, "dog", false},
      {image2_start, "person", true}, {image3_start, "monkey", false},
      {image4_start, "person", true}, {image5_start, "cat", false},
      {image6_start, "person (blurry)", true}, {image7_start, "portrait", true},
      {image8_start, "person", true}, {image9_start, "person", true},
  };
  const int n = sizeof(samples) / sizeof(samples[0]);

  printf("\n--- Self test: %d sample images, %s ---\n", n, NN_NAME);
  int correct = 0;
  uint32_t total_us = 0, min_us = UINT32_MAX, max_us = 0;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < kMaxImageSize; j++) {
      input->data.int8[j] = (int8_t)(samples[i].data[j] ^ 0x80);
    }
    float person, no_person;
    uint32_t us;
    if (!runInference(&person, &no_person, &us)) {
      return;
    }
    bool detected = person >= PD_THRESHOLD;
    correct += detected == samples[i].person;
    total_us += us;
    min_us = us < min_us ? us : min_us;
    max_us = us > max_us ? us : max_us;
    printf("image%d %-16s person %3d%%  -> %-9s %s  %lu ms\n", i, samples[i].name, (int)(person * 100 + 0.5f),
           detected ? "PERSON" : "no person", detected == samples[i].person ? "OK  " : "MISS",
           (unsigned long)(us / 1000));
    vTaskDelay(1);  // nhường CPU cho IDLE task giữa các ảnh, tránh task watchdog
  }
  printf("Self test: %d/%d correct, invoke avg %lu ms (min %lu, max %lu)\n\n", correct, n,
         (unsigned long)(total_us / n / 1000), (unsigned long)(min_us / 1000), (unsigned long)(max_us / 1000));
}
#endif

static void personTask(void *) {
  if (!modelInit()) {
    vTaskDelete(nullptr);
  }
#if PD_SELF_TEST
  selfTest();
#endif
  responderInit();
  if (!imageProviderInit()) {
    vTaskDelete(nullptr);
  }
#if PD_SNAPSHOT
  snapshotInit();
#endif
  printf("Person detection ready: threshold %.2f, free internal %u, PSRAM %u\n", PD_THRESHOLD,
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  uint32_t frames = 0, persons = 0;
  uint64_t sum_capture = 0, sum_pre = 0, sum_invoke = 0;
  int64_t window_start = esp_timer_get_time();
  while (true) {
    ImageTiming t;
    float person, no_person;
    uint32_t invoke_us;
#if PD_SNAPSHOT
    bool snap = snapshotRequested();
#endif
    if (!imageProviderGet(input->data.int8, &t)) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
#if PD_SNAPSHOT
    if (snap) {
      snapshotKeep(input->data.int8);
    }
#endif
    if (!runInference(&person, &no_person, &invoke_us)) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool detected = respondToDetection(person, no_person, PD_PRINT_EVERY_FRAME);
    persons += detected;
#if PD_SNAPSHOT
    if (snap) {
      // In 96 dòng hex mất ~1.6 s ở 115200 baud: không tính vào FPS
      int64_t t_send = esp_timer_get_time();
      snapshotSend(person, detected);
      window_start += esp_timer_get_time() - t_send;
    }
#endif

    frames++;
    sum_capture += t.capture_us;
    sum_pre += t.preprocess_us;
    sum_invoke += invoke_us;
    if (frames == PD_STATS_EVERY) {
      float secs = (esp_timer_get_time() - window_start) / 1e6f;
      printf("[stats] %lu frames: capture %.1f ms, preprocess %.2f ms, invoke %.1f ms (%s), %.2f FPS, person %lu/%lu\n",
             (unsigned long)frames, sum_capture / 1000.0f / frames, sum_pre / 1000.0f / frames,
             sum_invoke / 1000.0f / frames, NN_NAME, frames / secs, (unsigned long)persons, (unsigned long)frames);
      frames = persons = 0;
      sum_capture = sum_pre = sum_invoke = 0;
      window_start = esp_timer_get_time();
    }
    vTaskDelay(1);  // nhường CPU cho IDLE task, tránh task watchdog
  }
}

extern "C" void app_main() {
  // Invoke cần stack vài KB; task chính mặc định 3.5 KB nên chạy trong task riêng
  xTaskCreate(personTask, "person", 8 * 1024, nullptr, 5, nullptr);
}
