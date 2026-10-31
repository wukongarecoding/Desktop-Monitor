#pragma once
#include <LovyanGFX.hpp>
#include <math.h>
#include <string.h>
#include "config.h"

#define SCROLL_W 224
#define SCROLL_H 20
#define SCROLL_GAP 60
#define TITLE_STEP 1
#define ARTIST_STEP 1

static LGFX_Sprite coverSprite(&lcd);
static LGFX_Sprite scrollSprite(&lcd);
static bool cover_inited = false;
static bool scroll_inited = false;

static char music_title[1024] = "--";
static char music_artist[1024] = "--";
static float music_cur = 0.0f;
static float music_dur = 0.0f;
static char music_state = 'X';
static bool music_dirty = true;
static uint32_t music_last_ms = 0;
static uint32_t base_local_ms = 0;

static int title_scroll_x = 0;
static int artist_scroll_x = 0;
static uint32_t scroll_last_ms = 0;

static uint8_t music_theme_r = 30;
static uint8_t music_theme_g = 20;
static uint8_t music_theme_b = 50;
static uint16_t grad_colors[SCREEN_H];

static int music_last_sec = -1;
static char music_last_state = 'X';

static void music_init_cover_sprite() {
  if (!cover_inited) {
    coverSprite.setPsram(true);
    coverSprite.setColorDepth(16);
    if (coverSprite.createSprite(200, 200)) {
      coverSprite.fillScreen(TFT_BLACK);
      cover_inited = true;
    }
  }
}

static void music_init_scroll_sprite() {
  if (!scroll_inited) {
    scrollSprite.setPsram(true);
    scrollSprite.setColorDepth(16);
    if (scrollSprite.createSprite(SCROLL_W, SCROLL_H)) {
      scrollSprite.fillScreen(TFT_BLACK);
      scroll_inited = true;
    }
  }
}

static void music_set_meta(const char* title, const char* artist) {
  bool changed = (strcmp(music_title, title) != 0) || (strcmp(music_artist, artist) != 0);
  if (!changed) return;

  strncpy(music_title, title, sizeof(music_title) - 1);
  music_title[sizeof(music_title) - 1] = 0;
  strncpy(music_artist, artist, sizeof(music_artist) - 1);
  music_artist[sizeof(music_artist) - 1] = 0;
  title_scroll_x = 0;
  artist_scroll_x = 0;
  music_dirty = true;
}

static void music_set_progress(float cur, float dur, char state) {
  music_cur = cur;
  music_dur = dur;
  music_state = state;
  base_local_ms = millis();
}

static void music_set_theme(uint8_t r, uint8_t g, uint8_t b) {
  music_theme_r = r;
  music_theme_g = g;
  music_theme_b = b;
  music_dirty = true;
}

static void music_set_cover(uint8_t* jpeg_data, size_t len) {
  music_init_cover_sprite();
  if (!cover_inited) return;
  coverSprite.fillScreen(TFT_BLACK);
  coverSprite.drawJpg(jpeg_data, len, 0, 0);
  music_dirty = true;
}

static float music_display_cur() {
  if (music_state == 'P') {
    float elapsed = (millis() - base_local_ms) / 1000.0f;
    float v = music_cur + elapsed;
    if (music_dur > 0.1f && v > music_dur) v = music_dur;
    return v;
  }
  return music_cur;
}

static void draw_gradient_background(LGFX_Sprite* c) {
  uint16_t* buf = (uint16_t*)c->getBuffer();
  if (!buf) return;
  for (int y = 0; y < SCREEN_H; y++) {
    float t = 1.0f - (float)y / (float)SCREEN_H;
    float f = t * t;
    int r = (int)(music_theme_r * f);
    int g = (int)(music_theme_g * f);
    int b = (int)(music_theme_b * f);
    uint16_t col = rgb565(r, g, b);
    grad_colors[y] = col;
    uint16_t col_sw = (col >> 8) | (col << 8);
    uint32_t* row32 = (uint32_t*)(buf + (size_t)y * SCREEN_W);
    uint32_t col32 = ((uint32_t)col_sw << 16) | col_sw;
    for (int i = 0; i < SCREEN_W / 2; i++) {
      row32[i] = col32;
    }
  }
}

static void draw_scrolling_text(LGFX_Sprite* c, const char* text,
                                int x, int y, int w, int h,
                                int* scroll_x, uint16_t color) {
  c->setFont(&lgfx::fonts::efontCN_16);
  c->setTextSize(1);
  c->setTextWrap(false);
  int tw = c->textWidth(text);

  if (tw <= w) {
    c->setTextColor(color);
    int tx = x + (w - tw) / 2;
    c->setCursor(tx, y + (h - 16) / 2);
    c->print(text);
    *scroll_x = 0;
    return;
  }

  music_init_scroll_sprite();
  if (!scroll_inited) {
    c->fillRect(x, y, w, h, TFT_BLACK);
    c->setTextWrap(false);
    c->setTextColor(color);
    c->setCursor(x - *scroll_x, y + (h - 16) / 2);
    c->print(text);
    return;
  }

  int period = tw + SCROLL_GAP;

  scrollSprite.fillScreen(TFT_BLACK);
  scrollSprite.setFont(&lgfx::fonts::efontCN_16);
  scrollSprite.setTextSize(1);
  scrollSprite.setTextColor(color);
  scrollSprite.setTextWrap(false);

  int sx0 = -(*scroll_x);
  scrollSprite.setCursor(sx0, 2);
  scrollSprite.print(text);
  if (sx0 + period < SCROLL_W) {
    scrollSprite.setCursor(sx0 + period, 2);
    scrollSprite.print(text);
  }

  uint16_t* src = (uint16_t*)scrollSprite.getBuffer();
  uint16_t* dst = (uint16_t*)c->getBuffer();
  for (int row = 0; row < h; row++) {
    memcpy(dst + (size_t)(y + row) * SCREEN_W + x,
           src + (size_t)row * SCROLL_W,
           SCROLL_W * 2);
  }
}

