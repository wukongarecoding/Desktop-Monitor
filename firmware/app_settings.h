#pragma once
#include <LovyanGFX.hpp>
#include <string.h>
#include <stdint.h>
#include "config.h"

extern bool wifi_enabled;
extern bool wifi_connected;
extern char wifi_ssid[];
extern char wifi_ip[];
extern bool ws_server_started;
extern uint8_t ws_client;

static bool _settings_dirty = true;

static void settings_draw(LGFX_Sprite* c) {
  c->fillScreen(rgb565(15, 20, 30));

  c->setFont(&lgfx::fonts::efontCN_16);
  c->setTextSize(2);
  c->setTextColor(TFT_WHITE);
  const char* title = "设置";
  int tw = c->textWidth(title);
  c->setCursor(120 - tw / 2, 10);
  c->print(title);

  c->drawFastHLine(20, 52, 200, rgb565(60, 70, 90));

  c->setTextSize(1);
  char buf[64];
  int y = 68;

  c->setTextColor(rgb565(140, 160, 200));
  c->setCursor(15, y);
  c->print("WiFi");
  if (wifi_ssid[0]) {
    c->setTextColor(TFT_WHITE);
    c->setCursor(90, y);
    c->print(wifi_ssid);
  } else {
    c->setTextColor(rgb565(120, 120, 130));
    c->setCursor(90, y);
    c->print("(未配置)");
  }
  y += 28;

  c->setTextColor(rgb565(140, 160, 200));
  c->setCursor(15, y);
  c->print("IP");
  c->setTextColor(TFT_WHITE);
  c->setCursor(90, y);
  c->print((wifi_connected && wifi_ip[0]) ? wifi_ip : "—");
  y += 28;

  c->setTextColor(rgb565(140, 160, 200));
  c->setCursor(15, y);
  c->print("WiFi 状态");
  c->setTextColor(wifi_connected ? rgb565(80, 200, 120) : rgb565(200, 100, 100));
  c->setCursor(90, y);
  c->print(wifi_connected ? "已连接" : (wifi_enabled ? "连接中" : "关闭"));
  y += 28;


  c->setTextColor(rgb565(140, 160, 200));
  c->setCursor(15, y);
  c->print("WebSocket");
  c->setTextColor(ws_server_started ? rgb565(80, 200, 120) : rgb565(120, 120, 130));
  c->setCursor(90, y);
  snprintf(buf, sizeof(buf), "%s : 81", ws_server_started ? "监听" : "关闭");
  c->print(buf);
  y += 28;


  c->setTextColor(rgb565(140, 160, 200));
  c->setCursor(15, y);
  c->print("客户端");
  c->setTextColor(TFT_WHITE);
  c->setCursor(90, y);
  snprintf(buf, sizeof(buf), "%s",
           (ws_client != 0xFF) ? "已连接" : "无");
  c->print(buf);
  y += 36;

  c->setTextColor(rgb565(120, 130, 160));
  c->setCursor(15, y);
  c->print("WiFi SSID/密码请在上位机");
  c->setCursor(15, y + 20);
  c->print("「网络」页发送配置命令");

  c->setCursor(15, 300);
  c->setTextColor(rgb565(100, 110, 140));
  c->print("BACK 返回");
}

static void settings_setup(LGFX_Sprite* c) {
  _settings_dirty = false;
  settings_draw(c);
  push_with_fps(c);
  esp_send_raw("SET_ON\n");
}

static void settings_loop(LGFX_Sprite* c) {
  if (!_settings_dirty) return;
  _settings_dirty = false;
  settings_draw(c);
  push_with_fps(c);
}

static void settings_exit() {
  esp_send_raw("SET_OFF\n");
}