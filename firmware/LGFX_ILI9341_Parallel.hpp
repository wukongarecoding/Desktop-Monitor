#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_Parallel8 _bus_instance;
  lgfx::Panel_ILI9341 _panel_instance;


public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();

      cfg.pin_wr = 12;
      cfg.pin_rd = 16;
      cfg.pin_rs = 13;

      cfg.pin_d0 = 4;
      cfg.pin_d1 = 5;
      cfg.pin_d2 = 6;
      cfg.pin_d3 = 7;
      cfg.pin_d4 = 8;
      cfg.pin_d5 = 9;
      cfg.pin_d6 = 10;
      cfg.pin_d7 = 11;

      cfg.freq_write = 20000000;


      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();

      cfg.pin_cs = 14;
      cfg.pin_rst = 17;
      cfg.pin_busy = -1;

      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }



    setPanel(&_panel_instance);
  }
};