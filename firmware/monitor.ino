#include <Arduino.h>
#include <FFat.h>
#include <WiFi.h>
#include <stdarg.h>
#include <WebSocketsServer.h>

#include "LGFX_ILI9341_Parallel.hpp"
#include "config.h"
#include "encoder.h"
#include "ui.h"
#include "app_music.h"
#include "app_monitor.h"
#include "app_calendar.h"
#include "app_weather.h"
#include "app_settings.h"
#include "buttons.h"

LGFX lcd;
LGFX_Sprite bgSprite(&lcd);
LGFX_Sprite menuBgSprite(&lcd);
LGFX_Sprite canvas(&lcd);
LGFX_Sprite menuSprite(&lcd);
LGFX_Sprite appSprite(&lcd);

enum UIState { ST_HOME,
               ST_MENU,
               ST_APP,
               ST_LAUNCHING };
static UIState state = ST_HOME;
static int menuSel = 0;
static float menuPos = 0.0f;
static float menuTargetPos = 0.0f;
static float menuT = 0.0f;
static int menuDir = 0;
static int launchFrameIdx = 0;
static uint32_t hb_last_ms = 0;

static int menuMod(int v);


bool wifi_enabled = false;
bool wifi_connected = false;
char wifi_ssid[33] = { 0 };
char wifi_pwd[65] = { 0 };
char wifi_ip[16] = { 0 };

static int wifi_state = 0;
static uint32_t wifi_connect_start = 0;


WebSocketsServer webSocket(81);
bool ws_server_started = false;
uint8_t ws_client = 0xFF;

#define WIFI_CFG_PATH "/wifi.cfg"

enum BinKind { BIN_NONE = 0,
               BIN_BG,
               BIN_COVER };
struct PendingBin {
  BinKind kind = BIN_NONE;
  uint32_t id = 0;
  uint8_t* buf = nullptr;
  uint32_t total = 0;
  uint32_t got = 0;
  uint32_t last_ms = 0;
  uint32_t timeout_ms = 15000;
} _pending;

static void bin_cleanup() {
  if (_pending.buf) {
    free(_pending.buf);
    _pending.buf = nullptr;
  }
  _pending.kind = BIN_NONE;
  _pending.total = 0;
  _pending.got = 0;
  _pending.id = 0;
}


#define PIN_PWM_OUT 18
#define PIN_WS2812 48

static bool hwd_led_on = true;
static bool hwd_volt_on = true;
static float hwd_max_volt = 4.8f;
float current_mon_val = 0.0f;

static void hwd_init() {
  ledcAttach(PIN_PWM_OUT, 20000, 8);
  ledcWrite(PIN_PWM_OUT, 0);
  neopixelWrite(PIN_WS2812, 0, 0, 0);
}

void update_led(float p) {
  if (!hwd_led_on || p < 0.5f) {
    neopixelWrite(PIN_WS2812, 0, 0, 0);
    return;
  }
  int r = 0, g = 0, b = 0;
  if (p <= 50.0f) {
    r = (int)(p * 2.0f * 2.55f);
    g = 255;
  } else {
    r = 255;
    g = (int)((100.0f - p) * 2.0f * 2.55f);
  }
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  neopixelWrite(PIN_WS2812, r, g, b);
}

void update_pwm(float p) {
  if (!hwd_volt_on) {
    ledcWrite(PIN_PWM_OUT, 0);
    return;
  }
  float duty = (p / 100.0f) * (hwd_max_volt / 4.8f);
  if (duty > 1.0f) duty = 1.0f;
  if (duty < 0.0f) duty = 0.0f;
  ledcWrite(PIN_PWM_OUT, (int)(duty * 255.0f));
}


int g_time_y = 2026;
int g_time_mo = 1;
int g_time_d = 1;
int g_time_h = 0;
int g_time_m = 0;
int g_time_s = 0;
bool g_time_valid = false;
uint32_t g_time_set_ms = 0;
uint32_t g_time_tick_ms = 0;

bool g_clock_enabled = false;
int g_clock_x = 80;
int g_clock_y = 20;
int g_clock_size = 2;
uint16_t g_clock_color = 0xFFFF;

