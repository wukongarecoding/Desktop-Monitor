// app_weather.h
#pragma once
#include <LovyanGFX.hpp>
#include <string.h>
#include <math.h>
#include "config.h"

struct WeatherDay { int code; int tmax; int tmin; };

static char        _weather_city[32]  = "";
static int         _weather_temp      = 0;
static int         _weather_feels     = 0;
static int         _weather_hum       = 0;
static int         _weather_wind      = 0;
static int         _weather_code      = 0;
static int         _weather_tmax      = 0;
static int         _weather_tmin      = 0;
static WeatherDay  _weather_fc[3];
static bool        _weather_valid     = false;
static bool        _weather_dirty     = true;

// ── 配色 ──────────────────────────────────────
#define WC_CARD        rgb565(30, 38, 58)
#define WC_CARD_BORD   rgb565(52, 64, 92)
#define WC_TXT         rgb565(240, 246, 255)
#define WC_TXT_DIM     rgb565(150, 165, 195)
#define WC_TXT_MUTE    rgb565(105, 118, 145)
#define WC_ACCENT      rgb565(110, 200, 255)
#define WC_WARM        rgb565(255, 180, 90)
#define WC_COOL        rgb565(140, 180, 235)
#define WC_GREEN       rgb565(90, 215, 140)
#define WC_SUN         rgb565(255, 210, 90)
#define WC_CLOUD       rgb565(215, 225, 245)
#define WC_RAIN        rgb565(120, 185, 250)
#define WC_SNOW        rgb565(230, 240, 255)
#define WC_BOLT        rgb565(255, 225, 110)

static const char* weather_text(int code) {
    if (code == 0)  return "晴";
    if (code <= 2)  return "多云";
    if (code == 3)  return "阴";
    if (code <= 48) return "雾";
    if (code <= 57) return "毛毛雨";
    if (code <= 67) return "雨";
    if (code <= 77) return "雪";
    if (code <= 82) return "阵雨";
    if (code <= 86) return "阵雪";
    if (code >= 95) return "雷阵雨";
    return "未知";
}

static inline void _draw_deg_dot(LGFX_Sprite* c, int x, int y, int r, uint16_t color) {
    c->drawCircle(x, y, r, color);
    if (r > 1) c->drawCircle(x, y, r - 1, color);
}

// ── 天气图标几何绘制 ─────────────────────────
static void _draw_sun(LGFX_Sprite* c, int cx, int cy, int r, uint16_t color) {
    c->fillCircle(cx, cy, r, color);
    for (int i = 0; i < 8; i++) {
        float a = i * 3.14159f / 4.0f;
        int x1 = cx + (int)(cosf(a) * (r + 3));
        int y1 = cy + (int)(sinf(a) * (r + 3));
        int x2 = cx + (int)(cosf(a) * (r + 6));
        int y2 = cy + (int)(sinf(a) * (r + 6));
        c->drawLine(x1, y1, x2, y2, color);
    }
}

static void _draw_cloud(LGFX_Sprite* c, int cx, int cy, uint16_t color) {
    c->fillCircle(cx - 8, cy + 2, 6, color);
    c->fillCircle(cx + 8, cy + 2, 6, color);
    c->fillCircle(cx, cy - 3, 8, color);
    c->fillRoundRect(cx - 14, cy + 1, 28, 8, 4, color);
}

static void _draw_weather_icon(LGFX_Sprite* c, int cx, int cy, int code) {
    if (code == 0) {
        _draw_sun(c, cx, cy, 9, WC_SUN);
    } else if (code <= 2) {
        _draw_sun(c, cx - 6, cy - 6, 6, WC_SUN);
        _draw_cloud(c, cx + 3, cy + 5, WC_CLOUD);
    } else if (code == 3) {
        _draw_cloud(c, cx, cy, WC_CLOUD);
    } else if (code <= 48) {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        for (int i = 0; i < 3; i++) {
            int y = cy + 4 + i * 4;
            int w = 24 - i * 4;
            c->drawFastHLine(cx - w / 2, y, w, WC_TXT_DIM);
        }
    } else if (code <= 57) {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        for (int i = 0; i < 3; i++) {
            int x = cx - 8 + i * 8;
            c->fillCircle(x, cy + 8, 1, WC_RAIN);
        }
    } else if (code <= 67) {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        for (int i = 0; i < 3; i++) {
            int x = cx - 8 + i * 8;
            c->drawLine(x, cy + 4, x - 2, cy + 12, WC_RAIN);
        }
    } else if (code <= 77) {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        for (int i = 0; i < 3; i++) {
            int x = cx - 8 + i * 8;
            c->fillCircle(x, cy + 8, 2, WC_SNOW);
        }
    } else if (code <= 82) {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        for (int i = 0; i < 4; i++) {
            int x = cx - 10 + i * 7;
            c->drawLine(x, cy + 3, x - 3, cy + 13, WC_RAIN);
        }
    } else if (code <= 86) {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        for (int i = 0; i < 4; i++) {
            int x = cx - 10 + i * 7;
            c->fillCircle(x, cy + 8, 2, WC_SNOW);
        }
    } else {
        _draw_cloud(c, cx, cy - 6, WC_CLOUD);
        c->fillTriangle(cx + 2, cy + 2, cx - 4, cy + 14, cx + 4, cy + 13, WC_BOLT);
    }
}

