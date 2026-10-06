/*
  xdrv_95_aipassport_screen.ino - boot screen support for the FoloToy AI Passport

  Copyright (C) 2026  Shinku

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_AIPASSPORT_SCREEN

/*********************************************************************************************\
 * Minimal boot screen for the FoloToy AI Passport (ESP32-C3)
 *
 * While the device has no IP address the built-in panel shows the Wi-Fi setup hint: the
 * access point to join (Tasmota hostname) and the address to open in a browser.  Once the
 * device is connected the panel shows the network name and the address to open.
 *
 * Self contained: the Tasmota display framework is not used and no other behaviour,
 * setting or command is changed.
 *
 * Hardware (from the AI Passport BSP components/bsp):
 *   panel : ST7789P3 240x320, 4-line SPI, mode 0, 80 MHz
 *   pins  : SCLK 8, MOSI 9, CS 1, DC 20, RST not wired (software reset), backlight 21
 *   The vendor init sequence (porch / power / gamma) follows the panel maker's reference
 *   sequence as used by the AI Passport BSP.
\*********************************************************************************************/

#define XDRV_95                 95

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "./tasmota_xdrv_driver/aipassport_cjk_font.h"   // 24x24 1-bit CJK glyphs (see tools/gen_aipassport_cjk_font.py)

#define APS_SCREEN_W            240
#define APS_SCREEN_H            320
#define APS_STRIPE_H            40                    // Drawing strip height; 320 / 40 = 8 slots

#define APS_PIN_SCLK            8
#define APS_PIN_MOSI            9
#define APS_PIN_CS              1
#define APS_PIN_DC              20
#define APS_PIN_BL              21
#define APS_SPI_HZ              (80 * 1000 * 1000)

#define APS_COLOR_BG            0x0861                // Dark blue background
#define APS_COLOR_WHITE         0xFFFF
#define APS_COLOR_GRAY          0xA514
#define APS_COLOR_CYAN          0x07FF

static GFXcanvas16 *aps_canvas = nullptr;
static bool aps_ready = false;
static uint8_t aps_screen = 0xFF;                     // 0xFF = not drawn, 1 = setup hint, 2 = connected
static uint32_t aps_ip = 0;
static char aps_ssid[40] = "";

typedef struct {
  uint8_t  cmd;
  uint8_t  len;
  uint8_t  data[16];
  uint16_t delay_ms;
} aps_init_cmd_t;

// ST7789P3 vendor init sequence (porch / power / gamma) from the panel maker's reference
// routine as used by the AI Passport BSP.  Applies to this exact panel only.
static const aps_init_cmd_t APS_INIT_CMDS[] = {
  {0xB2,  5, {0x05, 0x05, 0x00, 0x33, 0x33}, 0},                        // PORCTRL
  {0xB7,  1, {0x35}, 0},                                                // GCTRL
  {0xBB,  1, {0x21}, 0},                                                // VCOMS
  {0xC0,  1, {0x2C}, 0},                                                // LCMCTRL
  {0xC2,  1, {0x01}, 0},                                                // VDVVRHEN
  {0xC3,  1, {0x0B}, 0},                                                // VRHS
  {0xC4,  1, {0x20}, 0},                                                // VDVSET
  {0xC6,  1, {0x0F}, 0},                                                // FRCTRL2 60 Hz dot inversion
  {0xD0,  2, {0xA7, 0xA1}, 0},                                          // PWCTRL1
  {0xD0,  2, {0xA4, 0xA1}, 0},                                          // PWCTRL1 (reference routine sends it twice)
  {0xD6,  1, {0xA1}, 0},                                                // PWCTRL2
  {0xE0, 14, {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43,
              0x49, 0x09, 0x16, 0x15, 0x26, 0x2B}, 0},                  // PVGAMCTRL
  {0xE1, 14, {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44,
              0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A}, 10},                 // NVGAMCTRL
};