static void time_tick() {
  if (!g_time_valid) return;
  uint32_t now = millis();
  if (now - g_time_tick_ms < 1000) return;
  g_time_tick_ms += 1000;
  g_time_s++;
  if (g_time_s >= 60) {
    g_time_s = 0;
    g_time_m++;
    if (g_time_m >= 60) {
      g_time_m = 0;
      g_time_h++;
      if (g_time_h >= 24) {
        g_time_h = 0;
        g_time_d++;
        static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int md = dim[g_time_mo - 1];
        if (g_time_mo == 2 && ((g_time_y % 4 == 0 && g_time_y % 100 != 0) || g_time_y % 400 == 0)) md = 29;
        if (g_time_d > md) {
          g_time_d = 1;
          g_time_mo++;
          if (g_time_mo > 12) {
            g_time_mo = 1;
            g_time_y++;
          }
        }
      }
    }
  }
}

static char serial_buf[2048];
static int serial_len = 0;
static volatile bool bg_ready = false;

#define BG_FILE_PATH "/bg.bin"
static bool g_fs_ok = false;


void esp_send_raw(const char* s) {
  Serial.print(s);
  if (ws_client != 0xFF)
    webSocket.sendTXT(ws_client, s);
}

void esp_printf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
  Serial.write((const uint8_t*)buf, n);
  if (ws_client != 0xFF)
    webSocket.sendTXT(ws_client, buf, n);
}


static void wifi_cfg_load() {
  if (!g_fs_ok || !FFat.exists(WIFI_CFG_PATH)) return;
  File f = FFat.open(WIFI_CFG_PATH, "r");
  if (!f) return;
  String s = f.readStringUntil('\n');
  f.close();
  int sep = s.indexOf('|');
  if (sep < 0) return;
  String ssid = s.substring(0, sep);
  ssid.trim();
  String pwd = s.substring(sep + 1);
  pwd.trim();
  strncpy(wifi_ssid, ssid.c_str(), sizeof(wifi_ssid) - 1);
  strncpy(wifi_pwd, pwd.c_str(), sizeof(wifi_pwd) - 1);
}

static bool wifi_cfg_save() {
  if (!g_fs_ok) return false;
  if (FFat.exists(WIFI_CFG_PATH)) FFat.remove(WIFI_CFG_PATH);
  File f = FFat.open(WIFI_CFG_PATH, "w");
  if (!f) return false;
  f.printf("%s|%s\n", wifi_ssid, wifi_pwd);
  f.close();
  return true;
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len);

static void wifi_begin() {
  if (!wifi_ssid[0]) {
    wifi_state = 0;
    wifi_enabled = false;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pwd);
  wifi_connect_start = millis();
  wifi_state = 1;
  wifi_enabled = true;
  esp_printf("[WIFI] connecting to %s\n", wifi_ssid);
}

static void wifi_poll() {
  if (wifi_state == 1) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_state = 2;
      wifi_connected = true;
      IPAddress ip = WiFi.localIP();
      snprintf(wifi_ip, sizeof(wifi_ip), "%d.%d.%d.%d",
               ip[0], ip[1], ip[2], ip[3]);
      esp_printf("[WIFI] connected IP=%s\n", wifi_ip);
      webSocket.begin();
      webSocket.onEvent(webSocketEvent);
      ws_server_started = true;
      esp_printf("[WS] server on :81\n");
    } else if (millis() - wifi_connect_start > 15000) {
      wifi_state = 3;
      wifi_connected = false;
      esp_printf("[WIFI] connect timeout\n");
      WiFi.disconnect(true);
    }
  } else if (wifi_state == 2) {
    if (WiFi.status() != WL_CONNECTED) {
      wifi_state = 0;
      wifi_connected = false;
      if (ws_server_started) {
        webSocket.close();
        ws_server_started = false;
      }
      ws_client = 0xFF;
      bin_cleanup();
      esp_printf("[WIFI] disconnected\n");
    }
  }
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      if (ws_client == 0xFF) {
        ws_client = num;
        esp_printf("[WS] client %u connected\n", num);
        webSocket.sendTXT(num, "HELLO\n");
      } else if (ws_client != num) {

        webSocket.sendTXT(num, "BUSY\n");
        webSocket.disconnect(num);
      }
      break;

    case WStype_DISCONNECTED:
      if (num == ws_client) {
        esp_printf("[WS] client %u disconnected\n", num);
        ws_client = 0xFF;
        bin_cleanup();
      }
      break;

    case WStype_TEXT:
      {

        static char tmp[2048];
        size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
        memcpy(tmp, payload, n);
        tmp[n] = 0;
        char* p = tmp;
        while (*p) {
          char* nl = strchr(p, '\n');
          if (nl) *nl = 0;
          if (strlen(p) > 0) handle_serial_line(p);
          if (!nl) break;
          p = nl + 1;
        }
        break;
      }

    case WStype_BIN:
      if (_pending.kind != BIN_NONE) {
        uint32_t take = _pending.total - _pending.got;
        if (take > len) take = len;
        if (take > 0) {
          memcpy(_pending.buf + _pending.got, payload, take);
          _pending.got += take;
          _pending.last_ms = millis();
        }
      }
      break;

    case WStype_PING:
    case WStype_PONG:
    default: break;
  }
}