// ── 主绘制 ───────────────────────────────────
// ── 主绘制 ───────────────────────────────────
static void weather_draw(LGFX_Sprite* c) {
    // 竖向渐变背景
    for (int y = 0; y < SCREEN_H; y++) {
        float t = (float)y / (float)SCREEN_H;
        int r = 12 + (int)((26 - 12) * t);
        int g = 16 + (int)((32 - 16) * t);
        int b = 30 + (int)((52 - 30) * t);
        c->drawFastHLine(0, y, SCREEN_W, rgb565(r, g, b));
    }

    if (!_weather_valid) {
        c->setFont(&lgfx::fonts::efontCN_16);
        c->setTextSize(2);
        c->setTextColor(WC_TXT_DIM);
        const char* t = "加载天气...";
        int tw = c->textWidth(t);
        c->setCursor(120 - tw / 2, 140);
        c->print(t);
        return;
    }

    char buf[16];

    // ── 城市 + 强调横线 ──
    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(2);
    c->setTextColor(WC_TXT);
    int tw = c->textWidth(_weather_city);
    c->setCursor(120 - tw / 2, 6);
    c->print(_weather_city);
    c->drawFastHLine(120 - 20, 42, 40, WC_ACCENT);

    // ── 图标 + 大温度 + °C（整组水平居中，下移 row_cy 到 90） ──
    snprintf(buf, sizeof(buf), "%d", _weather_temp);

    c->setFont(&lgfx::fonts::DejaVu40);
    int tw_num = c->textWidth(buf);
    int th_num = c->fontHeight();

    c->setFont(&lgfx::fonts::DejaVu24);
    int tw_C  = c->textWidth("C");

    const int ICON_BOX  = 34;
    const int GAP_IC_N  = 8;
    const int DOT_R     = 3;
    const int GAP_N_DOT = 6;
    const int GAP_DOT_C = 2;

    int total_w = ICON_BOX + GAP_IC_N + tw_num + GAP_N_DOT
                  + DOT_R * 2 + GAP_DOT_C + tw_C;
    int x0 = 120 - total_w / 2;

    int row_cy = 90;                                    // ← 76 → 90
    int icon_cx = x0 + ICON_BOX / 2;
    _draw_weather_icon(c, icon_cx, row_cy, _weather_code);

    int num_x = x0 + ICON_BOX + GAP_IC_N;
    int num_y = row_cy - th_num / 2;
    c->setFont(&lgfx::fonts::DejaVu40);
    c->setTextColor(WC_TXT);
    c->setCursor(num_x, num_y);
    c->print(buf);

    int dot_cx = num_x + tw_num + GAP_N_DOT + DOT_R;
    int dot_cy = num_y + DOT_R + 6;
    _draw_deg_dot(c, dot_cx, dot_cy, DOT_R, WC_ACCENT);

    c->setFont(&lgfx::fonts::DejaVu24);
    c->setTextColor(WC_ACCENT);
    c->setCursor(dot_cx + DOT_R + GAP_DOT_C, num_y + 14);
    c->print("C");

    // ── 天气状况（112 → 122） ──
    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(2);
    c->setTextColor(WC_TXT_DIM);
    const char* wt = weather_text(_weather_code);
    tw = c->textWidth(wt);
    c->setCursor(120 - tw / 2, 122);                    // ← 110 → 122
    c->print(wt);

    // ── 温度槽（下移 bar_y 到 180） ──
    const int bar_x = 24, bar_w = 192, bar_y = 180, bar_h = 8, bar_r = 4;   // ← 168 → 180

    c->setFont(&lgfx::fonts::DejaVu18);
    c->setTextSize(1);
    c->setTextColor(WC_COOL);
    snprintf(buf, sizeof(buf), "%d", _weather_tmin);
    c->setCursor(bar_x, bar_y - 22);
    c->print(buf);
    {
        int tw_n = c->textWidth(buf);
        _draw_deg_dot(c, bar_x + tw_n + 3, bar_y - 22 + 4, 2, WC_COOL);
    }

    c->setTextColor(WC_WARM);
    snprintf(buf, sizeof(buf), "%d", _weather_tmax);
    int tw_max = c->textWidth(buf);
    int mx_x = bar_x + bar_w - tw_max;
    c->setCursor(mx_x, bar_y - 22);
    c->print(buf);
    _draw_deg_dot(c, mx_x + tw_max + 3, bar_y - 22 + 4, 2, WC_WARM);

    c->fillRoundRect(bar_x, bar_y, bar_w, bar_h, bar_r, WC_CARD);
    c->drawRoundRect(bar_x, bar_y, bar_w, bar_h, bar_r, WC_CARD_BORD);

    float ratio = 0.0f;
    if (_weather_tmax > _weather_tmin) {
        ratio = (float)(_weather_temp - _weather_tmin) /
                (float)(_weather_tmax - _weather_tmin);
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
    }
    int fill_w = (int)(bar_w * ratio);
    if (fill_w > 2) {
        int rr = bar_r;
        if (fill_w < bar_r * 2) rr = fill_w / 2;
        c->fillRoundRect(bar_x, bar_y, fill_w, bar_h, rr, WC_GREEN);
    }

    int marker_x = bar_x + fill_w;
    if (marker_x < bar_x + 5) marker_x = bar_x + 5;
    if (marker_x > bar_x + bar_w - 5) marker_x = bar_x + bar_w - 5;
    c->fillCircle(marker_x, bar_y + bar_h / 2, 5, rgb565(14, 18, 30));
    c->drawCircle(marker_x, bar_y + bar_h / 2, 5, WC_TXT);
    c->fillCircle(marker_x, bar_y + bar_h / 2, 3, WC_GREEN);

    snprintf(buf, sizeof(buf), "%d", _weather_temp);
    c->setFont(&lgfx::fonts::DejaVu18);
    c->setTextColor(WC_TXT);
    int tw_cur = c->textWidth(buf);
    int cx = marker_x;
    int cx_min = bar_x + tw_cur / 2;
    int cx_max = bar_x + bar_w - tw_cur / 2;
    if (cx < cx_min) cx = cx_min;
    if (cx > cx_max) cx = cx_max;
    int cur_x = cx - tw_cur / 2;
    c->setCursor(cur_x, bar_y + bar_h + 6);
    c->print(buf);
    _draw_deg_dot(c, cur_x + tw_cur + 3, bar_y + bar_h + 6 + 4, 2, WC_TXT);

    // ── 分割线（210 → 224） ──
    c->drawFastHLine(20, 224, 200, WC_CARD_BORD);       // ← 210 → 224

    // ── 明天 / 后天（222/264 → 236/278） ──
    static const char* day_names[2] = { "明天", "后天" };
    for (int i = 0; i < 2; i++) {
        int y = 236 + i * 42;                           // ← 222 + i*42

        c->setFont(&lgfx::fonts::efontCN_16);
        c->setTextSize(1);
        c->setTextColor(WC_TXT_MUTE);
        c->setCursor(20, y);
        c->print(day_names[i]);

        _draw_weather_icon(c, 84, y + 7, _weather_fc[i].code);

        c->setFont(&lgfx::fonts::efontCN_16);
        c->setTextColor(WC_TXT);
        c->setCursor(108, y);
        c->print(weather_text(_weather_fc[i].code));

        c->setFont(&lgfx::fonts::DejaVu18);
        c->setTextColor(WC_COOL);
        snprintf(buf, sizeof(buf), "%d", _weather_fc[i].tmin);
        c->setCursor(158, y - 1);
        c->print(buf);
        {
            int tw_n = c->textWidth(buf);
            _draw_deg_dot(c, 158 + tw_n + 2, y - 1 + 4, 2, WC_COOL);
        }

        c->setTextColor(WC_WARM);
        snprintf(buf, sizeof(buf), "%d", _weather_fc[i].tmax);
        c->setCursor(202, y - 1);
        c->print(buf);
        {
            int tw_n = c->textWidth(buf);
            _draw_deg_dot(c, 202 + tw_n + 2, y - 1 + 4, 2, WC_WARM);
        }
    }
}

