// app_monitor.h
#pragma once
#include <LovyanGFX.hpp>
#include <math.h>
#include <string.h>
#include "config.h"

#define HIST_N  60

extern float current_mon_val;
extern void update_led(float p);
extern void update_pwm(float p);

static float    _mon_target = 0.0f;
static float    _mon_value  = 0.0f;
static uint8_t  _mon_type   = 0;
static bool     _mon_dirty  = true;
static uint32_t _mon_last_ms = 0;

static float    _mon_last_drawn = -1.0f;

static uint8_t  _mon_hist[HIST_N];
static int      _mon_hist_idx   = 0;
static int      _mon_hist_count = 0;

static const char* _mon_type_names[3] = {"CPU", "RAM", "GPU"};

static void monitor_set_type(uint8_t t) {
    if (t > 2) t = 0;
    if (_mon_type != t) {
        _mon_type  = t;
        _mon_dirty = true;
    }
}

static void monitor_set_type_by_name(const char* name) {
    if (!name) return;
    if      (strncmp(name, "cpu", 3) == 0) monitor_set_type(0);
    else if (strncmp(name, "ram", 3) == 0) monitor_set_type(1);
    else if (strncmp(name, "gpu", 3) == 0) monitor_set_type(2);
}

static void monitor_set_cpu(float v) {
    if (v < 0.0f)   v = 0.0f;
    if (v > 100.0f) v = 100.0f;

    _mon_target = v;
    _mon_dirty  = true;

    _mon_hist[_mon_hist_idx] = (uint8_t)(v + 0.5f);
    _mon_hist_idx = (_mon_hist_idx + 1) % HIST_N;
    if (_mon_hist_count < HIST_N) _mon_hist_count++;
}

static void fill_gauge_aa(LGFX_Sprite* c, int cx, int cy, float rin, float rout, float a_start, float a_total, float ratio, uint16_t color, uint16_t dark_col, uint16_t base_col) {
    if (ratio <= 0.0f) return;
    if (ratio > 1.0f)  ratio = 1.0f;
    float progress_ang = a_total * ratio;

    int ix0 = cx - (int)rout - 2;
    int iy0 = cy - (int)rout - 2;
    int ix1 = cx + (int)rout + 2;
    int iy1 = cy + (int)rout + 2;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= SCREEN_W) ix1 = SCREEN_W - 1;
    if (iy1 >= SCREEN_H) iy1 = SCREEN_H - 1;

    int cr = (color    >> 11) & 0x1F;
    int cg = (color    >> 5)  & 0x3F;
    int cb =  color           & 0x1F;
    int dr = (dark_col >> 11) & 0x1F;
    int dg = (dark_col >> 5)  & 0x3F;
    int db =  dark_col        & 0x1F;
    int br = (base_col >> 11) & 0x1F;
    int bgc = (base_col >> 5) & 0x3F;
    int bb =  base_col        & 0x1F;

    float rin2   = rin  * rin;
    float rout2  = rout * rout;
    float rin_i  = rin  - 1.5f;
    float rout_i = rout - 1.5f;
    float rin_i2  = rin_i  * rin_i;
    float rout_i2 = rout_i * rout_i;

    const float INV = 0.25f;

    for (int py = iy0; py <= iy1; py++) {
        float dy = (float)(py - cy);
        for (int px = ix0; px <= ix1; px++) {
            float dx = (float)(px - cx);
            float d2 = dx * dx + dy * dy;

            if (d2 > (rout + 1.0f) * (rout + 1.0f)) continue;
            if (d2 < (rin  - 1.0f) * (rin  - 1.0f)) continue;

            float ang = atan2f(dy, dx) * 57.2957795f;
            if (ang < 0) ang += 360.0f;
            float rel = ang - a_start;
            if (rel < 0) rel += 360.0f;
            if (rel > a_total + 1.0f) continue;

            bool deep_in_ring  = (d2 < rout_i2) && (d2 > rin_i2);
            bool deep_in_angle = (rel > 1.5f) && (rel < a_total - 1.5f);

            if (deep_in_ring && deep_in_angle) {
                if (rel < progress_ang - 1.0f) {
                    c->drawPixel(px, py, color);
                } else if (rel > progress_ang + 1.0f) {
                    c->drawPixel(px, py, dark_col);
                } else {
                    int hw = 0;
                    for (int sy = 0; sy < 4; sy++) {
                        for (int sx = 0; sx < 4; sx++) {
                            float fx = dx - 0.5f + (sx + 0.5f) * INV;
                            float fy = dy - 0.5f + (sy + 0.5f) * INV;
                            float a2 = atan2f(fy, fx) * 57.2957795f;
                            if (a2 < 0) a2 += 360.0f;
                            float r2 = a2 - a_start;
                            if (r2 < 0) r2 += 360.0f;
                            if (r2 <= progress_ang) hw++;
                        }
                    }
                    if (hw == 0) {
                        c->drawPixel(px, py, dark_col);
                    } else if (hw == 16) {
                        c->drawPixel(px, py, color);
                    } else {
                        int a   = hw * 16;
                        int inv = 256 - a;
                        int R = ((cr * a + dr * inv) >> 8) & 0x1F;
                        int G = ((cg * a + dg * inv) >> 8) & 0x3F;
                        int B = ((cb * a + db * inv) >> 8) & 0x1F;
                        c->drawPixel(px, py, (R << 11) | (G << 5) | B);
                    }
                }
                continue;
            }

            int hits_in = 0;
            int hits_white = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float fx = dx - 0.5f + (sx + 0.5f) * INV;
                    float fy = dy - 0.5f + (sy + 0.5f) * INV;
                    float r2 = fx * fx + fy * fy;
                    if (r2 < rin2 || r2 > rout2) continue;
                    float a2 = atan2f(fy, fx) * 57.2957795f;
                    if (a2 < 0) a2 += 360.0f;
                    float rl = a2 - a_start;
                    if (rl < 0) rl += 360.0f;
                    if (rl > a_total) continue;
                    hits_in++;
                    if (rl <= progress_ang) hits_white++;
                }
            }
            if (hits_in == 0) continue;

            int base_n  = 16 - hits_in;
            int dark_n  = hits_in - hits_white;
            int white_n = hits_white;

            int R = (br  * base_n + dr * dark_n + cr * white_n) >> 4;
            int G = (bgc * base_n + dg * dark_n + cg * white_n) >> 4;
            int B = (bb  * base_n + db * dark_n + cb * white_n) >> 4;
            c->drawPixel(px, py, (R << 11) | (G << 5) | B);
        }
    }
}