static void fs_print_info() {
  if (!g_fs_ok) return;
  esp_printf("[FS] total=%u, used=%u\n",
             (unsigned)FFat.totalBytes(), (unsigned)FFat.usedBytes());
}

static bool bg_load_from_fs() {
  if (!g_fs_ok) return false;
  if (!FFat.exists(BG_FILE_PATH)) return false;
  File f = FFat.open(BG_FILE_PATH, "r");
  if (!f) return false;
  const size_t expect = (size_t)SCREEN_W * SCREEN_H * 2;
  if (f.size() != expect) {
    f.close();
    return false;
  }
  size_t r = f.read((uint8_t*)bgSprite.getBuffer(), expect);
  f.close();
  return (r == expect);
}

static bool bg_save_to_fs(const uint8_t* data, size_t len) {
  if (!g_fs_ok) return false;
  if (FFat.exists(BG_FILE_PATH)) FFat.remove(BG_FILE_PATH);
  File f = FFat.open(BG_FILE_PATH, "w");
  if (!f) return false;
  size_t w = f.write(data, len);
  f.close();
  return (w == len);
}




static void draw_clock(LGFX_Sprite* c, const char* str,
                       int x0, int y0, int sizeLevel, uint16_t color) {
  const lgfx::IFont* font;
  switch (sizeLevel) {
    case 1: font = &lgfx::fonts::DejaVu18; break;
    case 2: font = &lgfx::fonts::DejaVu24; break;
    case 3: font = &lgfx::fonts::DejaVu40; break;
    case 4: font = &lgfx::fonts::DejaVu56; break;
    default: font = &lgfx::fonts::DejaVu24; break;
  }
  c->setFont(font);
  c->setTextSize(1);
  c->setTextColor(color);
  c->drawString(str, x0, y0);
}


