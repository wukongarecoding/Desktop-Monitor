#pragma once

#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>
#include <math.h>
#include "config.h"

extern LGFX lcd;
extern LGFX_Sprite bgSprite;
extern LGFX_Sprite menuBgSprite;
extern LGFX_Sprite canvas;
extern LGFX_Sprite menuSprite;
extern LGFX_Sprite appSprite;

extern "C" {
  extern const uint8_t bg_image_map[];
  extern const size_t bg_image_map_size;
  extern const uint8_t* bg_image_pixels(void);
  extern const size_t bg_image_pixel_bytes(void);
}


static const char* MENU_ITEMS[MENU_COUNT] = {
  "音乐", "监视器", "日历", "天气", "设置"
};

static inline uint16_t swap16(uint16_t v) {
  return (uint16_t)((v >> 8) | (v << 8));
}

static uint16_t getIconFrontColor(int idx) {
  switch (idx) {
    case 0: return rgb565(64, 190, 120);
    case 1: return rgb565(50, 160, 220);
    case 2: return rgb565(220, 80, 120);
    case 3: return rgb565(100, 180, 240);
    case 4: return rgb565(150, 150, 200);
  }
  return TFT_WHITE;
}

static void drawIcon(LGFX_Sprite* spr, int cx, int cy, int size, int idx) {
  uint16_t front = getIconFrontColor(idx);
  int r = size / 2;
  int x = cx - r, y = cy - r;
  spr->fillRoundRect(x, y, size, size, 12, front);
  uint16_t white = TFT_WHITE;
  float s = (float)size / 64.0f;
  int u = (int)(12 * s);
  if (u < 1) u = 1;

  if (idx == 0) {

    spr->fillCircle(cx - u / 2, cy + (u * 7) / 10, (u * 6) / 10, white);
    spr->fillRect(cx + u / 6, cy - u, (u * 3) / 10, (u * 17) / 10, white);
    spr->fillRect(cx + u / 6, cy - u, (u * 9) / 10, (u * 35) / 100, white);
  } else if (idx == 1) {
    int sw = (u * 26) / 10;
    int sh = (u * 16) / 10;
    int total_h = sh + 9;
    int top_y = cy - total_h / 2;
    int sx = cx - sw / 2;
    spr->fillRoundRect(sx - 2, top_y, sw + 4, sh + 4, 2, TFT_BLACK);
    spr->fillRect(sx, top_y + 2, sw, sh, white);
    int stem_w = (u * 4) / 10;
    if (stem_w < 2) stem_w = 2;
    spr->fillRect(cx - stem_w / 2, top_y + sh + 4, stem_w, 3, TFT_BLACK);
    int base_w = (u * 16) / 10;
    spr->fillRect(cx - base_w / 2, top_y + sh + 7, base_w, 2, TFT_BLACK);
  } else if (idx == 2) {
    spr->drawRect(cx - u, cy - (u * 8) / 10, u * 2, (u * 16) / 10, white);
    spr->fillRect(cx - u, cy - (u * 8) / 10, u * 2, (u * 4) / 10, white);
    spr->fillRect(cx - (u * 5) / 10, cy - (u * 11) / 10, (u * 2) / 10, (u * 4) / 10, white);
    spr->fillRect(cx + (u * 3) / 10, cy - (u * 11) / 10, (u * 2) / 10, (u * 4) / 10, white);
    spr->fillRect(cx - (u * 5) / 10, cy, (u * 2) / 10, (u * 2) / 10, white);
    spr->fillRect(cx - (u * 1) / 10, cy, (u * 2) / 10, (u * 2) / 10, white);
    spr->fillRect(cx + (u * 3) / 10, cy, (u * 2) / 10, (u * 2) / 10, white);
  } else if (idx == 3) {

    int rs = (u * 8) / 10;
    int r_in = u;
    int r_out = (u * 15) / 10;
    spr->fillCircle(cx, cy, rs, white);
    for (int i = 0; i < 8; i++) {
      float ang = i * PI / 4.0f;
      int x1 = cx + (int)(r_in * cosf(ang));
      int y1 = cy + (int)(r_in * sinf(ang));
      int x2 = cx + (int)(r_out * cosf(ang));
      int y2 = cy + (int)(r_out * sinf(ang));
      spr->drawLine(x1, y1, x2, y2, white);
      spr->drawLine(x1 + 1, y1, x2 + 1, y2, white);
      spr->drawLine(x1, y1 + 1, x2, y2 + 1, white);
      spr->drawLine(x1 + 1, y1 + 1, x2 + 1, y2 + 1, white);
    }
  } else {

    int r_outer = (u * 14) / 10;
    int r_root = (u * 10) / 10;
    int r_body = (u * 9) / 10;
    int r_hole = (u * 4) / 10;
    float w_root = u * 0.22f;
    float w_tip = u * 0.16f;
    for (int i = 0; i < 8; i++) {
      float ang = i * PI / 4.0f;
      float ca = cosf(ang);
      float sa = sinf(ang);
      float pc = -sa;
      float ps = ca;
      int x0 = cx + (int)(r_root * ca + pc * w_root);
      int y0 = cy + (int)(r_root * sa + ps * w_root);
      int x1 = cx + (int)(r_outer * ca + pc * w_tip);
      int y1 = cy + (int)(r_outer * sa + ps * w_tip);
      int x2 = cx + (int)(r_outer * ca - pc * w_tip);
      int y2 = cy + (int)(r_outer * sa - ps * w_tip);
      int x3 = cx + (int)(r_root * ca - pc * w_root);
      int y3 = cy + (int)(r_root * sa - ps * w_root);
      spr->fillTriangle(x0, y0, x1, y1, x2, y2, white);
      spr->fillTriangle(x0, y0, x2, y2, x3, y3, white);
    }
    spr->fillCircle(cx, cy, r_body, white);
    if (r_hole >= 2) {
      spr->fillCircle(cx, cy, r_hole, front);
    }
  }
}

