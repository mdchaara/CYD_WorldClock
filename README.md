# MDC World Clock

A touch-based world clock for the **ESP32-2432S028 ("Cheap Yellow Display")** that shows the time, date, weather, and time-of-day phase for 27 cities across the globe. Touch left/right to travel east or west around the world. Designed as a desk clock — periodically wakes itself up to display the time, then runs a Pac-Man-style attract-mode screensaver.


---

## Features

- **27 cities, east-to-west ordered**, from Auckland to Honolulu, with accurate DST handling for every region (including non-trivial cases: Egypt's reinstated DST, Iran's permanent UTC+3:30, Turkey's permanent UTC+3, New Zealand summer time, half-hour offsets for India and Iran).
- **Live weather** for every city, fetched from OpenWeatherMap and refreshed every 15 minutes.
- **Time-of-day background colors** — the screen tints itself based on local sunrise/sunset: dawn, morning, late afternoon, dusk, and night, each with its own background and text color. Transitions are detected automatically and trigger a redraw the moment a city crosses a phase boundary.
- **Touch navigation** — tap the right third of the screen to move east, the left third to move west. Tap the bottom-right corner to enter WiFi setup.
- **Pac-Man attract-mode screensaver** — two scenes that loop forever: the classic chase (Pac-Man eating dots, ghost chasing) and a "revenge" scene (frightened ghost runs from Pac-Man). Ghost colors and screen position are randomized each scene.
- **Desk-clock auto-wake** — at every multiple of 15 minutes (`:00`, `:15`, `:30`, `:45`), the display wakes from screen-off and shows the clock, then runs the idle progression again.
- **Persistent city selection** — the last viewed city is remembered across reboots.
- **Captive-portal WiFi setup** — no hardcoded credentials; first boot creates an access point you connect to and enter your network details.
- **Single CONFIG block** — everything tunable (colors, timing, fonts, positions, sprite sizes, screensaver behavior) lives at the top of the file.

---

## Hardware

- **ESP32-2432S028** (sometimes called CYD — "Cheap Yellow Display"). 320×240 ILI9341 LCD with XPT2046 resistive touch. Widely available on AliExpress / Amazon for ~$10.
- USB-C or micro-USB cable (depends on which CYD variant you have).
- For power, I used a TP4056 to charge and discharge a Li-ion battery. You may choose any power source you have to power the device up.

This sketch uses landscape orientation (`rotation 6`) and the touch driver is calibrated for that orientation.

---

## Libraries

Install these via Arduino Library Manager:

| Library | Author | Purpose |
|---|---|---|
| LovyanGFX | lovyan03 | Display + touch driver |
| WiFiManager | tzapu | Captive-portal WiFi setup |
| ArduinoJson | Benoît Blanchon | Parse OpenWeather API responses |
| Time | Paul Stoffregen | Time arithmetic |
| Timezone | Jack Christensen | DST rules |

`HTTPClient` and `Preferences` are part of the ESP32 Arduino core — no separate install needed.

---

## Setup

### 1. Get an OpenWeatherMap API key
Sign up for free at [openweathermap.org](https://openweathermap.org/api). The free tier covers the request volume this clock generates by a wide margin (27 cities × 4 calls/hour = ~108 calls/hour; free tier allows 60/minute).

### 2. Open the sketch in Arduino IDE
Board: `ESP32 Dev Module` (or whichever ESP32 board your CYD identifies as).

### 3. Configure
Open `WorldClock.ino` and find the CONFIG block at the top. At minimum, replace the API key:

```cpp
const char* API_KEY = "your_openweather_api_key_here";
```

### 4. Upload
Compile and upload. On first boot, the device creates a WiFi access point named `MDC-WorldClock-AP`. Connect to it with your phone or laptop, then browse to `192.168.4.1` and enter your home WiFi credentials. The device will reconnect and start.

---

## Usage

### Touch zones

```
+--------------------------------+
|                                |
|   TAP LEFT          TAP RIGHT  |
|   = go WEST         = go EAST  |
|   (next city)       (prev city)|
|                                |
|                                |
|                    WiFi setup  |
+--------------------------------+
```

- **Left third of screen** → move one city west
- **Right third of screen** → move one city east
- **Bottom-right corner** → enter WiFi setup mode (5-second cancel window if tapped by mistake)

### Idle behavior

```
TOUCH or AUTO-WAKE
       │
       ▼
   [CLOCK]  ──── 2 min idle ────►  [PAC-MAN]  ──── 3 min idle ────►  [BACKLIGHT OFF]
       ▲                                                                    │
       │                                                                    │
       └──── at :00, :15, :30, :45 local minutes (auto-wake) ───────────────┘
```

Any touch resets the idle timer and brings the clock back. The auto-wake fires regardless of touch state every 15 minutes.

---

## Customization

Everything is configured at the top of `WorldClock.ino` between the `========= CONFIG =========` banner and `========= END OF USER CONFIG =========`.

### Change wake interval
```cpp
const int AUTO_WAKE_INTERVAL_MIN = 15;   // try 5, 30, 60
// Set to 0 to disable auto-wake entirely.
```

### Change time-of-day colors
```cpp
#define COLOR_MORNING_R   0
#define COLOR_MORNING_G 150
#define COLOR_MORNING_B 255
```

For picking colors that look good on your specific (often color-shifted) panel, a companion sketch `BlueColorPicker.ino` is included — it shows a grid of 20 blue/navy shades you can tap to see their exact RGB values.

### Change date format
The date line under the clock is rendered with `strftime()`. Edit this line in `drawTime()`:
```cpp
strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y", tm_);
```
Examples:
- `"%a, %d %b %Y"` → "Thu, 04 Jun 2026"
- `"%a, %b %d"` → "Thu, Jun 04"
- `"%d/%m/%Y"` → "04/06/2026"

### Add or remove cities
Each city is one line in the `cities[]` array:
```cpp
{"London", "UK", 51.5074, -0.1278, &tzLondon, "", ""},
```
You need: display name, country, latitude, longitude, and a pointer to a `Timezone` object. If you add a city in a region with DST you don't already have, define new `TimeChangeRule` constants and a new `Timezone` object first.

### Change default city
```cpp
#define DEFAULT_CITY_INDEX 10
```
This is the index into `cities[]` shown on first boot or after a corrupt preferences read. Count from 0 starting at Auckland.

### Tweak the screensaver
```cpp
const int   PAC_FRAME_DELAY  = 35;    // ms per frame
const int   PAC_RADIUS       = 12;    // sprite size
const float PAC_SPEED_CHASE  = 1.8;
const float GHOST_SPEED_CHASE= 1.6;
```
Bigger sprites: bump `PAC_RADIUS` to 16. Slower demo: lower the speeds. Faster chomp: edit `pac.mouthPhase += 0.35` inside `drawPacFrame()`.

### Adjust idle/screen-off timing
```cpp
const unsigned long SCREENSAVER_DELAY = 120000UL;   // 2 min
const unsigned long SCREENOFF_DELAY   = 300000UL;   // 5 min
```

---

## File layout

```
WorldClock.ino           Main sketch (single file)
BlueColorPicker.ino      Standalone helper sketch: shows blue/navy
                         color swatches to pick exact RGB values
README.md                This file
```

---

## How it works

The sketch is organized as a single file with a heavy CONFIG block at the top and implementation below. The flow:

1. **`setup()`** initializes the display, runs the WiFi captive portal if needed, syncs time over NTP, loads the saved city, fetches weather for all 27 cities, and draws the clock.

2. **`loop()`** runs three state machines:
   - **CLOCK state**: every second, checks if the minute changed (redraw time + date) or if the time-of-day phase changed (redraw background); every 15 minutes, refresh weather; after 2 minutes idle, enter screensaver.
   - **SCREENSAVER state**: every 35 ms, render one frame of the Pac-Man demo; after 5 minutes idle, turn off the backlight.
   - **OFF state**: wait for either a touch or an auto-wake minute mark.

3. **Touch handler** reads the XPT2046, remaps coordinates to landscape, and dispatches to city navigation, WiFi setup, or wake-from-screensaver depending on where you tapped.

4. **Time-of-day phase** is computed from latitude + day-of-year (declination formula), giving sunrise and sunset times. The current local hour is bucketed into one of five phases, each with its own background and foreground color. Handles polar day / polar night safely.

5. **Weather** is fetched with a streaming JSON parser using a filter so only `main.temp` and `weather[0].description` are extracted — saves significant heap on the ESP32.

6. **Auto-wake** computes the current local minute-of-day each loop iteration in screensaver or off states. When the minute is a multiple of 15 and we haven't already woken at this exact minute, the clock wakes and resets the idle timer.

---

## Troubleshooting

**Colors look wrong / blue looks green.** Cheap ILI9341 panels often have BGR-order pixels instead of RGB. Try flipping `cfg.rgb_order = false;` in the panel config inside the `LGFX_ESP32_2432S028` driver class. If colors look washed-out, try `cfg.invert = true;`.

**Touch is offset or inverted.** The touch driver is calibrated for a specific orientation. If your panel responds in mirrored locations, adjust the `x_min`, `x_max`, `y_min`, `y_max` values in the `_touch.config()` block, or change `offset_rotation`.

**Weather always shows "Error".** Check your API key is correct, that you have WiFi, and that the city name in `cities[]` matches an OpenWeather-known city. Some cities (e.g. "Sharq" in Kuwait) are local neighborhoods — OpenWeather may not recognize them. Swap to a globally-known nearby city name.

**Clock shows wrong time.** The sketch syncs to NTP at boot; if your network blocks NTP, time will never set and the device will hang in setup. Make sure UDP port 123 is open. If only specific cities show wrong time, check the DST rules — DST policies change occasionally (Egypt did in 2023, Iran abolished in 2022, etc.).

**Screen never sleeps / never wakes.** Check `SCREENSAVER_DELAY`, `SCREENOFF_DELAY`, and `AUTO_WAKE_INTERVAL_MIN`. Set the wake interval to 0 to disable, or shorten the delays for faster testing (e.g. `5000UL` for 5 seconds).

**"Failed to compile: TimeChangeRule undefined."** Make sure the `Timezone` library (Jack Christensen's) is installed, not a different library with the same name.

---

## Credits

- Display driver: [LovyanGFX](https://github.com/lovyan03/LovyanGFX) by lovyan03
- WiFi captive portal: [WiFiManager](https://github.com/tzapu/WiFiManager) by tzapu
- Timezone rules: [Timezone](https://github.com/JChristensen/Timezone) by Jack Christensen
- Weather data: [OpenWeatherMap](https://openweathermap.org)
- Pac-Man and Mario are trademarks of their respective owners; this is a fan tribute screensaver, not affiliated with or endorsed by them.

---

## License

MIT — see `LICENSE` file. Use, modify, and share freely.
