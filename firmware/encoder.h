#pragma once
#include <Arduino.h>
#include "config.h"

enum EncoderEvent {
  ENC_NONE = 0,
  ENC_LEFT,
  ENC_RIGHT,
  ENC_PRESS,
  ENC_BACK
};

static volatile int enc_delta = 0;
static volatile int enc_lastA = 0;

static void IRAM_ATTR enc_isr() {
  int a = digitalRead(PIN_ENC_A);
  int b = digitalRead(PIN_ENC_B);
  if (a != enc_lastA) {
    enc_delta += (a == b) ? 1 : -1;
    enc_lastA = a;
  }
}

struct BtnDebounce {
  uint8_t pin;
  bool stable;
  bool last_raw;
  uint32_t last_change;
  bool pressed_evt;
};

static BtnDebounce _btn_sw = { PIN_ENC_SW, HIGH, HIGH, 0, false };
static BtnDebounce _btn_bk = { PIN_BTN_BACK, HIGH, HIGH, 0, false };

static void encoder_init() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  enc_lastA = digitalRead(PIN_ENC_A);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), enc_isr, CHANGE);

  uint32_t t = millis();
  _btn_sw.last_raw = _btn_sw.stable = digitalRead(PIN_ENC_SW);
  _btn_bk.last_raw = _btn_bk.stable = digitalRead(PIN_BTN_BACK);
  _btn_sw.last_change = _btn_bk.last_change = t;
}

static void _btn_poll(BtnDebounce& b) {
  bool raw = digitalRead(b.pin);
  uint32_t now = millis();
  if (raw != b.last_raw) {
    b.last_raw = raw;
    b.last_change = now;
  }
  if ((now - b.last_change) >= 20 && b.stable != raw) {
    b.stable = raw;
    if (raw == LOW) b.pressed_evt = true;
  }
}

static EncoderEvent encoder_read() {
  _btn_poll(_btn_sw);
  _btn_poll(_btn_bk);

  EncoderEvent e = ENC_NONE;
  noInterrupts();
  int d = enc_delta;
  bool sw = _btn_sw.pressed_evt;
  _btn_sw.pressed_evt = false;
  bool bk = _btn_bk.pressed_evt;
  _btn_bk.pressed_evt = false;

  if (bk) e = ENC_BACK;
  else if (sw) e = ENC_PRESS;
  else if (d >= 2) {
    enc_delta -= 2;
    e = ENC_RIGHT;
  } else if (d <= -2) {
    enc_delta += 2;
    e = ENC_LEFT;
  }

  interrupts();
  return e;
}