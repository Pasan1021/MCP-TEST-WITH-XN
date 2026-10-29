// NORVI X1 + NORVI X-TC4 (K-type thermocouple) - temperature monitor
//
//  * Reads thermocouple channel CONTROL_CH over I2C once per second
//  * Temperature > 35 C  -> ALARM ON  (alarm output + onboard LED2, solid)
//  * Temperature < 20 C  -> onboard LED1 BLINKS
//  * 20..35 C            -> NORMAL (everything off)
//  * Sensor fault / no reply from X-TC4 -> alarm ON (fail-safe) and "SENSOR FAULT" shown
//  * Shows temperature and status on the X1 TFT display and on Serial (115200)
//
// Alarm output: ALARM_PIN (GPIO5 = "IO5" on the expansion bus, active HIGH) drives
// e.g. an X-Q4 transistor module, buzzer driver or relay input. LED2 (onboard
// "Error" LED) also lights while the alarm is active. Change ALARM_PIN / ALARM_ACTIVE
// below to suit your wiring.
//
// X-TC4 I2C address (DIP switches): default 0x18.
// Board settings: ESP32S3 Dev Module, USB CDC On Boot = Enabled.

#include <Wire.h>
#include <SPI.h>
#include <PCA9536D.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ===================== User settings =====================
#define TC_SLAVE_ADDR      0x18     // X-TC4 I2C address
#define CONTROL_CH         1        // thermocouple channel used for alarm / LED (1..4)

#define TEMP_HIGH_C        35.0f    // above this -> alarm ON
#define TEMP_LOW_C         20.0f    // below this -> LED blinks
#define HYSTERESIS_C       1.0f     // avoids chattering around the limits

#define ALARM_PIN          5        // expansion-bus IO5, direct GPIO
#define ALARM_ACTIVE       HIGH     // level that turns the alarm ON
#define ALARM_ON_SENSOR_FAULT 1     // 1 = alarm if thermocouple fault / module not answering

#define BLINK_INTERVAL_MS  500
#define READ_INTERVAL_MS   1000

// LED polarity on the onboard PCA9536 LEDs (LOW = on, as in the NORVI X1 example).
// If your LEDs look inverted, swap these two.
#define LED_ON             LOW
#define LED_OFF            HIGH
// =========================================================

// ---------------- Pins ----------------
#define I2C_SDA_PIN   8
#define I2C_SCL_PIN   9
#define SPI_MISO_PIN  13
#define SPI_MOSI_PIN  11
#define SPI_SCLK_PIN  12
#define PORT_RST_PIN  38             // IO-expander reset: HIGH = enabled

#define IO_LED1       1              // PCA9536 pin: Run LED
#define IO_LED2       2              // PCA9536 pin: Error LED

// ---------------- Display (LovyanGFX, as in the NORVI X1 example) ----------------
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

static const uint16_t COL_BLACK   = 0x0000;
static const uint16_t COL_WHITE   = 0xFFFF;
static const uint16_t COL_RED     = 0xF800;
static const uint16_t COL_GREEN   = 0x07E0;
static const uint16_t COL_YELLOW  = 0xFFE0;
static const uint16_t COL_CYAN    = 0x07FF;
static const uint16_t COL_MAGENTA = 0xF81F;

PCA9536 io;
bool ioReady = false;

// ---------------- State ----------------
enum Status { ST_NORMAL, ST_HIGH_ALARM, ST_LOW_BLINK, ST_FAULT };
Status status = ST_NORMAL;

float   temperature = 0;
uint8_t faultCode   = 0;
bool    haveReading = false;     // a valid reply was received this cycle
uint8_t failCycles  = 0;
uint8_t i2cFailCount = 0;

bool blinkState = false;
unsigned long lastBlink = 0;
unsigned long lastRead  = 0;

// display cache to avoid flicker
String cacheTemp = "";
String cacheStatus = "";
bool   fullRedraw = true;

// ---------------- CRC8 (poly 0x8C) ----------------
uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0x00;
  while (len--) {
    uint8_t extract = *data++;
    for (uint8_t i = 8; i; i--) {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) crc ^= 0x8C;
      extract >>= 1;
    }
  }
  return crc;
}

