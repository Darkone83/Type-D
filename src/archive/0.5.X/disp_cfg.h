#ifndef DISP_CFG_H
#define DISP_CFG_H

#include <LovyanGFX.hpp>

// Backlight pin
#define TFT_BL 32

// --- LovyanGFX GC9A01 Panel (SPI) with PWM Backlight ---
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;  // Use Light_PWM for backlight control

public:
  LGFX(void) {
    // SPI bus setup
    { auto cfg = _bus.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 2000000;
      cfg.spi_3wire = false;
      cfg.use_lock = false;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 15;
      cfg.pin_miso = -1;
      cfg.pin_dc = 27;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    // Panel config
    { auto cfg = _panel.config();
      cfg.pin_cs = 5;
      cfg.pin_rst = 33;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0; cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel.config(cfg);
    }

    setPanel(&_panel);

    // Configure Light_PWM for backlight control
    { auto cfg = _light.config();
      cfg.pin_bl = TFT_BL;
      cfg.invert = false;
      cfg.freq = 5000; // adjust as desired
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
  }
};

// --- Touch config ---
#define TOUCH_SCL 22
#define TOUCH_SDA 21
#define TOUCH_RST 24
#define TOUCH_INT 19

// Display geometry
#define DISP_ROTATION 0
#define DISP_WIDTH 240
#define DISP_HEIGHT 240

// Version
static constexpr char VERSION_TEXT[] = "v0.5.2 Beta";

#endif // DISP_CFG_H