static void draw_history(LGFX_Sprite* c, int x0, int y0, int w, int h) {
    c->fillRect(x0, y0, w, h, rgb565(18, 18, 28));
    c->drawRect(x0, y0, w, h, rgb565(60, 60, 80));

    for (int i = 1; i < 4; i++) {
        int y = y0 + h - (h * i / 4);
        c->drawFastHLine(x0 + 1, y, w - 2, rgb565(40, 40, 55));
    }

    c->setFont(&lgfx::fonts::Font2);
    c->setTextSize(1);
    c->setTextColor(rgb565(100, 100, 120));
    c->setCursor(x0 + w - 22, y0 + 1);
    c->print("100");
    c->setCursor(x0 + w - 10, y0 + h - 9);
    c->print("0");

    if (_mon_hist_count < 2) return;

    int n = _mon_hist_count;
    int start_idx = (_mon_hist_idx - n + HIST_N * 2) % HIST_N;

    int prev_x = 0, prev_y = 0;
    uint16_t line_col = TFT_WHITE;

    for (int i = 0; i < n; i++) {
        int idx = (start_idx + i) % HIST_N;
        int v   = _mon_hist[idx];
        int px  = x0 + 1 + (i * (w - 3)) / (HIST_N - 1);
        int py  = y0 + h - 1 - (v * (h - 4)) / 100;

        if (i > 0) {
            c->drawLine(prev_x, prev_y, px, py, line_col);
            c->drawLine(prev_x, prev_y + 1, px, py + 1, line_col);
        }
        prev_x = px;
        prev_y = py;
    }

    if (prev_x > 0) c->fillCircle(prev_x, prev_y, 2, rgb565(80, 220, 140));
}

