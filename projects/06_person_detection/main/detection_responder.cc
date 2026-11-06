#include "detection_responder.h"

#include <stdio.h>

#include "driver/gpio.h"

#include "lesson_config.h"

static int last_state = -1;  // -1: chưa có kết quả, 0: no person, 1: PERSON

void responderInit() {
#if PD_LED_GPIO >= 0
  gpio_reset_pin((gpio_num_t)PD_LED_GPIO);
  gpio_set_direction((gpio_num_t)PD_LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)PD_LED_GPIO, PD_LED_ACTIVE_LOW);  // tắt
#endif
}

static void ledSet(bool on) {
#if PD_LED_GPIO >= 0
  gpio_set_level((gpio_num_t)PD_LED_GPIO, on != PD_LED_ACTIVE_LOW);
#endif
}

bool respondToDetection(float person, float no_person, bool print) {
  // Hai điểm ra từ softmax nên cộng lại ~1: person >= ngưỡng (>= 0.5) cũng là person > no person
  bool detected = person >= PD_THRESHOLD;
  int state = detected ? 1 : 0;
  if (print || state != last_state) {
    printf("%-9s person %3d%%  no person %3d%%\n", detected ? "PERSON" : "no person",
           (int)(person * 100 + 0.5f), (int)(no_person * 100 + 0.5f));
  }
  if (state != last_state) {
    ledSet(detected);
    last_state = state;
  }
  return detected;
}
