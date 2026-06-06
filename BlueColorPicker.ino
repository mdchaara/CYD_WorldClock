// =====================================================================
// Blue / Navy Color Picker for ESP32-2432S028 (CYD)
// Shows a grid of blue & navy shades on screen.
// Tap any swatch to see its RGB values fullscreen.
// Tap again to return to the grid.
//
// Use this to pick the exact colors that look best on YOUR panel,
// then copy the RGB values into your World Clock CONFIG block.
// =====================================================================
#include <LovyanGFX.hpp>

// -------- Display driver (same as your World Clock) --------
class LGFX_ESP32_2432S028 : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;
public:
    LGFX_ESP32_2432S028() {
        { auto c = _bus.config();
          c.spi_host=VSPI_HOST; c.spi_mode=0; c.freq_write=27000000;
          c.pin_sclk=14; c.pin_mosi=13; c.pin_miso=12; c.pin_dc=2;
          _bus.config(c); _panel.setBus(&_bus); }
        { auto c = _panel.config();
          c.pin_cs=15; c.pin_rst=-1;
          c.panel_width=320; c.panel_height=240;
          c.memory_width=320; c.memory_height=240;
          c.rgb_order=true; c.invert=false;
          _panel.config(c); }
        { auto c = _light.config();
          c.pin_bl=21; c.invert=false;
          _light.config(c); _panel.setLight(&_light); }
        { auto c = _touch.config();
          c.x_min=240; c.x_max=3800; c.y_min=3700; c.y_max=200;
          c.pin_mosi=32; c.pin_miso=39; c.pin_sclk=25; c.pin_cs=33;
          c.pin_int=-1; c.spi_host=-1; c.freq=1000000;
          c.bus_shared=false; c.offset_rotation=6;
          _touch.config(c); _panel.setTouch(&_touch); }
        setPanel(&_panel);
    }
};

LGFX_ESP32_2432S028 display;

// =====================================================================
// COLOR PALETTE — feel free to edit / add / remove
// =====================================================================
struct BlueColor {
    const char* name;
    uint8_t r, g, b;
};

BlueColor palette[] = {
    // ---- Bright / Sky blues (good for DAY backgrounds) ----
    {"Sky 1",         0, 150, 255},  // your current day color
    {"Sky 2",        30, 144, 255},  // Dodger Blue
    {"Sky 3",        70, 180, 255},
    {"Sky 4",       100, 200, 255},
    {"Azure",         0, 180, 230},
    {"Cyan-Blue",     0, 191, 255},  // Deep Sky Blue

    // ---- Mid blues ----
    {"Royal",        65, 105, 225},  // Royal Blue
    {"Cobalt",        0,  71, 171},
    {"Cornflower",  100, 149, 237},
    {"Steel",        70, 130, 180},

    // ---- Navy / Dark blues (good for NIGHT backgrounds) ----
    {"Navy 1",        0,   0, 100},  // your current night color
    {"Navy 2",        0,   0,  80},
    {"Navy 3",       10,  20,  80},
    {"Navy 4",       15,  25, 110},
    {"Midnight",     25,  25, 112},  // Midnight Blue
    {"Indigo",       40,   0,  90},

    // ---- Very dark / near-black blues ----
    {"Deep 1",        0,   0,  50},
    {"Deep 2",       10,  10,  40},
    {"Deep 3",       20,  20,  60},
    {"Deep 4",        5,  15,  35},
};

const int numColors = sizeof(palette) / sizeof(BlueColor);

// =====================================================================
// Grid layout
// =====================================================================
const int COLS = 5;
const int ROWS = 4;       // COLS*ROWS = 20 swatches
const int SWATCH_W = 320 / COLS;     // 64 px
const int SWATCH_H = 240 / ROWS;     // 60 px

enum ViewState { VIEW_GRID, VIEW_DETAIL };
ViewState view = VIEW_GRID;
int selectedColor = 0;

unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 400;

// =====================================================================
void setup() {
    Serial.begin(115200);
    display.init();
    display.setRotation(6);
    display.setBrightness(200);
    drawGrid();
}

void loop() {
    handleTouch();
}

// =====================================================================
// Drawing
// =====================================================================
void drawGrid() {
    display.fillScreen(TFT_BLACK);

    for (int i = 0; i < numColors && i < COLS*ROWS; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int x = col * SWATCH_W;
        int y = row * SWATCH_H;

        uint16_t c = display.color888(palette[i].r, palette[i].g, palette[i].b);
        display.fillRect(x, y, SWATCH_W, SWATCH_H, c);

        // Small label - white or black depending on brightness
        uint16_t txtColor = isDark(palette[i]) ? TFT_WHITE : TFT_BLACK;
        display.setTextColor(txtColor);
        display.setFont(&fonts::Font0);
        display.setTextSize(1);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString(palette[i].name, x + SWATCH_W/2, y + SWATCH_H/2 - 5);

        // Index number
        char buf[4];
        sprintf(buf, "#%d", i);
        display.drawString(buf, x + SWATCH_W/2, y + SWATCH_H/2 + 6);
    }
}

void drawDetail(int idx) {
    BlueColor& c = palette[idx];
    uint16_t bg = display.color888(c.r, c.g, c.b);
    display.fillScreen(bg);

    uint16_t fg = isDark(c) ? TFT_WHITE : TFT_BLACK;
    display.setTextColor(fg, bg);
    display.setTextDatum(textdatum_t::middle_center);

    // Name (big)
    display.setFont(&fonts::FreeSans18pt7b);
    display.drawString(c.name, 160, 50);

    // RGB values
    char buf[40];
    display.setFont(&fonts::FreeSans12pt7b);
    sprintf(buf, "RGB: %d, %d, %d", c.r, c.g, c.b);
    display.drawString(buf, 160, 110);

    // Hex value
    sprintf(buf, "Hex: #%02X%02X%02X", c.r, c.g, c.b);
    display.drawString(buf, 160, 145);

    // 565 value (LovyanGFX/TFT format)
    uint16_t rgb565 = display.color565(c.r, c.g, c.b);
    sprintf(buf, "565: 0x%04X", rgb565);
    display.drawString(buf, 160, 180);

    // Footer hint
    display.setFont(&fonts::Font0);
    display.drawString("Tap to go back  -  Index #" + String(idx), 160, 225);

    // Also print to Serial for easy copy/paste
    Serial.printf("Selected #%d: %s  RGB(%d,%d,%d)  Hex #%02X%02X%02X  565 0x%04X\n",
                  idx, c.name, c.r, c.g, c.b, c.r, c.g, c.b, rgb565);
}

// Brightness check (perceptual luminance) to pick readable label color
bool isDark(const BlueColor& c) {
    // Rec. 601 luma
    int luma = (c.r * 299 + c.g * 587 + c.b * 114) / 1000;
    return luma < 128;
}

// =====================================================================
// Touch
// =====================================================================
void handleTouch() {
    lgfx::touch_point_t tp;
    if (!display.getTouch(&tp)) return;

    if (millis() - lastTouchTime < TOUCH_DEBOUNCE) return;
    lastTouchTime = millis();

    // Same remap as your World Clock
    int tx = constrain(map(tp.y, 0,   230, 0, 320), 0, 320);
    int ty = constrain(map(tp.x, 308, 28,  0, 240), 0, 240);

    if (view == VIEW_GRID) {
        int col = tx / SWATCH_W;
        int row = ty / SWATCH_H;
        int idx = row * COLS + col;
        if (idx >= 0 && idx < numColors) {
            selectedColor = idx;
            view = VIEW_DETAIL;
            drawDetail(idx);
        }
    } else {
        view = VIEW_GRID;
        drawGrid();
    }
}
