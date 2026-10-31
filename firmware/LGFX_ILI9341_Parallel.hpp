// LGFX_ILI9341_Parallel.hpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Bus_Parallel8 _bus_instance;
  lgfx::Panel_ILI9341 _panel_instance;
  // lgfx::Light_PWM _light_instance; // 如需背光控制再启用

public:
  LGFX(void)
  {
    { // 配置 8 位并行总线
      auto cfg = _bus_instance.config();

      cfg.pin_wr = 12;   // 写选通 WR
      cfg.pin_rd = 16;   // 读选通 RD（你实际接的是 GPIO16，不是 15）
      cfg.pin_rs = 13;   // 数据/命令选择 DC/RS

      cfg.pin_d0 = 4;
      cfg.pin_d1 = 5;
      cfg.pin_d2 = 6;
      cfg.pin_d3 = 7;
      cfg.pin_d4 = 8;
      cfg.pin_d5 = 9;
      cfg.pin_d6 = 10;
      cfg.pin_d7 = 11;

      cfg.freq_write = 20000000; // 先 10MHz 测试，稳定后可提到 20MHz
      // cfg.freq_read  = 10000000; // 可选

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // 配置 ILI9341 面板
      auto cfg = _panel_instance.config();

      cfg.pin_cs  = 14;   // 片选 CS
      cfg.pin_rst = 17;   // 复位 RST（你接的是 GPIO17）
      cfg.pin_busy = -1;  // ILI9341 不需要 BUSY

      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;     // RD 接了 GPIO16，可读
      cfg.invert = false;      // 若颜色发白/反色，改为 true
      cfg.rgb_order = false;   // 若红蓝反了，改为 true
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;  // 并行总线设为 false

      _panel_instance.config(cfg);
    }

    /* 背光控制：如果背光 A 接 3.3V、K 接 GND 常亮，则不需要下面这段
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 17;      // 注意：不能与 RST 共用引脚
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    */

    setPanel(&_panel_instance);
  }
};