static void handle_serial_line(const char* line) {
  if (strncmp(line, "TIME:", 5) == 0) {
    int y, mo, d, h, mi, s;
    if (sscanf(line + 5, "%d|%d|%d|%d|%d|%d", &y, &mo, &d, &h, &mi, &s) == 6) {
      g_time_y = y;
      g_time_mo = mo;
      g_time_d = d;
      g_time_h = h;
      g_time_m = mi;
      g_time_s = s;
      g_time_valid = true;
      g_time_set_ms = millis();
      g_time_tick_ms = millis();
    }
    return;
  }
  if (strncmp(line, "CLOCK_SET:", 10) == 0) {
    int x, y, sz, r, g, b;
    if (sscanf(line + 10, "%d|%d|%d|%d|%d|%d", &x, &y, &sz, &r, &g, &b) == 6) {
      g_clock_x = x;
      g_clock_y = y;
      g_clock_size = sz < 1 ? 1 : (sz > 4 ? 4 : sz);
      g_clock_color = rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
      esp_printf("[CLOCK] pos=%d,%d size=%d color=%04X\n",
                 g_clock_x, g_clock_y, g_clock_size, g_clock_color);
      if (state == ST_HOME) {
        extern int _home_sec_cache;
        _home_sec_cache = -1;
      }
    }
    return;
  }
  if (strncmp(line, "CLOCK_EN:", 9) == 0) {
    g_clock_enabled = atoi(line + 9) != 0;
    esp_printf("[CLOCK] enabled=%d\n", g_clock_enabled);
    if (state == ST_HOME) {
      extern int _home_sec_cache;
      _home_sec_cache = -1;
    }
    return;
  }
  if (strncmp(line, "MON:", 4) == 0) {
    const char* p = line + 4;
    const char* sep = strchr(p, ':');
    if (sep) {
      char type[8] = { 0 };
      int tlen = sep - p;
      if (tlen > 7) tlen = 7;
      memcpy(type, p, tlen);
      monitor_set_type_by_name(type);
      monitor_set_cpu(atof(sep + 1));
    }
    return;
  }
  if (strncmp(line, "CPU:", 4) == 0) {
    monitor_set_cpu(atof(line + 4));
    return;
  }


  if (strncmp(line, "BEGIN:", 6) == 0) {
    unsigned long id = 0, total = 0, chunk = 0;
    char tag[16] = { 0 };
    if (sscanf(line + 6, "%lu:%15[^:]:%lu:%lu", &id, tag, &total, &chunk) >= 3) {
      bin_cleanup();
      if (total == 0 || total > 2 * 1024 * 1024) {
        esp_printf("NAK:%lu:0\n", id);
        return;
      }
      _pending.buf = (uint8_t*)ps_malloc(total);
      if (!_pending.buf) {
        esp_printf("NAK:%lu:0\n", id);
        return;
      }
      _pending.total = total;
      _pending.got = 0;
      _pending.id = id;
      _pending.last_ms = millis();
      if (strcmp(tag, "BG") == 0) _pending.kind = BIN_BG;
      else if (strcmp(tag, "COVER") == 0) _pending.kind = BIN_COVER;
      else {
        bin_cleanup();
        esp_printf("NAK:%lu:0\n", id);
        return;
      }
      esp_printf("BEGIN_OK:%lu\n", id);
    } else {
      esp_printf("NAK:0:parse\n");
    }
    return;
  }
  if (strncmp(line, "CHUNK:", 6) == 0) {
    unsigned long id = 0, seq = 0;
    if (sscanf(line + 6, "%lu:%lu", &id, &seq) == 2
        && _pending.kind != BIN_NONE && id == _pending.id) {
      _pending.last_ms = millis();
      esp_printf("ACK:%lu:%lu\n", id, seq);
    } else {
      esp_printf("NAK:%lu:%lu\n", id, seq);
    }
    return;
  }
  if (strncmp(line, "END:", 4) == 0) {
    unsigned long id = strtoul(line + 4, nullptr, 10);
    if (_pending.kind != BIN_NONE && id == _pending.id) {
      if (_pending.got == _pending.total) {
        if (_pending.kind == BIN_BG) {
          memcpy(bgSprite.getBuffer(), _pending.buf, _pending.total);
          bg_save_to_fs(_pending.buf, _pending.total);
          bg_ready = true;
        } else if (_pending.kind == BIN_COVER) {
          music_set_cover(_pending.buf, _pending.total);
        }
        esp_printf("DONE:%lu\n", id);
      } else {
        esp_printf("NAK:%lu:%u\n", id, (unsigned)_pending.got);
      }
      bin_cleanup();
    } else {
      esp_printf("NAK:%lu:state\n", id);
    }
    return;
  }

  if (strncmp(line, "WEA:", 4) == 0) {
    esp_printf("[WEA] raw(%d): %s\n", (int)strlen(line), line);

    char tmp[256];
    strncpy(tmp, line + 4, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    char* fields[17] = { 0 };
    int nf = 0;
    char* p = strtok(tmp, "|");
    while (p && nf < 17) {
      fields[nf++] = p;
      p = strtok(NULL, "|");
    }

    if (nf >= 8) {
      const char* city = fields[0];
      int temp = atoi(fields[1]);
      int feels = atoi(fields[2]);
      int hum = atoi(fields[3]);
      int wind = atoi(fields[4]);
      int code = atoi(fields[5]);
      int tmax = atoi(fields[6]);
      int tmin = atoi(fields[7]);
      int d1c = nf > 8 ? atoi(fields[8]) : 0;
      int d1max = nf > 9 ? atoi(fields[9]) : 0;
      int d1min = nf > 10 ? atoi(fields[10]) : 0;
      int d2c = nf > 11 ? atoi(fields[11]) : 0;
      int d2max = nf > 12 ? atoi(fields[12]) : 0;
      int d2min = nf > 13 ? atoi(fields[13]) : 0;
      int d3c = nf > 14 ? atoi(fields[14]) : 0;
      int d3max = nf > 15 ? atoi(fields[15]) : 0;
      int d3min = nf > 16 ? atoi(fields[16]) : 0;

      weather_set_data(city, temp, feels, hum, wind, code, tmax, tmin,
                       d1c, d1max, d1min, d2c, d2max, d2min,
                       d3c, d3max, d3min);
      esp_printf("[WEA] applied: %s %dC code=%d (fields=%d)\n",
                 city, temp, code, nf);
      if (state == ST_APP && menuSel == 3) weather_loop(&canvas);
    } else {
      esp_printf("[WEA] too few fields: %d\n", nf);
    }
    return;
  }

  if (strlen(line) < 80) esp_printf("[RX] %s\n", line);

  if (strncmp(line, "MUS_META:", 9) == 0) {
    const char* p = line + 9;
    const char* sep = strchr(p, '|');
    if (sep) {
      char title[1024] = { 0 };
      char artist[1024] = { 0 };
      int tlen = sep - p;
      if (tlen > 1023) tlen = 1023;
      memcpy(title, p, tlen);
      strncpy(artist, sep + 1, 1023);
      music_set_meta(title, artist);
      esp_printf("[MUS] meta ok t=%d a=%d\n", tlen, (int)strlen(artist));
    }
  } else if (strncmp(line, "MUS_PROG:", 9) == 0) {
    float cur = 0, dur = 0;
    char st = 'X';
    sscanf(line + 9, "%f|%f|%c", &cur, &dur, &st);
    music_set_progress(cur, dur, st);
  } else if (strncmp(line, "MUS_COVER:", 10) == 0) {
    if (ws_client != 0xFF) {
      esp_printf("[MUS_COVER] ignored: use BEGIN/END via WS\n");
    } else {
      int length = 0, r = 0, g = 0, b = 0;
      int n = sscanf(line + 10, "%d|%d|%d|%d", &length, &r, &g, &b);
      if (n >= 1 && length > 0 && length <= 300000) {
        if (n >= 4) music_set_theme((uint8_t)r, (uint8_t)g, (uint8_t)b);
        bin_cleanup();
        _pending.buf = (uint8_t*)ps_malloc(length);
        if (!_pending.buf) {
          esp_printf("NAK:0:nomem\n");
        } else {
          _pending.kind = BIN_COVER;
          _pending.total = (uint32_t)length;
          _pending.got = 0;
          _pending.id = 0xC0FFEE;
          _pending.last_ms = millis();
          esp_printf("BEGIN_OK:%lu\n", (unsigned long)_pending.id);
        }
      } else {
        esp_printf("[MUS_COVER] invalid len=%d\n", length);
      }
    }
  } else if (strncmp(line, "BG_IMAGE:", 9) == 0) {
    if (ws_client != 0xFF) {
      esp_printf("[BG_IMAGE] ignored: use BEGIN/END via WS\n");
    } else {
      int length = atoi(line + 9);
      const int expect = SCREEN_W * SCREEN_H * 2;
      if (length == expect) {
        bin_cleanup();
        _pending.buf = (uint8_t*)ps_malloc(length);
        if (!_pending.buf) {
          esp_printf("NAK:0:nomem\n");
        } else {
          _pending.kind = BIN_BG;
          _pending.total = (uint32_t)length;
          _pending.got = 0;
          _pending.id = 0xB6B6;
          _pending.last_ms = millis();
          esp_printf("BEGIN_OK:%lu\n", (unsigned long)_pending.id);
        }
      } else {
        esp_printf("[BG_IMAGE] bad length %d != %d\n", length, expect);
      }
    }
  } else if (strncmp(line, "HWD_SET:", 8) == 0) {
    int l = 0, v = 0;
    float m = 4.8f;
    sscanf(line + 8, "%d|%d|%f", &l, &v, &m);
    hwd_led_on = (l != 0);
    hwd_volt_on = (v != 0);
    hwd_max_volt = m;
    if (hwd_max_volt > 4.8f) hwd_max_volt = 4.8f;
    if (hwd_max_volt < 0.0f) hwd_max_volt = 0.0f;
    esp_printf("[HWD] LED=%d VOLT=%d MAXV=%.2f\n",
               hwd_led_on, hwd_volt_on, hwd_max_volt);
    update_pwm(current_mon_val);
    if (state != ST_APP || menuSel != 1) neopixelWrite(PIN_WS2812, 0, 0, 0);
    else update_led(current_mon_val);
  } else if (strncmp(line, "BG_RESET", 8) == 0) {
    if (g_fs_ok && FFat.exists(BG_FILE_PATH)) FFat.remove(BG_FILE_PATH);
    generateBackground(&bgSprite);
    precomputeMenuBg();
    bg_ready = true;
    esp_printf("[BG] reset to default\n");
  } else if (strncmp(line, "BG_DUMP", 7) == 0) {
    fs_print_info();
  } else if (strncmp(line, "WIFI_CFG:", 9) == 0) {
    const char* p = line + 9;
    const char* sep = strchr(p, '|');
    if (sep) {
      int slen = sep - p;
      if (slen > (int)sizeof(wifi_ssid) - 1) slen = sizeof(wifi_ssid) - 1;
      strncpy(wifi_ssid, p, slen);
      wifi_ssid[slen] = 0;
      strncpy(wifi_pwd, sep + 1, sizeof(wifi_pwd) - 1);
      wifi_cfg_save();
      esp_printf("[WIFI] cfg saved ssid=%s\n", wifi_ssid);
      if (ws_server_started) {
        webSocket.close();
        ws_server_started = false;
      }
      ws_client = 0xFF;
      WiFi.disconnect(true);
      wifi_state = 0;
      wifi_enabled = false;
      wifi_connected = false;
      wifi_begin();
    }
    return;
  } else if (strncmp(line, "WIFI_OFF", 8) == 0) {
    if (ws_server_started) {
      webSocket.close();
      ws_server_started = false;
    }
    ws_client = 0xFF;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_state = 0;
    wifi_enabled = false;
    wifi_connected = false;
    esp_printf("[WIFI] off\n");
    return;
  } else if (strncmp(line, "IP_QUERY", 8) == 0) {
    esp_printf("IP:%s\n", wifi_ip[0] ? wifi_ip : "0.0.0.0");
    return;
  }
}
static void serial_poll() {

  if (_pending.kind != BIN_NONE) {
    int avail = Serial.available();
    if (avail > 0) {
      uint32_t remain = _pending.total - _pending.got;
      int take = ((uint32_t)avail < remain) ? avail : (int)remain;
      int r = Serial.read(_pending.buf + _pending.got, take);
      if (r > 0) {
        _pending.got += r;
        _pending.last_ms = millis();
      }
    }
    if (_pending.got >= _pending.total) {
      if (_pending.kind == BIN_BG) {
        const size_t expect = (size_t)SCREEN_W * SCREEN_H * 2;
        if (_pending.total == expect) {
          memcpy(bgSprite.getBuffer(), _pending.buf, _pending.total);
          bg_save_to_fs(_pending.buf, _pending.total);
          bg_ready = true;
        }
      } else if (_pending.kind == BIN_COVER) {
        music_set_cover(_pending.buf, _pending.total);
      }
      esp_printf("DONE:%lu\n", (unsigned long)_pending.id);
      bin_cleanup();
    } else if (millis() - _pending.last_ms > _pending.timeout_ms) {
      esp_printf("NAK:%lu:timeout\n", (unsigned long)_pending.id);
      bin_cleanup();
    }
    return;
  }


  while (Serial.available()) {
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\n') {
      serial_buf[serial_len] = 0;
      if (serial_len > 0) handle_serial_line(serial_buf);
      serial_len = 0;
      if (_pending.kind != BIN_NONE) break;
    } else if (ch == '\r') {
    } else if (serial_len < (int)sizeof(serial_buf) - 1) {
      serial_buf[serial_len++] = (char)ch;
    }
  }
  if (serial_len > (int)sizeof(serial_buf) - 2) serial_len = 0;
}

