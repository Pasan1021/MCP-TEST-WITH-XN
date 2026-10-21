// NORVI-XN Touch Counter
// Display: ST7789 240x320 (LovyanGFX)  |  Touch: CST816S
// Tap "+" to increment, "-" to decrement, tap the number to reset to 0.
// The count is printed to Serial Monitor at 115200 baud
// (requires USB CDC On Boot: Enabled).

#include <SPI.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <CST816S.h>

// ---------------- LovyanGFX Setup (board-fixed) ----------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 13;
      cfg.pin_dc   = 46;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 45;
      cfg.pin_rst          = 47;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;

// Touch controller: SDA, SCL, RST, IRQ (board-fixed)
CST816S touch(8, 9, 47, 48);

int  counterValue = 0;
bool isTouched = false;

// --- Theme Colors ---
#define THEME_BG          TFT_BLACK
#define THEME_HEADER_BG   tft.color565(75, 0, 130)    // Deep Indigo
#define THEME_UNDERLINE   tft.color565(0, 255, 255)   // Neon Cyan
#define THEME_BTN_MINUS   tft.color565(255, 20, 147)  // Deep Pink
#define THEME_BTN_PLUS    tft.color565(0, 191, 255)   // Deep Sky Blue

// Button geometry
const int btnMinusX = 20,  btnMinusY = 220, btnMinusW = 80, btnMinusH = 60;
const int btnPlusX  = 140, btnPlusY  = 220, btnPlusW  = 80, btnPlusH  = 60;

// Counter area (tap to reset)
const int cntX = 10, cntY = 80, cntW = 220, cntH = 100;

bool inRect(uint16_t x, uint16_t y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

void printCount() {
  Serial.print("Count: ");
  Serial.println(counterValue);
}

void drawHeader() {
  tft.fillRect(0, 0, 240, 50, THEME_HEADER_BG);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("NORVI XN", 120, 25);
  tft.fillRect(0, 50, 240, 4, THEME_UNDERLINE);
}

void drawCounter() {
  tft.fillRect(cntX, cntY, cntW, cntH, THEME_BG);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(6);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawNumber(counterValue, 120, 130);
}

void drawHint() {
  tft.fillRect(0, 185, 240, 25, THEME_BG);
  tft.setTextColor(tft.color565(150, 150, 150));
  tft.setTextSize(1);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("Tap number to reset", 120, 197);
}

void drawButtons() {
  tft.fillRoundRect(btnMinusX, btnMinusY, btnMinusW, btnMinusH, 12, THEME_BTN_MINUS);
  tft.drawRoundRect(btnMinusX, btnMinusY, btnMinusW, btnMinusH, 12, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(4);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("-", btnMinusX + (btnMinusW / 2), btnMinusY + (btnMinusH / 2));

  tft.fillRoundRect(btnPlusX, btnPlusY, btnPlusW, btnPlusH, 12, THEME_BTN_PLUS);
  tft.drawRoundRect(btnPlusX, btnPlusY, btnPlusW, btnPlusH, 12, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(4);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("+", btnPlusX + (btnPlusW / 2), btnPlusY + (btnPlusH / 2));
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. Touch FIRST (shares reset pin GPIO 47 with the display)
  touch.begin();

  // 2. Display SECOND
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(THEME_BG);

  drawHeader();
  drawCounter();
  drawHint();
  drawButtons();

  Serial.println("NORVI-XN touch counter ready");
  printCount();
}

void loop() {
  if (touch.available()) {
    if (!isTouched) {
      isTouched = true;
      uint16_t x = touch.data.x;
      uint16_t y = touch.data.y;

      if (inRect(x, y, btnMinusX, btnMinusY, btnMinusW, btnMinusH)) {
        counterValue--;
        drawCounter();
        printCount();
        tft.fillRoundRect(btnMinusX, btnMinusY, btnMinusW, btnMinusH, 12, TFT_WHITE);
        delay(80);
        drawButtons();
      } else if (inRect(x, y, btnPlusX, btnPlusY, btnPlusW, btnPlusH)) {
        counterValue++;
        drawCounter();
        printCount();
        tft.fillRoundRect(btnPlusX, btnPlusY, btnPlusW, btnPlusH, 12, TFT_WHITE);
        delay(80);
        drawButtons();
      } else if (inRect(x, y, cntX, cntY, cntW, cntH)) {
        counterValue = 0;
        drawCounter();
        printCount();
      }
    }
  } else {
    isTouched = false;
  }

  delay(20);
}
