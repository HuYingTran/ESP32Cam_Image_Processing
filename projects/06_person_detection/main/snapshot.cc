#include "snapshot.h"

#include <stdio.h>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "model_settings.h"

static const char *TAG = "snapshot";

static uint8_t *image = nullptr;  // uint8 0..255, cấp trong PSRAM để không lấy RAM trong của tensor arena
static bool ready = false;

void snapshotInit() {
  // Driver chỉ để đọc lệnh (RX); printf vẫn ghi thẳng ra UART0 như cũ
  if (uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0) != ESP_OK) {
    ESP_LOGW(TAG, "UART0 driver install failed, snapshot disabled");
    return;
  }
  image = (uint8_t *)heap_caps_malloc(kMaxImageSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!image) {
    image = (uint8_t *)heap_caps_malloc(kMaxImageSize, MALLOC_CAP_8BIT);
  }
  if (image) {
    printf("Snapshot: send 'c' over serial to dump the 96x96 model input (tools/person_snapshot.py)\n");
  }
}

bool snapshotRequested() {
  if (!image) {
    return false;
  }
  bool requested = false;
  uint8_t c;
  while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
    requested |= c == 'c';
  }
  return requested;
}

void snapshotKeep(const int8_t *img) {
  // int8 -> uint8: ngược với bước tiền xử lý (đảo bit cao = cộng 128)
  for (int i = 0; i < kMaxImageSize; i++) {
    image[i] = (uint8_t)img[i] ^ 0x80;
  }
  ready = true;
}

void snapshotSend(float person, bool detected) {
  if (!ready) {
    return;
  }
  static const char hex[] = "0123456789abcdef";
  char line[kNumCols * 2 + 2];
  printf("SNAP %d %d person %d detected %d\n", kNumCols, kNumRows, (int)(person * 100 + 0.5f), detected);
  for (int y = 0; y < kNumRows; y++) {
    const uint8_t *row = image + y * kNumCols;
    for (int x = 0; x < kNumCols; x++) {
      line[2 * x] = hex[row[x] >> 4];
      line[2 * x + 1] = hex[row[x] & 0xf];
    }
    line[kNumCols * 2] = '\n';
    line[kNumCols * 2 + 1] = '\0';
    fputs(line, stdout);
  }
  printf("SNAP END\n");
  ready = false;
}
