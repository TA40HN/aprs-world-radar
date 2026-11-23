// LovyanGFX device for the GMT130 module: ST7789, 240x240 IPS, 4-wire SPI without a CS pin.
#pragma once
#include <LovyanGFX.hpp>
#include "config.h"

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  Display() {
    auto b = bus_.config();
    b.spi_host = VSPI_HOST;
    b.spi_mode = 3;  // a CS-less ST7789 only latches data in SPI mode 3
    b.freq_write = 80000000;  // VSPI on its IOMUX pins (18, 23) runs at 80 MHz; halves the 24 ms frame push
    b.freq_read = 16000000;
    b.pin_sclk = PIN_TFT_SCLK;
    b.pin_mosi = PIN_TFT_MOSI;
    b.pin_miso = -1;
    b.pin_dc = PIN_TFT_DC;
    b.dma_channel = SPI_DMA_CH_AUTO;
    bus_.config(b);
    panel_.setBus(&bus_);

    auto p = panel_.config();
    p.pin_cs = -1;
    p.pin_rst = PIN_TFT_RST;
    p.pin_busy = -1;
    p.panel_width = 240;
    p.panel_height = 240;
    p.memory_width = 240;
    p.memory_height = 320;
    p.offset_x = 0;
    p.offset_y = 0;
    p.invert = true;
    p.rgb_order = false;
    p.readable = false;
    p.bus_shared = false;
    panel_.config(p);

    auto l = light_.config();
    l.pin_bl = PIN_TFT_BL;
    l.invert = false;
    l.freq = 12000;
    l.pwm_channel = 7;
    light_.config(l);
    panel_.setLight(&light_);

    setPanel(&panel_);
  }
};
