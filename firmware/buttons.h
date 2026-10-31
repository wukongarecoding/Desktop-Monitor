// buttons.h
#pragma once
#include <Arduino.h>
#include "config.h"

// =============================================================
//  三个按键 + 单击/双击检测（主循环轮询，不用中断）
//  单击：按下→释放，300ms 内无第二次按下
//  双击：两次按下间隔 < 300ms
// =============================================================

static const uint32_t DEBOUNCE_MS   = 30;
static const uint32_t DOUBLE_GAP_MS = 300;

struct ButtonState {
    uint8_t  pin;
    bool     is_down;
    uint32_t last_down_ms;
    uint32_t last_up_ms;
    bool     pending_single;   // 已收到第一次单击，等待确认是否双击
    bool     awaiting_second;  // 已确认进入双击流程，等第二次释放
    uint32_t last_change_ms;   // 消抖用
};

static ButtonState btns[3] = {
    {PIN_KEY1, false, 0, 0, false, false, 0},
    {PIN_KEY2, false, 0, 0, false, false, 0},
    {PIN_KEY3, false, 0, 0, false, false, 0},
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

        // ---- 消抖 ----
        if (raw != b.is_down) {
            if (now - b.last_change_ms < DEBOUNCE_MS) {
                continue;   // 抖动期，忽略
            }
            b.last_change_ms = now;
            b.is_down = raw;

            if (raw) {
                // 按下
                b.last_down_ms = now;
                if (b.pending_single) {
                    // 300ms 内第二次按下 → 进入双击流程
                    b.pending_single = false;
                    b.awaiting_second = true;
                }
            } else {
                // 释放
                b.last_up_ms = now;
                if (b.awaiting_second) {
                    // 双击第二下释放 → 立即触发双击
                    Serial.printf("KEY%d_D\n", i + 1);
                    b.awaiting_second = false;
                } else {
                    // 第一次释放 → 挂起等待双击确认
                    b.pending_single = true;
                }
            }
        }

        // ---- 单击超时确认 ----
        if (b.pending_single && !b.is_down &&
            (now - b.last_up_ms > DOUBLE_GAP_MS)) {
            Serial.printf("KEY%d\n", i + 1);
            b.pending_single = false;
        }

        // ---- 双击超时兜底（第二次按住不松太久，取消双击） ----
        if (b.awaiting_second && b.is_down &&
            (now - b.last_down_ms > 1000)) {
            b.awaiting_second = false;
            // 超时就当普通单击处理
            Serial.printf("KEY%d\n", i + 1);
        }
    }
}