// ---------------- I2C bus recovery ----------------
void i2cBusRecover() {
  Serial.println("Recovering I2C bus...");
  Wire.end();
  pinMode(I2C_SCL_PIN, OUTPUT);
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {
    digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
    if (digitalRead(I2C_SDA_PIN) == HIGH) break;
  }
  pinMode(I2C_SDA_PIN, OUTPUT);
  digitalWrite(I2C_SDA_PIN, LOW);  delayMicroseconds(5);
  digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
  digitalWrite(I2C_SDA_PIN, HIGH); delayMicroseconds(5);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
}

// ---------------- Read the thermocouple ----------------
// Request [0x01, ch]; reply 8 bytes: [1]=channel [2..5]=float temp (LE) [6]=fault [7]=CRC8(0..6)
bool readThermocouple() {
  Wire.beginTransmission(TC_SLAVE_ADDR);
  Wire.write((uint8_t)0x01);
  Wire.write((uint8_t)CONTROL_CH);
  if (Wire.endTransmission() != 0) {
    Serial.println("I2C TX FAIL (no ACK from X-TC4)");
    if (++i2cFailCount >= 5) { i2cBusRecover(); i2cFailCount = 0; }
    return false;
  }
  i2cFailCount = 0;

  delay(15);

  Wire.requestFrom((uint8_t)TC_SLAVE_ADDR, (uint8_t)8);
  if (Wire.available() != 8) {
    Serial.println("I2C RX FAIL");
    while (Wire.available()) Wire.read();
    return false;
  }

  uint8_t buf[8];
  for (int i = 0; i < 8; i++) buf[i] = Wire.read();

  if (crc8(buf, 7) != buf[7]) { Serial.println("CRC ERROR"); return false; }
  if (buf[1] != CONTROL_CH)   { return false; }

  memcpy(&temperature, &buf[2], 4);
  faultCode = buf[6];
  return true;
}

// ---------------- Status logic (with hysteresis) ----------------
Status evaluateStatus(Status prev) {
  if (failCycles >= 3 || faultCode != 0 || isnan(temperature)) return ST_FAULT;

  switch (prev) {
    case ST_HIGH_ALARM:   // stay in alarm until temp drops below (high - hysteresis)
      if (temperature > TEMP_HIGH_C - HYSTERESIS_C) return ST_HIGH_ALARM;
      break;
    case ST_LOW_BLINK:    // stay in low state until temp rises above (low + hysteresis)
      if (temperature < TEMP_LOW_C + HYSTERESIS_C) return ST_LOW_BLINK;
      break;
    default:
      break;
  }

  if (temperature > TEMP_HIGH_C) return ST_HIGH_ALARM;
  if (temperature < TEMP_LOW_C)  return ST_LOW_BLINK;
  return ST_NORMAL;
}

const char *statusText(Status s) {
  switch (s) {
    case ST_HIGH_ALARM: return "HIGH - ALARM";
    case ST_LOW_BLINK:  return "LOW TEMP";
    case ST_FAULT:      return "SENSOR FAULT";
    default:            return "NORMAL";
  }
}

uint16_t statusColor(Status s) {
  switch (s) {
    case ST_HIGH_ALARM: return COL_RED;
    case ST_LOW_BLINK:  return COL_CYAN;
    case ST_FAULT:      return COL_MAGENTA;
    default:            return COL_GREEN;
  }
}

// ---------------- Outputs ----------------
void setAlarm(bool on) {
  digitalWrite(ALARM_PIN, on ? ALARM_ACTIVE : !ALARM_ACTIVE);
  if (ioReady) io.digitalWrite(IO_LED2, on ? LED_ON : LED_OFF);   // onboard Error LED
}

void applyOutputs() {
  bool alarm = (status == ST_HIGH_ALARM) || (ALARM_ON_SENSOR_FAULT && status == ST_FAULT);
  setAlarm(alarm);

  // LED1 is driven by updateBlink() while in the low-temperature state, off otherwise
  if (ioReady && status != ST_LOW_BLINK) io.digitalWrite(IO_LED1, LED_OFF);
}

void updateBlink() {
  if (!ioReady || status != ST_LOW_BLINK) return;
  unsigned long now = millis();
  if (now - lastBlink >= BLINK_INTERVAL_MS) {
    lastBlink = now;
    blinkState = !blinkState;
    io.digitalWrite(IO_LED1, blinkState ? LED_ON : LED_OFF);
  }
}

