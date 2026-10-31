#pragma once
#include <Arduino.h>
#include "config.h"



static const uint32_t DEBOUNCE_MS = 30;
static const uint32_t DOUBLE_GAP_MS = 200;

struct ButtonState {
  uint8_t pin;
  bool is_down;
  uint32_t last_down_ms;
  uint32_t last_up_ms;
  bool pending_single;
  bool awaiting_second;
  uint32_t last_change_ms;
};

static ButtonState btns[3] = {
  { PIN_KEY1, false, 0, 0, false, false, 0 },
  { PIN_KEY2, false, 0, 0, false, false, 0 },
  { PIN_KEY3, false, 0, 0, false, false, 0 },
};

static void buttons_init() {
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  pinMode(PIN_KEY3, INPUT_PULLUP);
  for (int i = 0; i < 3; i++) {
    btns[i].is_down = (digitalRead(btns[i].pin) == LOW);
    btns[i].last_change_ms = millis();
  }
}

static void buttons_poll() {
  uint32_t now = millis();

  for (int i = 0; i < 3; i++) {
    ButtonState& b = btns[i];
    bool raw = (digitalRead(b.pin) == LOW);

    if (raw != b.is_down) {
      if (now - b.last_change_ms < DEBOUNCE_MS) {
        continue;
      }
      b.last_change_ms = now;
      b.is_down = raw;

      if (raw) {
        b.last_down_ms = now;
        if (b.pending_single) {
          b.pending_single = false;
          b.awaiting_second = true;
        }
      } else {
        b.last_up_ms = now;
        if (b.awaiting_second) {
          Serial.printf("KEY%d_D\n", i + 1);
          b.awaiting_second = false;
        } else {
          b.pending_single = true;
        }
      }
    }

    if (b.pending_single && !b.is_down && (now - b.last_up_ms > DOUBLE_GAP_MS)) {
      Serial.printf("KEY%d\n", i + 1);
      b.pending_single = false;
    }


    if (b.awaiting_second && b.is_down && (now - b.last_down_ms > 1000)) {
      b.awaiting_second = false;

      Serial.printf("KEY%d\n", i + 1);
    }
  }
}