static int menuMod(int v) {
  int m = v % MENU_COUNT;
  return (m < 0) ? (m + MENU_COUNT) : m;
}

int _home_sec_cache = -1;

static void draw_home() {
  memcpy(canvas.getBuffer(), bgSprite.getBuffer(),
         (size_t)SCREEN_W * SCREEN_H * 2);

  if (g_clock_enabled && g_time_valid) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", g_time_h, g_time_m);
    draw_clock(&canvas, buf, g_clock_x + 1, g_clock_y + 1,
               g_clock_size, rgb565(20, 20, 30));
    draw_clock(&canvas, buf, g_clock_x, g_clock_y,
               g_clock_size, g_clock_color);
  }
  push_with_fps(&canvas);
}


void setup() {
  Serial.setRxBufferSize(32768);
  Serial.begin(256000);
  delay(500);

  g_fs_ok = FFat.begin(true, "/ffat", 10, "ffat")
            || FFat.begin(true, "/ffat", 10, "spiffs")
            || FFat.begin(true);

  wifi_cfg_load();

  lcd.init();
  lcd.setRotation(2);
  lcd.fillScreen(TFT_BLACK);

  auto must_create = [](LGFX_Sprite& s) {
    s.setPsram(true);
    s.setColorDepth(16);
    if (!s.createSprite(SCREEN_W, SCREEN_H)) {
      while (1) delay(1000);
    }
  };
  must_create(bgSprite);
  must_create(menuBgSprite);
  must_create(canvas);
  must_create(menuSprite);
  must_create(appSprite);

  if (!bg_load_from_fs()) generateBackground(&bgSprite);
  precomputeMenuBg();
  renderMenuToSprite(0.0f);

  hwd_init();
  encoder_init();
  buttons_init();

  if (wifi_ssid[0]) wifi_begin();

  _home_sec_cache = -1;
  draw_home();
  hb_last_ms = millis();
}