static void draw_play_icon(LGFX_Sprite* c, int cx, int cy, bool playing, uint16_t col) {
  if (playing) {
    c->fillRect(cx - 8, cy - 10, 5, 20, col);
    c->fillRect(cx + 3, cy - 10, 5, 20, col);
  } else {
    c->fillTriangle(cx - 7, cy - 10,
                    cx - 7, cy + 10,
                    cx + 10, cy, col);
  }
}

static void music_draw(LGFX_Sprite* c) {
  draw_gradient_background(c);

  if (cover_inited) {
    uint16_t* src = (uint16_t*)coverSprite.getBuffer();
    uint16_t* dst = (uint16_t*)c->getBuffer();
    for (int row = 0; row < 200; row++) {
      memcpy(dst + (size_t)(10 + row) * SCREEN_W + 20,
             src + (size_t)row * 200,
             200 * 2);
    }
    for (int dy = 0; dy < 30; dy++) {
      for (int dx = 0; dx < 30; dx++) {
        int ddx = 29 - dx;
        int ddy = 29 - dy;
        if (ddx * ddx + ddy * ddy > 841) {
          c->drawPixel(20 + dx, 10 + dy, grad_colors[10 + dy]);
          c->drawPixel(20 + 199 - dx, 10 + dy, grad_colors[10 + dy]);
          c->drawPixel(20 + dx, 10 + 199 - dy, grad_colors[10 + 199 - dy]);
          c->drawPixel(20 + 199 - dx, 10 + 199 - dy, grad_colors[10 + 199 - dy]);
        }
      }
    }
  } else {
    c->fillRoundRect(20, 10, 200, 200, 30, rgb565(30, 30, 40));
    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(1);
    c->setTextColor(TFT_DARKGREY);
    c->setCursor(85, 105);
    c->print("No Cover");
  }

  draw_scrolling_text(c, music_title, 8, 216, SCROLL_W, 18, &title_scroll_x, TFT_WHITE);
  draw_scrolling_text(c, music_artist, 8, 236, SCROLL_W, 18, &artist_scroll_x, rgb565(170, 170, 180));

  const int px = 38, py = 264, pw = 164, ph = 6;
  c->fillRoundRect(px, py, pw, ph, 3, rgb565(50, 50, 60));
  float cur = music_display_cur();
  if (music_dur > 0.1f) {
    float ratio = cur / music_dur;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    int filled = (int)(pw * ratio);
    if (filled > 0) {
      c->fillRoundRect(px, py, filled, ph, 3, rgb565(50, 200, 100));
    }
  }

  c->setFont(&lgfx::fonts::Font2);
  c->setTextSize(1);
  c->setTextColor(rgb565(180, 180, 190));
  char tbuf[16];
  int cur_sec = (int)cur;
  snprintf(tbuf, sizeof(tbuf), "%d:%02d", cur_sec / 60, cur_sec % 60);
  c->setCursor(2, 260);
  c->print(tbuf);

  int dur_sec = (int)music_dur;
  snprintf(tbuf, sizeof(tbuf), "%d:%02d", dur_sec / 60, dur_sec % 60);
  int tw = c->textWidth(tbuf);
  c->setCursor(238 - tw, 260);
  c->print(tbuf);

  draw_play_icon(c, 120, 290, music_state == 'P', TFT_WHITE);
}

static void music_setup(LGFX_Sprite* c) {
  music_init_cover_sprite();
  music_init_scroll_sprite();
  music_dirty = true;
  music_last_ms = 0;
  scroll_last_ms = 0;
  music_last_sec = -1;
  music_last_state = 'X';
  music_draw(c);
  push_with_fps(c);
  esp_send_raw("MUS_ON\n");
}

static void music_loop(LGFX_Sprite* c) {
  uint32_t now = millis();
  bool need_redraw = music_dirty;

  if (music_state == 'P') {
    int cur_sec = (int)music_display_cur();
    if (cur_sec != music_last_sec) {
      music_last_sec = cur_sec;
      need_redraw = true;
    }
  }
  if (music_state != music_last_state) {
    music_last_state = music_state;
    need_redraw = true;
  }

  if (now - scroll_last_ms >= 50) {
    scroll_last_ms = now;
    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(1);

    int tw = c->textWidth(music_title);
    if (tw > SCROLL_W) {
      int period = tw + SCROLL_GAP;
      title_scroll_x += TITLE_STEP;
      if (title_scroll_x >= period) title_scroll_x -= period;
      need_redraw = true;
    }

    tw = c->textWidth(music_artist);
    if (tw > SCROLL_W) {
      int period = tw + SCROLL_GAP;
      artist_scroll_x += ARTIST_STEP;
      if (artist_scroll_x >= period) artist_scroll_x -= period;
      need_redraw = true;
    }
  }

  if (need_redraw) {
    music_dirty = false;
    music_draw(c);
    push_with_fps(c);
  }
}

static void music_exit() {
  esp_send_raw("MUS_OFF\n");
}