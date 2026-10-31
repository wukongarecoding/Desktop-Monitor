// app_calendar.h
#pragma once
#include <LovyanGFX.hpp>
#include <math.h>
#include <string.h>
#include "config.h"

static const uint32_t _lunarInfo[] = {
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0,
    0x0abb7, 0x025d0, 0x092d0, 0x0cab5, 0x0a950, 0x0b4a0,
    0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176,
    0x052b0, 0x0a930, 0x07954, 0x06aa0, 0x0ad50, 0x05b52,
    0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0,
    0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0,
    0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255,
    0x06d20, 0x0ada0, 0x14b63,
};
#define LUNAR_Y0 2000
#define LUNAR_Y1 2050

static int lunar_leap_month(int y) {
    int i = y - LUNAR_Y0;
    if (i < 0 || i >= (int)(sizeof(_lunarInfo)/4)) return 0;
    return _lunarInfo[i] & 0xF;
}
static int lunar_leap_month_days(int y) {
    int i = y - LUNAR_Y0;
    if (i < 0 || i >= (int)(sizeof(_lunarInfo)/4)) return 0;
    if (!(_lunarInfo[i] & 0xF)) return 0;
    return (_lunarInfo[i] & 0x10000) ? 30 : 29;
}
static int lunar_month_days(int y, int m) {
    int i = y - LUNAR_Y0;
    if (i < 0 || i >= (int)(sizeof(_lunarInfo)/4)) return 29;
    return (_lunarInfo[i] & (0x10000 >> m)) ? 30 : 29;
}
static int solar_month_days(int y, int m) {
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y%4==0 && y%100!=0) || y%400==0)) return 29;
    return dim[m-1];
}
static long days_from_2000(int y, int m, int d) {
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    long days = 0;
    for (int yy = 2000; yy < y; yy++) {
        days += ((yy%4==0 && yy%100!=0) || yy%400==0) ? 366 : 365;
    }
    for (int mm = 1; mm < m; mm++) {
        days += dim[mm-1] + ((mm==2 && ((y%4==0&&y%100!=0)||y%400==0)) ? 1 : 0);
    }
    days += d - 1;
    return days;
}
static void solar_to_lunar(int sy, int sm, int sd,
                           int& ly, int& lm, int& ld, bool& leap) {
    long off = days_from_2000(sy, sm, sd) - days_from_2000(2000, 2, 5);
    ly = 2000; lm = 1; ld = 1; leap = false;
    if (off < 0) return;
    while (true) {
        int mdays = leap ? lunar_leap_month_days(ly) : lunar_month_days(ly, lm);
        int remain = mdays - ld;
        if (off <= remain) { ld += off; return; }
        off -= remain + 1;
        ld = 1;
        int leap_m = lunar_leap_month(ly);
        if (!leap && leap_m == lm) leap = true;
        else {
            leap = false;
            lm++;
            if (lm > 12) { lm = 1; ly++; }
        }
    }
}

static const char* _lunar_month_names[13] = {
    "", "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月"
};
static const char* _lunar_day_names[31] = {
    "",
    "初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
    "十一","十二","十三","十四","十五","十六","十七","十八","十九","二十",
    "二一","二二","二三","二四","二五","二六","二七","二八","二九","三十"
};
static const char* _stem_names[10] = {"甲","乙","丙","丁","戊","己","庚","辛","壬","癸"};
static const char* _branch_names[12] = {"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"};

extern int g_time_y, g_time_mo, g_time_d;
extern bool g_time_valid;

static int      _cal_year = 2026;
static int      _cal_month = 1;
static int      _cal_sel_day = 1;
static uint8_t  _cal_level = 2;
static bool     _cal_dirty = true;