static void ApsWriteCmd(uint8_t cmd) {
  digitalWrite(APS_PIN_DC, LOW);
  digitalWrite(APS_PIN_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(APS_PIN_CS, HIGH);
}

static void ApsWriteData(const uint8_t *data, size_t len) {
  digitalWrite(APS_PIN_DC, HIGH);
  digitalWrite(APS_PIN_CS, LOW);
  SPI.writeBytes(data, len);
  digitalWrite(APS_PIN_CS, HIGH);
}

static void ApsWriteCmdData(const uint8_t *seq, size_t len) {
  ApsWriteCmd(seq[0]);
  if (len > 1) {
    ApsWriteData(seq + 1, len - 1);
  }
}

static void ApsPanelInit(void) {
  pinMode(APS_PIN_CS, OUTPUT);
  digitalWrite(APS_PIN_CS, HIGH);
  pinMode(APS_PIN_DC, OUTPUT);
  digitalWrite(APS_PIN_DC, HIGH);
  pinMode(APS_PIN_BL, OUTPUT);
  digitalWrite(APS_PIN_BL, LOW);                      // Backlight stays off until the first frame

  SPI.begin(APS_PIN_SCLK, -1, APS_PIN_MOSI, -1);
  SPI.beginTransaction(SPISettings(APS_SPI_HZ, MSBFIRST, SPI_MODE0));

  uint8_t data[16];

  ApsWriteCmd(0x01);                                  // SWRESET
  delay(150);
  ApsWriteCmd(0x11);                                  // SLPOUT
  delay(120);

  uint8_t madctl[] = {0x36, 0x00};                    // MADCTL: no mirror, RGB order
  ApsWriteCmdData(madctl, sizeof(madctl));

  uint8_t colmod[] = {0x3A, 0x55};                    // COLMOD: 16 bits per pixel
  ApsWriteCmdData(colmod, sizeof(colmod));
  delay(10);

  uint8_t ramctrl[] = {0xB0, 0x00, 0xF0};             // RAMCTRL: same values as the working ESP-IDF driver
  ApsWriteCmdData(ramctrl, sizeof(ramctrl));

  for (uint32_t i = 0; i < sizeof(APS_INIT_CMDS) / sizeof(APS_INIT_CMDS[0]); i++) {
    const aps_init_cmd_t *c = &APS_INIT_CMDS[i];
    data[0] = c->cmd;
    memcpy(data + 1, c->data, c->len);
    ApsWriteCmdData(data, c->len + 1);
    if (c->delay_ms) {
      delay(c->delay_ms);
    }
  }

  ApsWriteCmd(0x21);                                  // INVON - this panel needs inverted colors
  ApsWriteCmd(0x13);                                  // NORON
  ApsWriteCmd(0x29);                                  // DISPON

  SPI.endTransaction();
}

// Push the drawing strip to the panel at row y.
static void ApsBlit(uint16_t y) {
  uint16_t *buffer = aps_canvas->getBuffer();
  size_t pixels = (size_t)APS_SCREEN_W * APS_STRIPE_H;
  for (size_t i = 0; i < pixels; i++) {
    uint16_t color = buffer[i];
    buffer[i] = (uint16_t)((color << 8) | (color >> 8));   // ST7789 expects big endian pixel data
  }

  uint16_t y_end = y + APS_STRIPE_H - 1;
  SPI.beginTransaction(SPISettings(APS_SPI_HZ, MSBFIRST, SPI_MODE0));
  uint8_t caset[] = {0x2A, 0x00, 0x00, (uint8_t)((APS_SCREEN_W - 1) >> 8), (uint8_t)(APS_SCREEN_W - 1)};
  ApsWriteCmdData(caset, sizeof(caset));
  uint8_t raset[] = {0x2B, (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y_end >> 8), (uint8_t)y_end};
  ApsWriteCmdData(raset, sizeof(raset));
  ApsWriteCmd(0x2C);                                  // RAMWR
  ApsWriteData((const uint8_t *)buffer, pixels * 2);
  SPI.endTransaction();
}

// Note: the font parameters are declared const void * on purpose.  PlatformIO's
// ino-to-cpp step hoists generated function prototypes to the top of the merged
// sketch, before Adafruit_GFX.h is included, so a header type such as GFXfont
// cannot appear in any function signature of a .ino file.
static uint16_t ApsTextWidth(const char *text, const void *font) {
  aps_canvas->setFont((const GFXfont *)font);
  int16_t x1, y1;
  uint16_t w, h;
  aps_canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// Draw one centred text line into a full width strip and flush it.  Long text falls back to
// a smaller font so that the line still fits the 240 pixel wide screen.
static void ApsDrawText(uint16_t y, const char *text, uint16_t color,
                        const void *font, const void *sfont, const void *xfont) {
  const GFXfont *use = (const GFXfont *)font;
  if (sfont && (ApsTextWidth(text, font) > (APS_SCREEN_W - 16))) {
    use = (const GFXfont *)sfont;
    if (xfont && (ApsTextWidth(text, sfont) > (APS_SCREEN_W - 16))) {
      use = (const GFXfont *)xfont;
    }
  }
  aps_canvas->fillScreen(APS_COLOR_BG);
  aps_canvas->setFont(use);
  aps_canvas->setTextColor(color);
  aps_canvas->setTextWrap(false);
  int16_t x1, y1;
  uint16_t w, h;
  aps_canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  aps_canvas->setCursor((int16_t)((APS_SCREEN_W - w) / 2) - x1, (int16_t)((APS_STRIPE_H - h) / 2) - y1);
  aps_canvas->print(text);
  ApsBlit(y);
}

static void ApsDrawDivider(uint16_t y) {
  aps_canvas->fillScreen(APS_COLOR_BG);
  aps_canvas->drawFastHLine(24, APS_STRIPE_H / 2, APS_SCREEN_W - 48, APS_COLOR_CYAN);
  ApsBlit(y);
}

static void ApsClearStrip(uint16_t y) {
  aps_canvas->fillScreen(APS_COLOR_BG);
  ApsBlit(y);
}

// ---------------------------------------------------------------- CJK text --

// Decode one UTF-8 code point.  Returns false for empty or invalid input.
static bool ApsUtf8Next(const char **cursor, uint32_t *codepoint) {
  const uint8_t *p = (const uint8_t *)*cursor;
  uint8_t lead = *p;
  if (0 == lead) {
    return false;
  }
  uint32_t value;
  uint8_t extra;
  if (lead < 0x80) {
    value = lead;
    extra = 0;
  } else if ((lead & 0xE0) == 0xC0) {
    value = lead & 0x1F;
    extra = 1;
  } else if ((lead & 0xF0) == 0xE0) {
    value = lead & 0x0F;
    extra = 2;
  } else if ((lead & 0xF8) == 0xF0) {
    value = lead & 0x07;
    extra = 3;
  } else {
    *cursor = (const char *)(p + 1);
    return false;
  }
  for (uint8_t i = 0; i < extra; i++) {
    if ((p[1 + i] & 0xC0) != 0x80) {
      *cursor = (const char *)(p + 1);
      return false;
    }
    value = (value << 6) | (p[1 + i] & 0x3F);
  }
  *cursor = (const char *)(p + 1 + extra);
  *codepoint = value;
  return true;
}

static int ApsCjkGlyphIndex(uint32_t codepoint) {
  for (uint32_t i = 0; i < sizeof(APS_CJK_CODEPOINTS) / sizeof(APS_CJK_CODEPOINTS[0]); i++) {
    if (APS_CJK_CODEPOINTS[i] == codepoint) {
      return (int)i;
    }
  }
  return -1;
}

// Draw one centred Chinese line using the embedded 24x24 glyphs, then flush the strip.
static void ApsDrawCjkText(uint16_t y, const char *text, uint16_t color) {
  uint16_t glyphs = 0;
  const char *cursor = text;
  uint32_t codepoint;
  while (ApsUtf8Next(&cursor, &codepoint)) {
    if (ApsCjkGlyphIndex(codepoint) >= 0) {
      glyphs++;
    }
  }
  if (0 == glyphs) {
    ApsClearStrip(y);
    return;
  }

  int16_t x_start = (APS_SCREEN_W - (int16_t)(glyphs * APS_CJK_GLYPH_W)) / 2;
  int16_t y_start = (APS_STRIPE_H - APS_CJK_GLYPH_H) / 2;
  aps_canvas->fillScreen(APS_COLOR_BG);
  cursor = text;
  while (ApsUtf8Next(&cursor, &codepoint)) {
    int index = ApsCjkGlyphIndex(codepoint);
    if (index < 0) {
      continue;
    }
    const uint8_t *bits = APS_CJK_BITS + APS_CJK_OFFSETS[index];
    for (uint8_t row = 0; row < APS_CJK_GLYPH_H; row++) {
      for (uint8_t col = 0; col < APS_CJK_GLYPH_W; col++) {
        if (bits[row * (APS_CJK_GLYPH_W / 8) + (col >> 3)] & (0x80 >> (col & 7))) {
          aps_canvas->drawPixel(x_start + col, y_start + row, color);
        }
      }
    }
    x_start += APS_CJK_GLYPH_W;
  }
  ApsBlit(y);
}

static void ApsDrawSetupScreen(void) {
  ApsClearStrip(0);
  ApsDrawCjkText(40, "无线网络设置", APS_COLOR_WHITE);
  ApsDrawDivider(80);
  ApsDrawCjkText(120, "第一步：连接热点", APS_COLOR_GRAY);
  ApsDrawText(160, TasmotaGlobal.hostname, APS_COLOR_CYAN, &FreeSansBold12pt7b, &FreeSans9pt7b, nullptr);
  ApsDrawCjkText(200, "第二步：打开网页", APS_COLOR_GRAY);
  ApsDrawText(240, "http://192.168.4.1", APS_COLOR_CYAN, &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSans9pt7b);
  ApsClearStrip(280);
}

static void ApsDrawConnectedScreen(void) {
  String url = String("http://") + WiFi.localIP().toString();
  ApsClearStrip(0);
  ApsDrawCjkText(40, "已连接无线网", APS_COLOR_WHITE);
  ApsDrawDivider(80);
  ApsDrawCjkText(120, "网络名称：", APS_COLOR_GRAY);
  ApsDrawText(160, WiFi.SSID().c_str(), APS_COLOR_CYAN, &FreeSansBold12pt7b, &FreeSans9pt7b, nullptr);
  ApsDrawCjkText(200, "管理页面：", APS_COLOR_GRAY);
  ApsDrawText(240, url.c_str(), APS_COLOR_CYAN, &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSans9pt7b);
  ApsClearStrip(280);
}

static void ApsScreenRefresh(void) {
  uint32_t ip = (uint32_t)WiFi.localIP();
  uint8_t screen = WifiHasIPv4() ? 2 : 1;             // 2 = connected, 1 = Wi-Fi setup hint

  bool redraw = (screen != aps_screen) || ((2 == screen) && (ip != aps_ip));
  if ((1 == screen) && (0 != strcmp(aps_ssid, TasmotaGlobal.hostname))) {
    redraw = true;                                    // Hostname was not known yet on an earlier frame
  }
  if (!redraw) {
    return;
  }

  if (2 == screen) {
    ApsDrawConnectedScreen();
  } else {
    ApsDrawSetupScreen();
    strncpy(aps_ssid, TasmotaGlobal.hostname, sizeof(aps_ssid) - 1);
    aps_ssid[sizeof(aps_ssid) - 1] = 0;
  }
  aps_screen = screen;
  aps_ip = ip;
}

static void ApsScreenInit(void) {
  if (aps_ready) {
    return;
  }
  ApsPanelInit();
  aps_canvas = new GFXcanvas16(APS_SCREEN_W, APS_STRIPE_H);
  if (!aps_canvas || !aps_canvas->getBuffer()) {
    AddLog(LOG_LEVEL_ERROR, PSTR("APS: Not enough memory for the screen buffer"));
    return;
  }
  aps_ready = true;
  ApsScreenRefresh();                                 // First frame
  digitalWrite(APS_PIN_BL, HIGH);                     // Backlight on with a clean screen
  AddLog(LOG_LEVEL_INFO, PSTR("APS: AI Passport screen active"));
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv95(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      ApsScreenInit();
      break;
    case FUNC_EVERY_SECOND:
      if (aps_ready) {
        ApsScreenRefresh();
      }
      break;
    case FUNC_ACTIVE:
      result = true;
      break;
  }
  return result;
}

#endif  // USE_AIPASSPORT_SCREEN