// ---------------- Display ----------------
void drawStatic() {
  tft.fillScreen(COL_BLACK);
  tft.setTextColor(COL_YELLOW);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setCursor(10, 28);
  tft.print("NORVI TEMP MONITOR");

  tft.setTextColor(COL_WHITE);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setCursor(10, 55);
  tft.printf("Thermocouple Ch %d", CONTROL_CH);

  tft.drawFastHLine(0, 66, 240, COL_WHITE);
  tft.drawFastHLine(0, 240, 240, COL_WHITE);

  tft.setTextColor(COL_WHITE);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setCursor(8, 265);
  tft.printf("Alarm > %.0f C", TEMP_HIGH_C);
  tft.setCursor(8, 290);
  tft.printf("LED blink < %.0f C", TEMP_LOW_C);

  cacheTemp = "";
  cacheStatus = "";
  fullRedraw = false;
}

void drawValues() {
  if (fullRedraw) drawStatic();

  // ---- temperature ----
  String t = (failCycles >= 3) ? String("-- --") : String(temperature, 1);
  if (t != cacheTemp) {
    cacheTemp = t;
    tft.fillRect(0, 70, 240, 90, COL_BLACK);
    tft.setTextColor(statusColor(status));
    tft.setFont(&fonts::FreeSansBold24pt7b);
    tft.setCursor(10, 130);
    tft.print(t);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.print(" C");
  }

  // ---- status ----
  String s = String(statusText(status)) + "|" + String(faultCode) + "|" + String(failCycles >= 3);
  if (s != cacheStatus) {
    cacheStatus = s;
    tft.fillRect(0, 165, 240, 70, COL_BLACK);
    tft.setTextColor(statusColor(status));
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.setCursor(10, 195);
    tft.print(statusText(status));

    tft.setTextColor(COL_WHITE);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setCursor(10, 222);
    if (status == ST_HIGH_ALARM)     tft.print("Alarm output ON");
    else if (status == ST_LOW_BLINK) tft.print("LED blinking");
    else if (status == ST_FAULT)     tft.print(failCycles >= 3 ? "No reply from X-TC4" : "Check thermocouple");
    else                             tft.print("All outputs OFF");
  }
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);
  Serial.println("\nNORVI X1 + X-TC4 temperature monitor");
  Serial.printf("Channel %d, alarm > %.1f C, LED blink < %.1f C, X-TC4 addr 0x%02X\n",
                CONTROL_CH, TEMP_HIGH_C, TEMP_LOW_C, TC_SLAVE_ADDR);

  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, !ALARM_ACTIVE);           // alarm off at start

  pinMode(PORT_RST_PIN, OUTPUT);
  digitalWrite(PORT_RST_PIN, HIGH);                 // enable IO expanders
  delay(100);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  delay(100);

  ioReady = io.begin();
  if (ioReady) {
    io.pinMode(IO_LED1, OUTPUT);
    io.pinMode(IO_LED2, OUTPUT);
    io.digitalWrite(IO_LED1, LED_OFF);
    io.digitalWrite(IO_LED2, LED_OFF);
  } else {
    Serial.println("PCA9536 (onboard LEDs) not detected - LEDs disabled");
  }

  SPI.begin(SPI_SCLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
  delay(100);
  tft.init();
  tft.setRotation(0);
  tft.setTextDatum(lgfx::textdatum_t::baseline_left);
  fullRedraw = true;
  drawValues();
}

void loop() {
  unsigned long now = millis();

  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;

    haveReading = readThermocouple();
    if (haveReading) failCycles = 0;
    else if (failCycles < 255) failCycles++;

    Status newStatus = evaluateStatus(status);
    if (newStatus != status) {
      Serial.printf(">> Status: %s\n", statusText(newStatus));
      if (newStatus == ST_LOW_BLINK) { blinkState = false; lastBlink = now; }
      status = newStatus;
    }

    if (haveReading) Serial.printf("Temp: %.2f C  fault=%u  status=%s\n", temperature, faultCode, statusText(status));
    else Serial.printf("No valid reading (%u)  status=%s\n", failCycles, statusText(status));

    applyOutputs();
    drawValues();
  }

  updateBlink();
  delay(5);
}