static void calendar_draw(LGFX_Sprite* c) {
    c->fillScreen(rgb565(15, 15, 20));

    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(2);
    char buf[32];

    snprintf(buf, sizeof(buf), "%d年", _cal_year);
    int tw1 = c->textWidth(buf);
    snprintf(buf, sizeof(buf), "%d月", _cal_month);
    int tw2 = c->textWidth(buf);
    const int gap = 12;
    int total_w = tw1 + gap + tw2;
    int x0 = 120 - total_w / 2;

    c->setTextColor((_cal_level == 0) ? TFT_YELLOW : TFT_WHITE);
    snprintf(buf, sizeof(buf), "%d年", _cal_year);
    c->setCursor(x0, 8);
    c->print(buf);
    int x_year = x0, w_year = tw1;

    c->setTextColor((_cal_level == 1) ? TFT_YELLOW : TFT_WHITE);
    snprintf(buf, sizeof(buf), "%d月", _cal_month);
    c->setCursor(x0 + tw1 + gap, 8);
    c->print(buf);
    int x_month = x0 + tw1 + gap, w_month = tw2;

    if (_cal_level == 0) {
        c->drawFastHLine(x_year, 40, w_year, TFT_YELLOW);
        c->drawFastHLine(x_year, 41, w_year, TFT_YELLOW);
    } else if (_cal_level == 1) {
        c->drawFastHLine(x_month, 40, w_month, TFT_YELLOW);
        c->drawFastHLine(x_month, 41, w_month, TFT_YELLOW);
    }

    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(1);
    c->setTextColor(rgb565(130, 130, 150));
    static const char* _wn[7] = {"日","一","二","三","四","五","六"};
    const int cw = 32;
    const int gx0 = (SCREEN_W - cw * 7) / 2;
    for (int i = 0; i < 7; i++) {
        int tw = c->textWidth(_wn[i]);
        c->setCursor(gx0 + i * cw + (cw - tw) / 2, 52);
        c->print(_wn[i]);
    }
    c->drawFastHLine(8, 74, SCREEN_W - 16, rgb565(50, 50, 70));

    const int ch  = 30;
    const int gy0 = 82;
    int wday  = (int)((days_from_2000(_cal_year, _cal_month, 1) + 6) % 7);
    int mdays = solar_month_days(_cal_year, _cal_month);

    for (int d = 1; d <= mdays; d++) {
        int cell = wday + d - 1;
        int row  = cell / 7;
        int col  = cell % 7;
        int cx   = gx0 + col * cw + cw / 2;
        int cy   = gy0 + row * ch;

        bool selected = (_cal_level == 2 && d == _cal_sel_day);
        if (selected) {
            c->fillRoundRect(cx - 13, cy + 1, 26, 26, 5, rgb565(60, 90, 160));
        }

        snprintf(buf, sizeof(buf), "%d", d);
        c->setFont(&lgfx::fonts::efontCN_16);
        c->setTextSize(1);
        c->setTextColor(selected ? TFT_WHITE : TFT_LIGHTGREY);
        int tw = c->textWidth(buf);
        int th = 16;
        c->setCursor(cx - tw / 2, cy + (ch - th) / 2);
        c->print(buf);
    }

    c->drawFastHLine(8, 268, SCREEN_W - 16, rgb565(50, 50, 70));
    c->setFont(&lgfx::fonts::efontCN_16);
    c->setTextSize(1);
    c->setTextColor(rgb565(200, 200, 210));

    int ly, lm, ld; bool leap;
    solar_to_lunar(_cal_year, _cal_month, _cal_sel_day, ly, lm, ld, leap);
    if (lm < 1 || lm > 12) lm = 1;
    if (ld < 1 || ld > 30) ld = 1;

    snprintf(buf, sizeof(buf), "农历 %s%s%s",
             leap ? "闰" : "", _lunar_month_names[lm], _lunar_day_names[ld]);
    int tw = c->textWidth(buf);
    c->setCursor(120 - tw / 2, 278);
    c->print(buf);

    snprintf(buf, sizeof(buf), "%s%s年",
             _stem_names[(ly - 4) % 10],
             _branch_names[(ly - 4) % 12]);
    c->setTextColor(rgb565(160, 160, 180));
    tw = c->textWidth(buf);
    c->setCursor(120 - tw / 2, 300);
    c->print(buf);

    c->setFont(&lgfx::fonts::Font2);
    c->setTextColor(rgb565(90, 90, 110));
    static const char* _lvl_name[3] = {"[Y]", "[M]", "[D]"};
    c->setCursor(4, 4);
    c->print(_lvl_name[_cal_level]);
}

static void calendar_on_encoder(EncoderEvent e) {
    if (e == ENC_PRESS) {
        _cal_level = (_cal_level + 1) % 3;
        _cal_dirty = true;
        return;
    }
    if (e != ENC_LEFT && e != ENC_RIGHT) return;
    int delta = (e == ENC_RIGHT) ? 1 : -1;

    if (_cal_level == 0) {
        _cal_year += delta;
        if (_cal_year < LUNAR_Y0) _cal_year = LUNAR_Y0;
        if (_cal_year > LUNAR_Y1) _cal_year = LUNAR_Y1;
        int mdays = solar_month_days(_cal_year, _cal_month);
        if (_cal_sel_day > mdays) _cal_sel_day = mdays;
    } else if (_cal_level == 1) {
        _cal_month += delta;
        if (_cal_month < 1)  { _cal_month = 12; _cal_year--; }
        if (_cal_month > 12) { _cal_month = 1;  _cal_year++; }
        if (_cal_year < LUNAR_Y0) { _cal_year = LUNAR_Y0; _cal_month = 1; }
        if (_cal_year > LUNAR_Y1) { _cal_year = LUNAR_Y1; _cal_month = 12; }
        int mdays = solar_month_days(_cal_year, _cal_month);
        if (_cal_sel_day > mdays) _cal_sel_day = mdays;
    } else {
        int mdays = solar_month_days(_cal_year, _cal_month);
        _cal_sel_day += delta;
        if (_cal_sel_day < 1) {
            _cal_month--;
            if (_cal_month < 1) { _cal_month = 12; _cal_year--; }
            if (_cal_year < LUNAR_Y0) _cal_year = LUNAR_Y0;
            _cal_sel_day = solar_month_days(_cal_year, _cal_month);
        } else if (_cal_sel_day > mdays) {
            _cal_month++;
            if (_cal_month > 12) { _cal_month = 1; _cal_year++; }
            if (_cal_year > LUNAR_Y1) _cal_year = LUNAR_Y1;
            _cal_sel_day = 1;
        }
    }
    _cal_dirty = true;
}

static void calendar_preview(LGFX_Sprite* c) {
    if (g_time_valid) {
        _cal_year = g_time_y;
        _cal_month = g_time_mo;
        _cal_sel_day = g_time_d;
    } else {
        _cal_year = 2026; _cal_month = 1; _cal_sel_day = 1;
    }
    _cal_level = 2;
    calendar_draw(c);
}

static void calendar_setup(LGFX_Sprite* c) {
    calendar_preview(c);
    _cal_dirty = false;
    esp_send_raw("CAL_ON\n");          // ⭐ 双通道
    push_with_fps(c);
}

static void calendar_loop(LGFX_Sprite* c) {
    if (!_cal_dirty) return;
    _cal_dirty = false;
    calendar_draw(c);
    push_with_fps(c);
}

static void calendar_exit() {
    esp_send_raw("CAL_OFF\n");         // ⭐ 双通道
}