static void weather_set_data(const char* city, int temp, int feels, int hum, int wind,
                              int code, int tmax, int tmin,
                              int d1c, int d1max, int d1min,
                              int d2c, int d2max, int d2min,
                              int d3c, int d3max, int d3min) {
    strncpy(_weather_city, city, sizeof(_weather_city) - 1);
    _weather_city[sizeof(_weather_city) - 1] = 0;
    _weather_temp = temp; _weather_feels = feels;
    _weather_hum = hum;   _weather_wind = wind;
    _weather_code = code; _weather_tmax = tmax; _weather_tmin = tmin;
    _weather_fc[0] = { d1c, d1max, d1min };
    _weather_fc[1] = { d2c, d2max, d2min };
    _weather_fc[2] = { d3c, d3max, d3min };
    _weather_valid = true;
    _weather_dirty = true;
}

// ⭐ 保留原注释：不要在此清 _weather_dirty
static void weather_setup(LGFX_Sprite* c) {
    weather_draw(c);
    push_with_fps(c);
    esp_send_raw("WEA_ON\n");
}

static void weather_loop(LGFX_Sprite* c) {
    if (!_weather_dirty) return;
    _weather_dirty = false;
    weather_draw(c);
    push_with_fps(c);
}

static void weather_exit() { esp_send_raw("WEA_OFF\n"); }