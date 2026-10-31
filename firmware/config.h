#pragma once
#include <stdint.h>

#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>


#define PIN_ENC_A 41
#define PIN_ENC_B 42
#define PIN_ENC_SW 21
#define PIN_BTN_BACK 40


#define SCREEN_W 240
#define SCREEN_H 320


#define MENU_COUNT 5
#define MENU_ITEM_H 90
#define MENU_START_Y ((SCREEN_H - MENU_COUNT * MENU_ITEM_H) / 2)
#define TEXT_SIZE 2

#define ICON_CX 60
#define ICON_SIZE 60
#define TEXT_LEFT 105
#define TEXT_RIGHT 235

#define PIN_KEY1 38
#define PIN_KEY2 39
#define PIN_KEY3 47

#define SHOW_FPS 0
#define MENU_INTERP 0.5f
#define MENU_MAX_LEAD 2.0f
#define FADE_FRAMES 8
#define FADE_EXTRA_DELAY 3

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

extern LGFX lcd;

static inline void push_with_fps(LGFX_Sprite* c) {
  c->pushSprite(0, 0);
#if SHOW_FPS
  static uint32_t last_ms = 0;
  static uint32_t frames = 0;
  frames++;
  uint32_t now = millis();
  if (last_ms == 0) last_ms = now;
  if (now - last_ms >= 500) {
    float value = frames * 1000.0f / (float)(now - last_ms);
    frames = 0;
    last_ms = now;
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", value);
    lcd.setFont(&lgfx::fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_YELLOW, rgb565(30, 30, 40));
    lcd.setCursor(4, 2);
    lcd.print(buf);
  }
#endif
}

extern void esp_send_raw(const char* s);
extern void esp_printf(const char* fmt, ...);