static void monitor_draw_gauge(LGFX_Sprite* c, float value) {
    uint16_t base_col = rgb565(15, 15, 20);
    uint16_t dark_col = rgb565(40, 40, 55);

    c->fillScreen(base_col);

    const int   cx      = 120;
    const int   cy      = 125;
    const float r_out   = 95.0f;
    const float r_in    = 72.0f;
    const float a_start = 135.0f;
    const float a_end   = 405.0f;
    const float a_total = a_end - a_start;

    float ratio = value / 100.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    fill_gauge_aa(c, cx, cy, r_in, r_out, a_start, a_total, 1.0f, dark_col, dark_col, base_col);
    fill_gauge_aa(c, cx, cy, r_in, r_out, a_start, a_total, ratio, TFT_WHITE, dark_col, base_col);

    for (int i = 0; i <= 10; i++) {
        float a   = a_start + a_total * (i / 10.0f);
        float rad = a * PI / 180.0f;
        bool major = (i % 5 == 0);
        float in_r  = major ? (r_out + 4.0f) : (r_out + 8.0f);
        float out_r = r_out + 14.0f;
        int x1 = cx + (int)(in_r  * cosf(rad));
        int y1 = cy + (int)(in_r  * sinf(rad));
        int x2 = cx + (int)(out_r * cosf(rad));
        int y2 = cy + (int)(out_r * sinf(rad));
        c->drawLine(x1, y1, x2, y2, major ? TFT_WHITE : rgb565(100, 100, 110));
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%.0f", value);
    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(4);
    c->setTextColor(TFT_WHITE);
    int tw = c->textWidth(buf);
    int th = 16 * 4;
    c->setCursor(cx - tw / 2, cy - th / 2);
    c->print(buf);

    c->setTextSize(2);
    c->setTextColor(rgb565(180, 180, 190));
    c->setCursor(cx + tw / 2 + 2, cy + 8);
    c->print("%");

    c->setFont(&lgfx::fonts::Font2);
    c->setTextSize(2);
    c->setTextColor(rgb565(140, 140, 155));
    const char* label = _mon_type_names[_mon_type];
    tw = c->textWidth(label);
    c->setCursor(cx - tw / 2, cy + 48);
    c->print(label);

    draw_history(c, 15, 225, 210, 80);
}

static void monitor_hw_sync() {
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms < 30) return;
    last_ms = now;

    if (fabsf(_mon_target - _mon_value) > 0.05f) {
        _mon_value += (_mon_target - _mon_value) * 0.15f;
        current_mon_val = _mon_value;
        update_pwm(_mon_value);
    } else if (_mon_value != _mon_target) {
        _mon_value = _mon_target;
        current_mon_val = _mon_value;
        update_pwm(_mon_value);
    }
}

static void monitor_setup(LGFX_Sprite* c) {
    _mon_last_ms = 0;
    _mon_hist_idx   = 0;
    _mon_hist_count = 0;
    memset(_mon_hist, 0, sizeof(_mon_hist));
    _mon_dirty = true;
    _mon_last_drawn = _mon_value;
    esp_send_raw("MON_ON\n");          // ⭐ 双通道
    monitor_draw_gauge(c, _mon_value);
    push_with_fps(c);
}

static void monitor_loop(LGFX_Sprite* c) {
    uint32_t now = millis();

    // ⭐ 每 2 秒重发一次 MON_ON，确保 PC 端最终一定能收到
    static uint32_t last_mon_on = 0;
    if (now - last_mon_on > 2000) {
        last_mon_on = now;
        esp_send_raw("MON_ON\n");
    }

    bool need_redraw = false;

    if (_mon_dirty || fabsf(_mon_value - _mon_last_drawn) > 0.05f) {
        need_redraw = true;
        _mon_dirty = false;
    }

    update_led(_mon_value);

    static uint32_t last_draw_ms = 0;
    if (need_redraw && (now - last_draw_ms > 33)) {
        last_draw_ms = now;
        _mon_last_drawn = _mon_value;
        monitor_draw_gauge(c, _mon_value);
        push_with_fps(c);
    }
}

static void monitor_exit() {
    esp_send_raw("MON_OFF\n");         // ⭐ 双通道
    update_led(0.0f);
}