void loop() {

  if (ws_server_started) webSocket.loop();


  if (_pending.kind != BIN_NONE
      && millis() - _pending.last_ms > _pending.timeout_ms) {
    esp_printf("NAK:%lu:timeout\n", (unsigned)_pending.id);
    bin_cleanup();
  }

  serial_poll();
  wifi_poll();
  time_tick();
  monitor_hw_sync();

  if (bg_ready) {
    bg_ready = false;
    precomputeMenuBg();
    if (state == ST_HOME) draw_home();
    else if (state == ST_MENU) {
      renderMenuToSprite(menuPos);
      memcpy(canvas.getBuffer(), menuSprite.getBuffer(),
             (size_t)SCREEN_W * SCREEN_H * 2);
      push_with_fps(&canvas);
    }
  }

  buttons_poll();

  if (millis() - hb_last_ms > 3000) {
    hb_last_ms = millis();
    esp_send_raw("PING\n");
  }

  if (state == ST_HOME && g_clock_enabled && g_time_valid) {
    int sec = g_time_h * 3600 + g_time_m * 60 + g_time_s;
    if (sec != _home_sec_cache) {
      _home_sec_cache = sec;
      draw_home();
    }
  }

  EncoderEvent evt = encoder_read();

  if (state == ST_LAUNCHING) {
    if (launchFrameIdx >= FADE_FRAMES) {
      state = ST_APP;
      switch (menuSel) {
        case 0: music_setup(&canvas); break;
        case 1: monitor_setup(&canvas); break;
        case 2: calendar_setup(&canvas); break;
        case 3: weather_setup(&canvas); break;
        case 4: settings_setup(&canvas); break;
      }
    } else {
      drawLaunchFade(&canvas, launchFrameIdx);
      launchFrameIdx++;
      delay(15);
    }
    return;
  }

  if (menuDir != 0) {
    menuT += menuDir * 0.09f;
    if (menuT > 1.0f) {
      menuT = 1.0f;
      menuDir = 0;
    }
    if (menuT < 0.0f) {
      menuT = 0.0f;
      menuDir = 0;
    }
    if (menuDir == 0 && menuT <= 0.0f) {
      state = ST_HOME;
      _home_sec_cache = -1;
      draw_home();
    } else drawMenu(&canvas, menuT);
    delay(5);
    return;
  }

  switch (state) {
    case ST_HOME:
      if (evt == ENC_BACK) {
        blend_set_src(&bgSprite);
        state = ST_MENU;
        menuSel = 0;
        menuPos = 0.0f;
        menuTargetPos = 0.0f;
        menuT = 0.0f;
        menuDir = 1;
        renderMenuToSprite(menuPos);
      }
      break;

    case ST_MENU:
      {
        if (evt == ENC_LEFT) menuTargetPos -= 1.0f;
        else if (evt == ENC_RIGHT) menuTargetPos += 1.0f;
        else if (evt == ENC_PRESS) {
          menuSel = menuMod((int)roundf(menuPos));
          switch (menuSel) {
            case 0: music_draw(&appSprite); break;
            case 1: monitor_draw_gauge(&appSprite, 0.0f); break;
            case 2: calendar_preview(&appSprite); break;
            case 3: weather_draw(&appSprite); break;
            case 4: settings_draw(&appSprite); break;
          }
          memcpy(canvas.getBuffer(), menuSprite.getBuffer(),
                 (size_t)SCREEN_W * SCREEN_H * 2);
          canvas.pushSprite(0, 0);
          state = ST_LAUNCHING;
          launchFrameIdx = 0;
          break;
        } else if (evt == ENC_BACK) {
          blend_set_src(&bgSprite);
          menuDir = -1;
          break;
        }
        if (menuTargetPos < menuPos - MENU_MAX_LEAD) menuTargetPos = menuPos - MENU_MAX_LEAD;
        if (menuTargetPos > menuPos + MENU_MAX_LEAD) menuTargetPos = menuPos + MENU_MAX_LEAD;
        float diff = menuTargetPos - menuPos;
        if (fabsf(diff) > 0.001f) {
          menuPos += diff * MENU_INTERP;
          if (fabsf(menuTargetPos - menuPos) < 0.02f) menuPos = menuTargetPos;
          renderMenuToSprite(menuPos);
          memcpy(canvas.getBuffer(), menuSprite.getBuffer(),
                 (size_t)SCREEN_W * SCREEN_H * 2);
          push_with_fps(&canvas);
        }
        break;
      }

    case ST_APP:
      if (evt == ENC_BACK) {
        switch (menuSel) {
          case 0: music_exit(); break;
          case 1: monitor_exit(); break;
          case 2: calendar_exit(); break;
          case 3: weather_exit(); break;
          case 4: settings_exit(); break;
        }
        memcpy(appSprite.getBuffer(), canvas.getBuffer(),
               (size_t)SCREEN_W * SCREEN_H * 2);
        blend_set_src(&appSprite);
        state = ST_MENU;
        menuT = 0.0f;
        menuDir = 1;
        menuSel = menuMod((int)roundf(menuPos));
        menuPos = menuTargetPos = (float)menuSel;
        renderMenuToSprite(menuPos);
      } else {
        switch (menuSel) {
          case 0:
            if (evt == ENC_PRESS) esp_send_raw("MUS_PLAY\n");
            else if (evt == ENC_LEFT) esp_send_raw("MUS_PREV\n");
            else if (evt == ENC_RIGHT) esp_send_raw("MUS_NEXT\n");
            music_loop(&canvas);
            break;
          case 1: monitor_loop(&canvas); break;
          case 2:
            calendar_on_encoder(evt);
            calendar_loop(&canvas);
            break;
          case 3: weather_loop(&canvas); break;
          case 4: settings_loop(&canvas); break;
        }
      }
      break;
    default: break;
  }
  delay(5);
}