static void drawSelectionFrame(LGFX_Sprite* spr, int cy) {
  const int x1 = 22;
  const int x2 = 236;
  const int w = x2 - x1 + 1;
  const int y1 = cy - 38;
  const int y2 = cy + 38;
  const int h = y2 - y1 + 1;
  const int r = 14;
  spr->drawRoundRect(x1 - 2, y1 - 2, w + 4, h + 4, r + 2, rgb565(10, 10, 22));
  spr->drawRoundRect(x1 - 1, y1 - 1, w + 2, h + 2, r + 1, rgb565(90, 110, 150));
  spr->drawRoundRect(x1, y1, w, h, r, rgb565(180, 210, 255));
  spr->drawRoundRect(x1 + 1, y1 + 1, w - 2, h - 2, r - 1, TFT_WHITE);
  spr->drawRoundRect(x1 + 2, y1 + 2, w - 4, h - 4, r - 2, rgb565(150, 190, 240));
  spr->drawFastHLine(x1 + r, y1 + 1, w - r * 2, TFT_WHITE);
  spr->drawFastHLine(x1 + r, y2 - 1, w - r * 2, rgb565(210, 225, 255));
}

static void generateBackground(LGFX_Sprite* spr) {
  const size_t need = (size_t)SCREEN_W * SCREEN_H * 2;
  memcpy(spr->getBuffer(), bg_image_pixels(), need);
}

static void precomputeMenuBg() {
  uint16_t* dst = (uint16_t*)menuBgSprite.getBuffer();
  const uint16_t* src = (const uint16_t*)bgSprite.getBuffer();
  if (!dst || !src) return;
  for (int y = 0; y < SCREEN_H; y++) {
    const uint16_t* sRow = src + (size_t)y * SCREEN_W;
    uint16_t* dRow = dst + (size_t)y * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t b = swap16(sRow[x]);
      int br = (b >> 11) & 0x1F;
      int bg = (b >> 5) & 0x3F;
      int bb = b & 0x1F;
      float dx = 1.0f - (float)x / (float)SCREEN_W;
      int bri = (int)((1.0f - (0.35f + 0.35f * dx)) * 512.0f);
      if (bri < 0) bri = 0;
      if (bri > 512) bri = 512;
      int R = (br * bri) >> 9;
      int G = (bg * bri) >> 9;
      int B = (bb * bri) >> 9;
      dRow[x] = swap16((uint16_t)((R << 11) | (G << 5) | B));
    }
  }
}

static void drawHome(LGFX_Sprite* c) {
  memcpy(c->getBuffer(), bgSprite.getBuffer(),
         (size_t)SCREEN_W * SCREEN_H * 2);
  push_with_fps(c);
}

