// NORVI X1 - show "NORVI CONTROLLERS" on the TFT display
// Board settings: ESP32S3 Dev Module, USB CDC On Boot = Enabled.

#include <SPI.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define SPI_MISO_PIN 13
#define SPI_MOSI_PIN 11
#define SPI_SCLK_PIN 12

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
public:
  LGFX(void) {
    { auto c = _bus.config();
      c.spi_host    = SPI2_HOST;
      c.spi_mode    = 0;
      c.freq_write  = 27000000;
      c.freq_read   = 16000000;
      c.spi_3wire   = false;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk    = 12;
      c.pin_mosi    = 11;
      c.pin_miso    = 13;
      c.pin_dc      = 46;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    { auto c = _panel.config();
      c.pin_cs   = 45;
      c.pin_rst  = 47;
      c.pin_busy = -1;
      c.panel_width  = 240;
      c.panel_height = 320;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = 0;
      c.readable   = false;
      c.invert     = true;
      c.rgb_order  = false;
      c.dlen_16bit = false;
      c.bus_shared = true;
      _panel.config(c);
    }
    setPanel(&_panel);
  }
};
LGFX tft;

void setup() {
  Serial.begin(115200);
  SPI.begin(SPI_SCLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
  delay(100);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(lgfx::textdatum_t::middle_center);
  tft.setTextColor(TFT_YELLOW);

  int cx = tft.width() / 2;
  int cy = tft.height() / 2;

  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.drawString("NORVI", cx, cy - 25);

  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.drawString("CONTROLLERS", cx, cy + 25);

  Serial.println("NORVI CONTROLLERS");
}

void loop() {
  delay(1000);
}