static void renderMenuToSprite(float menuPos) {
  memcpy(menuSprite.getBuffer(), menuBgSprite.getBuffer(),
         (size_t)SCREEN_W * SCREEN_H * 2);
  menuSprite.setFont(&lgfx::fonts::efontCN_16);
  menuSprite.setTextSize((float)TEXT_SIZE);
  const float centerY = SCREEN_H * 0.5f;
  int baseK = (int)floorf(menuPos);
  for (int k = baseK - 2; k <= baseK + 2; k++) {
    float dy = ((float)k - menuPos) * (float)MENU_ITEM_H;
    float cyF = centerY + dy;
    if (cyF < -(float)MENU_ITEM_H || cyF > (float)(SCREEN_H + MENU_ITEM_H))
      continue;
    int idx = ((k % MENU_COUNT) + MENU_COUNT) % MENU_COUNT;
    int cy = (int)roundf(cyF);
    float t = fabsf(dy) / (float)MENU_ITEM_H;
    if (t > 1.0f) t = 1.0f;
    float scale = 1.25f - 0.25f * t;
    int iconSize = (int)(ICON_SIZE * scale);
    bool selected = (t < 0.15f);
    if (selected) {
      drawSelectionFrame(&menuSprite, cy);
    }
    drawIcon(&menuSprite, ICON_CX, cy, iconSize, idx);
    int bri = (int)(255.0f - 130.0f * t);
    if (bri < 125) bri = 125;
    if (bri > 255) bri = 255;
    int textW = menuSprite.textWidth(MENU_ITEMS[idx]);
    int textAreaW = TEXT_RIGHT - TEXT_LEFT;
    int textX = TEXT_LEFT + (textAreaW - textW) / 2;
    int textY = cy - (16 * TEXT_SIZE) / 2;
    menuSprite.setTextColor(rgb565(bri, bri, bri));
    menuSprite.setCursor(textX, textY);
    menuSprite.print(MENU_ITEMS[idx]);
  }
}

static inline uint16_t blend_pixel(uint16_t aRaw, uint16_t bRaw,
                                   int alpha, int inv) {
  uint16_t a = (uint16_t)((aRaw >> 8) | (aRaw << 8));
  uint16_t b = (uint16_t)((bRaw >> 8) | (bRaw << 8));
  int ar = (a >> 11) & 0x1F;
  int ag = (a >> 5) & 0x3F;
  int ab = a & 0x1F;
  int br = (b >> 11) & 0x1F;
  int bg = (b >> 5) & 0x3F;
  int bb = b & 0x1F;
  int R = ((ar * inv + br * alpha) >> 8) & 0x1F;
  int G = ((ag * inv + bg * alpha) >> 8) & 0x3F;
  int B = ((ab * inv + bb * alpha) >> 8) & 0x1F;
  uint16_t rgb = (uint16_t)((R << 11) | (G << 5) | B);
  return (uint16_t)((rgb >> 8) | (rgb << 8));
}

static LGFX_Sprite* gBlendSrcSprite = &bgSprite;

static inline void blend_set_src(LGFX_Sprite* s) {
  gBlendSrcSprite = s ? s : &bgSprite;
}

static void blend_sprites(LGFX_Sprite* dst,
                          const LGFX_Sprite* srcA,
                          const LGFX_Sprite* srcB, float t) {
  uint16_t* d = (uint16_t*)dst->getBuffer();
  const uint16_t* a = (const uint16_t*)srcA->getBuffer();
  const uint16_t* b = (const uint16_t*)srcB->getBuffer();
  if (!d || !a || !b) {
    static bool once = false;
    if (!once) {
      once = true;
      Serial.println("[BLEND] NULL buffer!");
    }
    return;
  }
  if (t <= 0.0f) {
    memcpy(d, a, (size_t)SCREEN_W * SCREEN_H * 2);
    return;
  }
  if (t >= 1.0f) {
    memcpy(d, b, (size_t)SCREEN_W * SCREEN_H * 2);
    return;
  }
  int alpha = (int)(t * 256.0f);
  int inv = 256 - alpha;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    d[i] = blend_pixel(a[i], b[i], alpha, inv);
  }
}

static void drawMenu(LGFX_Sprite* c, float t) {
  blend_sprites(c, gBlendSrcSprite, &menuSprite, t);
  push_with_fps(c);
}

static void drawLaunchFade(LGFX_Sprite* c, int frameIdx) {
  if (frameIdx < 0) frameIdx = 0;
  if (frameIdx >= FADE_FRAMES) frameIdx = FADE_FRAMES - 1;
  float t = (FADE_FRAMES > 1)
              ? (float)frameIdx / (float)(FADE_FRAMES - 1)
              : 1.0f;
  blend_sprites(c, &menuSprite, &appSprite, t);
  push_with_fps(c);
}