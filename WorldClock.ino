// =====================================================================
// MDC World Clock for ESP32-2432S028 (CYD)
// Optimized version — all configuration in the CONFIG block below
// =====================================================================
#include <LovyanGFX.hpp>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <Preferences.h>

// =====================================================================
// ============== CONFIG  (change anything here on the fly) ============
// =====================================================================

// -------- Display / Brightness --------
#define SCREEN_W            320
#define SCREEN_H            240
#define SCREEN_ROTATION     6
#define BRIGHTNESS_ON       250 //was 200
#define BRIGHTNESS_OFF      0

// -------- Colors (RGB888 — converted at runtime) --------
// Five time-of-day phases. Edit RGB to taste.
//
// Dawn       -> Deep 3   (palette #18)
// Morning    -> Sky 1    (palette #0)
// Late aft.  -> Sky 3    (palette #2)
// Dusk       -> Navy 4   (palette #13)
// Night      -> Navy 1   (palette #10)

#define COLOR_DAWN_R         20
#define COLOR_DAWN_G         20
#define COLOR_DAWN_B         60
#define COLOR_DAWN_FG        TFT_WHITE

#define COLOR_MORNING_R       0
#define COLOR_MORNING_G     150
#define COLOR_MORNING_B     255
#define COLOR_MORNING_FG     TFT_BLACK

#define COLOR_LATEAFT_R      70
#define COLOR_LATEAFT_G     180
#define COLOR_LATEAFT_B     255
#define COLOR_LATEAFT_FG     TFT_BLACK

#define COLOR_DUSK_R         15
#define COLOR_DUSK_G         25
#define COLOR_DUSK_B        110
#define COLOR_DUSK_FG        TFT_WHITE

#define COLOR_NIGHT_R         0
#define COLOR_NIGHT_G         0
#define COLOR_NIGHT_B       100
#define COLOR_NIGHT_FG       TFT_WHITE

// -------- Phase window widths (hours, relative to sunrise/sunset) --------
// Dawn window:    [sunrise - DAWN_BEFORE,  sunrise + DAWN_AFTER]
// Dusk window:    [sunset  - DUSK_BEFORE,  sunset  + DUSK_AFTER]
// Late afternoon: [sunset  - LATEAFT_BEFORE, sunset - DUSK_BEFORE]
// Morning/early afternoon: everything between dawn and late afternoon
// Night: outside everything else
const float DAWN_BEFORE    = 0.5;   // 30 min before sunrise
const float DAWN_AFTER     = 0.5;   // 30 min after sunrise
const float DUSK_BEFORE    = 0.5;   // 30 min before sunset
const float DUSK_AFTER     = 0.5;   // 30 min after sunset
const float LATEAFT_BEFORE = 2.0;   // 2 hours before sunset (until dusk starts)

// -------- API --------
const char* API_KEY        = "YOUR API KEY GOES HERE";
const char* WEATHER_HOST   = "http://api.openweathermap.org";
const char* NTP_SERVER     = "pool.ntp.org";
const char* WIFI_AP_NAME   = "MDC-WorldClock-AP";
const char* DOMAIN_TEXT    = "YOUR MESSAGE GOES HERE";

// -------- Timing (ms) --------
const unsigned long TIME_REFRESH_INTERVAL   = 1000;        // 1 s
const unsigned long WEATHER_REFRESH_INTERVAL= 900000UL;    // 15 min
const unsigned long SCREENSAVER_DELAY       = 120000UL;    // 2 min idle → screensaver
const unsigned long SCREENOFF_DELAY         = 300000UL;    // 5 min idle → backlight off
const unsigned long WIFI_CANCEL_WINDOW      = 5000UL;      // touch to cancel WiFi setup
const unsigned long TOUCH_DEBOUNCE          = 300UL;       // ms between city changes
const int           WIFI_CONFIG_TIMEOUT     = 180;         // s

// -------- Desk clock auto-wake --------
// At every multiple of AUTO_WAKE_INTERVAL_MIN local minutes (e.g. :00, :15,
// :30, :45 when set to 15), the display wakes from screen-off back to clock
// mode, then runs through the normal idle progression again:
//   clock (SCREENSAVER_DELAY) → screensaver → (SCREENOFF_DELAY) → backlight off.
// Set to 0 to disable auto-wake entirely.
const int           AUTO_WAKE_INTERVAL_MIN  = 15;

// -------- Screensaver (Pac-Man demo) --------
const int   PAC_FRAME_DELAY    = 35;     // ms per frame (~28 fps)
const int   PAC_RADIUS         = 12;     // Pac-Man / ghost size (pixels)
const float PAC_SPEED_CHASE    = 1.8;    // Scene 1: Pac-Man speed
const float GHOST_SPEED_CHASE  = 1.6;    // Scene 1: ghost slower (gets caught)
const float PAC_SPEED_REVENGE  = 1.5;    // Scene 2: Pac-Man slightly slower
const float GHOST_SPEED_REVENGE= 1.7;    // Scene 2: ghost faster (never caught)
const int   GHOST_LEAD         = 40;     // Scene 2: initial gap, pixels
const int   PAC_Y_MARGIN       = 6;      // min pixels from top/bottom edges
const int   PAC_DOT_SIZE       = 2;      // radius of pellets
const int   PAC_DOT_SPACING    = 16;     // pixels between dots
const unsigned long PAC_SCENE_PAUSE = 1200;  // ms between scenes

// -------- Layout (pixel positions) --------
#define POS_CITY_Y          40
#define POS_COUNTRY_Y       75
#define POS_TZ_Y            98
#define POS_TIME_Y          140
#define POS_DATE_Y          182
#define POS_WEATHER_Y       210
#define POS_WEATHER_TEMP_X  30
#define POS_WEATHER_DESC_X  290
#define POS_DEG_CIRCLE_Y    203
#define POS_DOMAIN_X        10
#define POS_DOMAIN_Y        238
#define POS_WIFI_X          310
#define POS_WIFI_Y          238

// -------- Touch zones (after remapping) --------
#define TOUCH_LEFT_MAX_X    106
#define TOUCH_RIGHT_MIN_X   214
#define TOUCH_WIFI_MIN_X    200
#define TOUCH_TOP_MAX_Y     190   // above = city nav, below-right = wifi

// -------- Fonts (change these — affects everything everywhere) --------
const lgfx::v1::IFont* fontCityName = &fonts::FreeSans12pt7b;
const lgfx::v1::IFont* fontCountry  = &fonts::DejaVu12;
const lgfx::v1::IFont* fontTZ       = &fonts::DejaVu9;
const lgfx::v1::IFont* fontTime     = &fonts::Font6;
const lgfx::v1::IFont* fontDate     = &fonts::FreeSans9pt7b;   // date / day of week
const lgfx::v1::IFont* fontWeather  = &fonts::FreeSans9pt7b;
const lgfx::v1::IFont* fontSmall    = &fonts::Font0;
const lgfx::v1::IFont* fontWiFiMsg  = &fonts::Font2;
const float TEXT_SIZE_CITY = 1.2;
const float TEXT_SIZE_TIME = 1.4;

// -------- Default city (used on first boot, before any city is saved) --------
// Index into the cities[] array below. 10 = Kuwait City.
#define DEFAULT_CITY_INDEX  10

// -------- Time Change Rules (DST) --------
// North America
TimeChangeRule usPDT   = {"PDT",  Second, Sun, Mar, 2, -420};
TimeChangeRule usPST   = {"PST",  First,  Sun, Nov, 2, -480};
TimeChangeRule usMDT   = {"MDT",  Second, Sun, Mar, 2, -360};
TimeChangeRule usMST   = {"MST",  First,  Sun, Nov, 2, -420};
TimeChangeRule usCDT   = {"CDT",  Second, Sun, Mar, 2, -300};
TimeChangeRule usCST   = {"CST",  First,  Sun, Nov, 2, -360};
TimeChangeRule usEDT   = {"EDT",  Second, Sun, Mar, 2, -240};
TimeChangeRule usEST   = {"EST",  First,  Sun, Nov, 2, -300};
TimeChangeRule akDT    = {"AKDT", Second, Sun, Mar, 2, -480};
TimeChangeRule akST    = {"AKST", First,  Sun, Nov, 2, -540};

// Europe
TimeChangeRule euDST   = {"CEST", Last,   Sun, Mar, 2, 120};
TimeChangeRule euSTD   = {"CET",  Last,   Sun, Oct, 3, 60};
TimeChangeRule ukDST   = {"BST",  Last,   Sun, Mar, 1, 60};      // UK summer time
TimeChangeRule ukSTD   = {"GMT",  Last,   Sun, Oct, 2, 0};

// Middle East / North Africa
TimeChangeRule gazaDST = {"EEST", Last,   Fri, Mar, 0, 180};
TimeChangeRule gazaSTD = {"EET",  Last,   Fri, Oct, 0, 120};
TimeChangeRule egyDST  = {"EEST", Last,   Fri, Apr, 0, 180};   // last Fri Apr -> last Thu Oct
TimeChangeRule egySTD  = {"EET",  Last,   Thu, Oct, 0, 120};

// Australia / NZ
TimeChangeRule auDST   = {"AEDT", First,  Sun, Oct, 2, 660};
TimeChangeRule auSTD   = {"AEST", First,  Sun, Apr, 3, 600};
TimeChangeRule nzDST   = {"NZDT", Last,   Sun, Sep, 2, 780};
TimeChangeRule nzSTD   = {"NZST", First,  Sun, Apr, 3, 720};

// Fixed offsets (no DST) — offset is in minutes from UTC
TimeChangeRule trkSTD  = {"TRT",  First, Sun, Jan, 0, 180};   // Turkey UTC+3
TimeChangeRule irSTD   = {"IRST", First, Sun, Jan, 0, 210};   // Iran UTC+3:30
TimeChangeRule indSTD  = {"IST",  First, Sun, Jan, 0, 330};   // India UTC+5:30
TimeChangeRule thaSTD  = {"ICT",  First, Sun, Jan, 0, 420};   // Thailand UTC+7
TimeChangeRule vlaSTD  = {"VLAT", First, Sun, Jan, 0, 600};   // Vladivostok UTC+10
TimeChangeRule hawSTD  = {"HST",  First, Sun, Jan, 0, -600};  // Hawaii UTC-10
TimeChangeRule utc1    = {"UTC+1",First, Sun, Jan, 0, 60};    // Morocco
TimeChangeRule utc3    = {"UTC+3",First, Sun, Jan, 0, 180};   // Kuwait, Makkah, Mombasa
TimeChangeRule utc4    = {"UTC+4",First, Sun, Jan, 0, 240};   // Abu Dhabi
TimeChangeRule utc8    = {"UTC+8",First, Sun, Jan, 0, 480};   // Guangzhou, KL
TimeChangeRule utc9    = {"UTC+9",First, Sun, Jan, 0, 540};   // Tokyo

// -------- Timezone Objects --------
Timezone tzAuckland(nzDST, nzSTD);
Timezone tzSydney(auDST, auSTD);
Timezone tzVladivostok(vlaSTD);
Timezone tzTokyo(utc9);
Timezone tzGuangzhou(utc8);
Timezone tzKL(utc8);
Timezone tzBangkok(thaSTD);
Timezone tzDelhi(indSTD);
Timezone tzAbuDhabi(utc4);
Timezone tzTehran(irSTD);
Timezone tzKuwait(utc3);
Timezone tzMakkah(utc3);
Timezone tzIstanbul(trkSTD);
Timezone tzGaza(gazaDST, gazaSTD);
Timezone tzCairo(egyDST, egySTD);
Timezone tzMombasa(utc3);
Timezone tzRome(euDST, euSTD);
Timezone tzParis(euDST, euSTD);
Timezone tzLondon(ukDST, ukSTD);
Timezone tzMadrid(euDST, euSTD);
Timezone tzRabat(utc1);
Timezone tzNYC(usEDT, usEST);
Timezone tzChicago(usCDT, usCST);
Timezone tzDenver(usMDT, usMST);
Timezone tzLA(usPDT, usPST);
Timezone tzAnchorage(akDT, akST);
Timezone tzHonolulu(hawSTD);

// -------- Cities (arranged East -> West) --------
// Index 0 = easternmost. Tap RIGHT to go east (lower index),
// tap LEFT to go west (higher index).
struct City {
    const char* name;
    const char* country;
    float lat;
    float lon;
    Timezone* tz;
    char weatherTemp[8];
    char weatherDesc[32];
};

City cities[] = {
    {"Auckland",     "New Zealand",  -36.8485,  174.7633, &tzAuckland,    "", ""},
    {"Sydney",       "Australia",    -33.8688,  151.2093, &tzSydney,      "", ""},
    {"Tokyo",        "Japan",         35.6895,  139.6917, &tzTokyo,       "", ""},
    {"Vladivostok",  "Russia",        43.1198,  131.8869, &tzVladivostok, "", ""},
    {"Guangzhou",    "China",         23.1291,  113.2644, &tzGuangzhou,   "", ""},
    {"Ampang",       "Malaysia",       3.1390,  101.6870, &tzKL,          "", ""},
    {"Bangkok",      "Thailand",      13.7563,  100.5018, &tzBangkok,     "", ""},
    {"Mumbai",       "India",         28.6139,   77.2090, &tzDelhi,       "", ""},
    {"Dubai",        "UAE",           24.4539,   54.3773, &tzAbuDhabi,    "", ""},
    {"Tehran",       "Iran",          35.6892,   51.3890, &tzTehran,      "", ""},
    {"Sharq",        "Kuwait",        29.3759,   47.9774, &tzKuwait,      "", ""},
    {"Mecca",        "Saudi Arabia",  21.3891,   39.8579, &tzMakkah,      "", ""},
    {"Mombasa",      "Kenya",         -4.0435,   39.6682, &tzMombasa,     "", ""},
    {"Gaza",         "Palestine",     31.5000,   34.4667, &tzGaza,        "", ""},
    {"Cairo",        "Egypt",         30.0444,   31.2357, &tzCairo,       "", ""},
    {"Istanbul",     "Turkey",        41.0082,   28.9784, &tzIstanbul,    "", ""},
    {"Rome",         "Italy",         41.9028,   12.4964, &tzRome,        "", ""},
    {"Paris",        "France",        48.8566,    2.3522, &tzParis,       "", ""},
    {"London",       "UK",            51.5074,   -0.1278, &tzLondon,      "", ""},
    {"Madrid",       "Spain",         40.4168,   -3.7038, &tzMadrid,      "", ""},
    {"Rabat",        "Morocco",       34.0209,   -6.8416, &tzRabat,       "", ""},
    {"Manhattan",    "USA",           40.7128,  -74.0060, &tzNYC,         "", ""},
    {"Chicago",      "USA",           41.8781,  -87.6298, &tzChicago,     "", ""},
    {"Denver",       "USA",           39.7392, -104.9903, &tzDenver,      "", ""},
    {"Malibu",       "USA",           34.0522, -118.2437, &tzLA,          "", ""},
    {"Anchorage",    "USA",           61.2181, -149.9003, &tzAnchorage,   "", ""},
    {"Honolulu",     "USA",           21.3099, -157.8581, &tzHonolulu,    "", ""}
};
const int numCities = sizeof(cities) / sizeof(City);

// =====================================================================
// ================== END OF USER CONFIG  ==============================
// =====================================================================


// -------- LovyanGFX driver --------
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
          c.panel_width=SCREEN_W; c.panel_height=SCREEN_H;
          c.memory_width=SCREEN_W; c.memory_height=SCREEN_H;
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
WiFiManager wm;
Preferences prefs;

// -------- Runtime state --------
uint16_t currentBgColor = 0x0000;
uint16_t currentFgColor = 0xFFFF;
int      currentCity    = 0;

enum ScreenState { STATE_CLOCK, STATE_SCREENSAVER, STATE_OFF };
ScreenState screenState = STATE_CLOCK;
unsigned long lastActivityTime = 0;

// Auto-wake: stores the local minute-of-day of the last auto-wake event,
// so we don't repeatedly wake during the same minute. -1 = no wake yet.
int lastAutoWakeMinute = -1;

// Cache last displayed time so we only redraw when minute changes
char lastTimeStr[6] = "";

// Time-of-day phase (full enum defined later in SUN section).
// Forward-declared here so loop() can use it.
enum TimePhase { PHASE_DAWN, PHASE_MORNING, PHASE_LATEAFT, PHASE_DUSK, PHASE_NIGHT };
TimePhase getTimePhase(int idx, struct tm* localTM);

// Forward declarations for auto-wake helpers (used in loop() before defined)
bool checkAutoWake();
void autoWakeToClock();

// Track current phase to auto-redraw background on phase change.
// -1 = uninitialised (forces redraw on first check).
int currentPhase = -1;

// Pac-Man demo screensaver state
enum PacScene { SCENE_CHASE, SCENE_PAUSE_AFTER_CHASE, SCENE_REVENGE, SCENE_PAUSE_AFTER_REVENGE };
struct PacDemo {
    PacScene scene;
    float pacX, pacY;          // Pac-Man position
    float ghostX, ghostY;      // Ghost position
    int   facing;              // -1 = left, +1 = right
    float mouthPhase;          // 0..2π for chomp animation
    unsigned long sceneStartTime;
    // Dots: each scene has a row of pellets. Bitmask tracks eaten ones.
    // 320/16 = 20 dots max
    uint32_t dotEaten;         // bit i = dot i eaten
    int   numDots;
    int   dotY;                // y-coord of dot row
    bool  ghostFrightened;     // blue ghost in revenge scene
    uint16_t ghostColor;       // randomized per scene
};
PacDemo pac;
unsigned long lastFrameTime = 0;


// =====================================================================
// SETUP
// =====================================================================
void setup() {
    Serial.begin(115200);

    display.init();
    display.setRotation(SCREEN_ROTATION);
    display.setBrightness(BRIGHTNESS_ON);

    // Splash
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setFont(fontWiFiMsg);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString("Connecting to WiFi...", SCREEN_W/2, SCREEN_H/2);

    WiFi.mode(WIFI_STA);
    wm.setConfigPortalTimeout(WIFI_CONFIG_TIMEOUT);
    if (!wm.autoConnect(WIFI_AP_NAME)) {
        Serial.println("WiFi failed, restarting...");
        ESP.restart();
    }

    configTzTime("UTC0", NTP_SERVER);
    while (time(nullptr) < 100000) delay(500);

    currentCity = loadCityIndex();

    for (int i = 0; i < numCities; i++) updateWeather(i);

    lastActivityTime = millis();
    redrawAll();
}

// =====================================================================
// MAIN LOOP
// =====================================================================
void loop() {
    handleTouch();
    const unsigned long now = millis();

    if (screenState == STATE_CLOCK) {
        static unsigned long lastWeather = 0;
        static unsigned long lastTime    = 0;

        if (now - lastTime >= TIME_REFRESH_INTERVAL) {
            lastTime = now;

            // Check if time-of-day phase has changed -> redraw background
            time_t t = time(nullptr);
            TimeChangeRule* tcr;
            time_t local = cities[currentCity].tz->toLocal(t, &tcr);
            struct tm* tm_ = localtime(&local);
            int newPhase = (int)getTimePhase(currentCity, tm_);
            if (newPhase != currentPhase) {
                redrawAll();
            } else {
                drawTime(currentCity);
            }
        }
        if (now - lastWeather >= WEATHER_REFRESH_INTERVAL) {
            lastWeather = now;
            for (int i = 0; i < numCities; i++) updateWeather(i);
            redrawAll();
        }
        if (now - lastActivityTime > SCREENSAVER_DELAY) {
            screenState = STATE_SCREENSAVER;
            initPacDemo();
        }
    }
    else if (screenState == STATE_SCREENSAVER) {
        if (now - lastFrameTime >= PAC_FRAME_DELAY) {
            lastFrameTime = now;
            drawPacFrame();
        }
        if (now - lastActivityTime > SCREENOFF_DELAY) {
            screenState = STATE_OFF;
            display.setBrightness(BRIGHTNESS_OFF);
            display.fillScreen(TFT_BLACK);
        }
        // Auto-wake from screensaver too (rare path: user is staring at it
        // and the minute mark arrives). Resets idle timer so clock shows
        // for full SCREENSAVER_DELAY again.
        if (checkAutoWake()) autoWakeToClock();
    }
    else if (screenState == STATE_OFF) {
        // The only way out of OFF (besides a touch) is the auto-wake.
        if (checkAutoWake()) autoWakeToClock();
    }
}

// =====================================================================
// DESK-CLOCK AUTO-WAKE
// =====================================================================
// Returns true when we've just entered a wake-minute (multiple of
// AUTO_WAKE_INTERVAL_MIN) that we haven't already woken on.
bool checkAutoWake() {
    if (AUTO_WAKE_INTERVAL_MIN <= 0) return false;

    time_t t = time(nullptr);
    TimeChangeRule* tcr;
    time_t local = cities[currentCity].tz->toLocal(t, &tcr);
    struct tm* tm_ = localtime(&local);
    int minuteOfDay = tm_->tm_hour * 60 + tm_->tm_min;

    if (minuteOfDay == lastAutoWakeMinute) return false;
    if ((minuteOfDay % AUTO_WAKE_INTERVAL_MIN) != 0) return false;

    lastAutoWakeMinute = minuteOfDay;
    return true;
}

void autoWakeToClock() {
    display.setBrightness(BRIGHTNESS_ON);
    screenState = STATE_CLOCK;
    lastActivityTime = millis();
    lastTimeStr[0] = '\0';      // force time redraw
    redrawAll();
}

// =====================================================================
// WEATHER
// =====================================================================
void updateWeather(int idx) {
    if (WiFi.status() != WL_CONNECTED) {
        setWeatherError(idx);
        return;
    }

    HTTPClient http;
    String url = String(WEATHER_HOST) + "/data/2.5/weather?q=" +
                 cities[idx].name + "&units=metric&appid=" + API_KEY;
    http.begin(url);
    int code = http.GET();

    if (code == 200) {
        // Filter: only parse the two fields we need (saves a lot of RAM)
        StaticJsonDocument<64> filter;
        filter["main"]["temp"] = true;
        filter["weather"][0]["description"] = true;

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(
            doc, http.getStream(), DeserializationOption::Filter(filter));

        if (!err) {
            float temp = doc["main"]["temp"] | NAN;
            const char* desc = doc["weather"][0]["description"] | "";
            if (!isnan(temp)) {
                dtostrf(temp, 4, 1, cities[idx].weatherTemp);
                copyAndCapitalize(desc, cities[idx].weatherDesc,
                                  sizeof(cities[idx].weatherDesc));
            } else {
                setWeatherError(idx);
            }
        } else {
            setWeatherError(idx);
        }
    } else {
        setWeatherError(idx);
    }
    http.end();
}

void setWeatherError(int idx) {
    strcpy(cities[idx].weatherTemp, "--");
    strcpy(cities[idx].weatherDesc, "Error");
}

// Capitalize first letter of each word, copy safely
void copyAndCapitalize(const char* src, char* dst, size_t dstSize) {
    bool capNext = true;
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dstSize - 1; i++) {
        char c = src[i];
        if (isSpace(c)) { capNext = true; }
        else if (capNext) { c = toupper(c); capNext = false; }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

// =====================================================================
// SUN / TIME-OF-DAY PHASE
// =====================================================================
// (TimePhase enum is forward-declared near the top of the file)

// Handles polar day / polar night where acos input is out of range.
// Returns: sunrise & sunset in local decimal hours (0..24).
// If sun never rises: both = -1.  If sun never sets: both = 25.
void computeSunTimes(int idx, struct tm* localTM, float& sunrise, float& sunset) {
    float lat = cities[idx].lat;
    int   doy = localTM->tm_yday;
    float dec = 23.45 * sin(radians(360.0 / 365.0 * (doy - 81)));
    float tanLat = tan(radians(lat));
    float tanDec = tan(radians(dec));
    float cosH   = -tanLat * tanDec;

    if (cosH > 1.0)  { sunrise = -1; sunset = -1; return; } // polar night
    if (cosH < -1.0) { sunrise = 25; sunset = 25; return; } // polar day

    float daylight = (2.0 / 15.0) * acos(cosH) * 180.0 / M_PI;
    sunrise = 12 - daylight / 2;
    sunset  = 12 + daylight / 2;
}

TimePhase getTimePhase(int idx, struct tm* localTM) {
    float sunrise, sunset;
    computeSunTimes(idx, localTM, sunrise, sunset);
    if (sunrise < 0) return PHASE_NIGHT;       // polar night
    if (sunrise > 24) return PHASE_MORNING;    // polar day

    float current = localTM->tm_hour + localTM->tm_min / 60.0;

    float dawnStart = sunrise - DAWN_BEFORE;
    float dawnEnd   = sunrise + DAWN_AFTER;
    float duskStart = sunset  - DUSK_BEFORE;
    float duskEnd   = sunset  + DUSK_AFTER;
    float lateStart = sunset  - LATEAFT_BEFORE;

    // Guard against overlapping windows (very short days near poles)
    if (lateStart < dawnEnd) lateStart = dawnEnd;
    if (duskStart < lateStart) duskStart = lateStart;

    if (current >= dawnStart && current < dawnEnd) return PHASE_DAWN;
    if (current >= dawnEnd   && current < lateStart) return PHASE_MORNING;
    if (current >= lateStart && current < duskStart) return PHASE_LATEAFT;
    if (current >= duskStart && current < duskEnd)   return PHASE_DUSK;
    return PHASE_NIGHT;
}

void colorsForPhase(TimePhase p, uint16_t& bg, uint16_t& fg) {
    switch (p) {
        case PHASE_DAWN:
            bg = display.color888(COLOR_DAWN_R, COLOR_DAWN_G, COLOR_DAWN_B);
            fg = COLOR_DAWN_FG; break;
        case PHASE_MORNING:
            bg = display.color888(COLOR_MORNING_R, COLOR_MORNING_G, COLOR_MORNING_B);
            fg = COLOR_MORNING_FG; break;
        case PHASE_LATEAFT:
            bg = display.color888(COLOR_LATEAFT_R, COLOR_LATEAFT_G, COLOR_LATEAFT_B);
            fg = COLOR_LATEAFT_FG; break;
        case PHASE_DUSK:
            bg = display.color888(COLOR_DUSK_R, COLOR_DUSK_G, COLOR_DUSK_B);
            fg = COLOR_DUSK_FG; break;
        case PHASE_NIGHT:
        default:
            bg = display.color888(COLOR_NIGHT_R, COLOR_NIGHT_G, COLOR_NIGHT_B);
            fg = COLOR_NIGHT_FG; break;
    }
}

// =====================================================================
// DRAWING
// =====================================================================
void redrawAll() {
    drawBackground(currentCity);
    drawCityInfo(currentCity);
    drawTime(currentCity);
}

void drawBackground(int idx) {
    time_t now = time(nullptr);
    TimeChangeRule* tcr;
    time_t local = cities[idx].tz->toLocal(now, &tcr);
    struct tm* tm_ = localtime(&local);

    TimePhase p = getTimePhase(idx, tm_);
    currentPhase = (int)p;
    colorsForPhase(p, currentBgColor, currentFgColor);

    display.fillScreen(currentBgColor);
    lastTimeStr[0] = '\0';  // force time redraw
}

void drawCityInfo(int idx) {
    display.setTextColor(currentFgColor, currentBgColor);

    time_t now = time(nullptr);
    TimeChangeRule* tcr;
    cities[idx].tz->toLocal(now, &tcr);

    // City
    display.setFont(fontCityName);
    display.setTextSize(TEXT_SIZE_CITY);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(cities[idx].name, SCREEN_W/2, POS_CITY_Y);
    display.setTextSize(1.0);

    // Country
    display.setFont(fontCountry);
    display.drawString(cities[idx].country, SCREEN_W/2, POS_COUNTRY_Y);

    // TZ
    display.setFont(fontTZ);
    display.drawString(tcr->abbrev, SCREEN_W/2, POS_TZ_Y);

    // Weather
    if (strcmp(cities[idx].weatherTemp, "--") != 0) {
        display.setFont(fontWeather);
        display.setTextDatum(textdatum_t::middle_left);
        display.drawString(cities[idx].weatherTemp,
                           POS_WEATHER_TEMP_X, POS_WEATHER_Y);

        int tw = display.textWidth(cities[idx].weatherTemp);
        display.drawCircle(POS_WEATHER_TEMP_X + tw + 6,
                           POS_DEG_CIRCLE_Y, 3, currentFgColor);
        display.setFont(fontTZ);
        display.drawString("C", POS_WEATHER_TEMP_X + tw + 12, POS_WEATHER_Y);

        display.setFont(fontWeather);
        display.setTextDatum(textdatum_t::middle_right);
        display.drawString(cities[idx].weatherDesc,
                           POS_WEATHER_DESC_X, POS_WEATHER_Y);
    }

    // Domain + WiFi label
    display.setFont(fontSmall);
    display.setTextDatum(textdatum_t::bottom_left);
    display.drawString(DOMAIN_TEXT, POS_DOMAIN_X, POS_DOMAIN_Y);
    display.setTextDatum(textdatum_t::bottom_right);
    display.drawString("wireless settings", POS_WIFI_X, POS_WIFI_Y);
}

void drawTime(int idx) {
    time_t now = time(nullptr);
    TimeChangeRule* tcr;
    time_t local = cities[idx].tz->toLocal(now, &tcr);

    char buf[6];
    struct tm* tm_ = localtime(&local);
    sprintf(buf, "%02d:%02d", tm_->tm_hour, tm_->tm_min);

    // Skip redraw if minute hasn't changed (eliminates flicker + saves CPU)
    if (strcmp(buf, lastTimeStr) == 0) return;
    strcpy(lastTimeStr, buf);

    display.setTextColor(currentFgColor, currentBgColor);

    // --- Time (HH:MM) ---
    display.setFont(fontTime);
    display.setTextSize(TEXT_SIZE_TIME);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(buf, SCREEN_W/2, POS_TIME_Y);
    display.setTextSize(1.0);

    // --- Date / Day of week (e.g. "Thu, Jun 04") ---
    char dateBuf[24];
    strftime(dateBuf, sizeof(dateBuf), "%a, %b %d", tm_);
    // Erase the previous date strip first (in case the new string is shorter)
    display.fillRect(0, POS_DATE_Y - 8, SCREEN_W, 16, currentBgColor);
    display.setFont(fontDate);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(dateBuf, SCREEN_W/2, POS_DATE_Y);
}

// =====================================================================
// TOUCH
// =====================================================================
void changeCity(int delta) {
    currentCity = (currentCity + delta + numCities) % numCities;
    saveCityIndex(currentCity);
    redrawAll();
    delay(TOUCH_DEBOUNCE);
}

void handleTouch() {
    lgfx::touch_point_t tp;
    if (!display.getTouch(&tp)) return;

    // Remap touch coordinates
    int tx = constrain(map(tp.y, 0,   230, 0, SCREEN_W), 0, SCREEN_W);
    int ty = constrain(map(tp.x, 308, 28,  0, SCREEN_H), 0, SCREEN_H);

    lastActivityTime = millis();

    if (screenState != STATE_CLOCK) { wakeFromScreenSaver(); return; }

    if (ty < TOUCH_TOP_MAX_Y) {
        // Cities are ordered East -> West (index 0 = easternmost).
        // Tap RIGHT -> go EAST -> previous index (-1)
        // Tap LEFT  -> go WEST -> next index (+1)
        if (tx > TOUCH_RIGHT_MIN_X)      changeCity(-1);   // east
        else if (tx < TOUCH_LEFT_MAX_X)  changeCity(+1);   // west
    } else if (tx > TOUCH_WIFI_MIN_X) {
        startWiFiConfig();
    }
}

// =====================================================================
// WIFI SETUP
// =====================================================================
void startWiFiConfig() {
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setFont(fontWiFiMsg);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString("Entering WiFi Setup...", SCREEN_W/2, 100);
    display.drawString("Connect to '" + String(WIFI_AP_NAME) + "'",
                       SCREEN_W/2, 140);

    unsigned long start = millis();
    while (millis() - start < WIFI_CANCEL_WINDOW) {
        lgfx::touch_point_t tp;
        if (display.getTouch(&tp)) { wakeFromScreenSaver(); return; }
        delay(50);
    }
    wm.startConfigPortal(WIFI_AP_NAME);
    ESP.restart();
}


// =====================================================================
// SCREENSAVER (Pac-Man Demo)
// =====================================================================
// Classic arcade-style attract mode: two scenes loop forever.
//   Scene 1: Pac-Man runs right-to-left eating dots, chased by a ghost.
//   Scene 2: A frightened ghost runs right-to-left, chased by Pac-Man
//            (who never catches him). Ghost colors and Y position are
//            randomized each scene.

// Pac-Man colors
#define PAC_YELLOW         0xFFE0    // bright yellow
#define PAC_WHITE          0xFFFF
#define PAC_DOT_COLOR      0xFFFF    // pellets are white
#define PAC_EYE_PUPIL      0x001F    // pupils are blue
#define PAC_BG             TFT_BLACK

// Classic Pac-Man ghost colors (Blinky/Pinky/Inky/Clyde)
const uint16_t GHOST_PALETTE_NORMAL[] = {
    0xF800,   // Blinky  - red
    0xFBB6,   // Pinky   - pink
    0x07FF,   // Inky    - cyan
    0xFC00    // Clyde   - orange
};
const int GHOST_PALETTE_NORMAL_COUNT = 4;

// Frightened ghost colors (cool tones, readable as "scared")
const uint16_t GHOST_PALETTE_SCARED[] = {
    0x001F,   // classic blue
    0x0013,   // dark blue
    0x381F,   // indigo/violet
    0x041F    // azure
};
const int GHOST_PALETTE_SCARED_COUNT = 4;

uint16_t randomGhostColor(bool frightened) {
    if (frightened) {
        return GHOST_PALETTE_SCARED[random(0, GHOST_PALETTE_SCARED_COUNT)];
    } else {
        return GHOST_PALETTE_NORMAL[random(0, GHOST_PALETTE_NORMAL_COUNT)];
    }
}

// Draw Pac-Man at (cx, cy) facing direction (-1 = left, +1 = right).
// mouthOpen is 0..1 (0 = closed, 1 = wide open).
void drawPacMan(int cx, int cy, int radius, int facing, float mouthOpen, uint16_t color) {
    // mouth wedge half-angle, in degrees (0 = closed mouth = full circle)
    float halfMouth = mouthOpen * 40.0;   // up to ±40°

    if (halfMouth < 1.0) {
        // Mouth essentially closed -- just a full circle
        display.fillCircle(cx, cy, radius, color);
        return;
    }

    // Facing right: mouth opens around 0°. Draw the body arc from
    // +halfMouth around to (360 - halfMouth).
    // Facing left: mouth opens around 180°. Draw arc from
    // (180 + halfMouth) around to (180 - halfMouth + 360) = (540 - halfMouth).
    // LovyanGFX fillArc sweeps from angle0 -> angle1 in positive direction.
    float a0, a1;
    if (facing > 0) {
        a0 = halfMouth;
        a1 = 360.0 - halfMouth;
    } else {
        a0 = 180.0 + halfMouth;
        a1 = 540.0 - halfMouth;
    }
    display.fillArc(cx, cy, 0, radius, a0, a1, color);
}

// Draw a classic Pac-Man ghost at (cx, cy). bodyColor is the ghost's hue.
// eyeDir = -1 (look left), +1 (look right), 0 (look forward).
// frightened = true draws a wavy "scared" mouth instead of normal pupils.
void drawGhost(int cx, int cy, int radius, uint16_t bodyColor, int eyeDir, bool frightened) {
    int top    = cy - radius;
    int bottom = cy + radius;
    int left   = cx - radius;
    int width  = radius * 2;

    // 1. Dome (top half) -- draw a full circle; bottom half overlaps body rect, no harm.
    display.fillCircle(cx, cy, radius, bodyColor);
    // 2. Rectangular body from middle down to (bottom - 3)
    display.fillRect(left, cy, width, radius - 3, bodyColor);

    // 3. Bottom skirt: 3 triangular bumps pointing down.
    //    The gaps between triangles stay black (background) automatically.
    int bumpW = width / 3;
    int skirtY = bottom - 3;
    for (int b = 0; b < 3; b++) {
        int bx0 = left + b * bumpW;
        int bx1 = bx0 + bumpW;
        int bxm = (bx0 + bx1) / 2;
        display.fillTriangle(bx0, skirtY,
                             bx1, skirtY,
                             bxm, skirtY + 3, bodyColor);
    }

    // 4. Eyes
    int eyeY  = cy - radius / 4;
    int eyeR  = max(2, radius / 4);
    int eyeDX = radius / 2;

    if (!frightened) {
        int pupilOffset = (eyeDir != 0) ? (eyeR / 2) * eyeDir : 0;
        // White eyes with blue pupils
        display.fillCircle(cx - eyeDX, eyeY, eyeR, PAC_WHITE);
        display.fillCircle(cx + eyeDX, eyeY, eyeR, PAC_WHITE);
        display.fillCircle(cx - eyeDX + pupilOffset, eyeY, eyeR / 2, PAC_EYE_PUPIL);
        display.fillCircle(cx + eyeDX + pupilOffset, eyeY, eyeR / 2, PAC_EYE_PUPIL);
    } else {
        // Frightened: small square eyes + zigzag mouth
        display.fillRect(cx - eyeDX - 1, eyeY - 1, 3, 3, PAC_WHITE);
        display.fillRect(cx + eyeDX - 1, eyeY - 1, 3, 3, PAC_WHITE);
        // Wavy mouth (zigzag horizontal line)
        int my = cy + radius / 4;
        for (int x = -radius + 4; x <= radius - 4; x += 4) {
            display.drawLine(cx + x,     my + 1, cx + x + 2, my - 1, PAC_WHITE);
            display.drawLine(cx + x + 2, my - 1, cx + x + 4, my + 1, PAC_WHITE);
        }
    }
}

// Erase a rectangular region with the screensaver background
void eraseRect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w > 0 && h > 0) display.fillRect(x, y, w, h, PAC_BG);
}

// Pick a random Y row, keeping the sprite fully on-screen.
// Returns center-Y for a sprite of given radius.
int randomSpriteY(int radius) {
    int minY = radius + PAC_Y_MARGIN;
    int maxY = SCREEN_H - radius - PAC_Y_MARGIN;
    return random(minY, maxY + 1);
}

void initPacScene1() {
    pac.scene = SCENE_CHASE;
    pac.facing = -1;                                // moving left
    int row = randomSpriteY(PAC_RADIUS);
    pac.pacX = SCREEN_W + PAC_RADIUS;               // start off right edge
    pac.pacY = row;
    pac.ghostX = pac.pacX + PAC_RADIUS * 4;         // ghost trails further right
    pac.ghostY = row;
    pac.dotY = row;
    pac.numDots = SCREEN_W / PAC_DOT_SPACING;
    pac.dotEaten = 0;
    pac.ghostFrightened = false;
    pac.ghostColor = randomGhostColor(false);
    pac.mouthPhase = 0;
    pac.sceneStartTime = millis();
}

void initPacScene2() {
    pac.scene = SCENE_REVENGE;
    pac.facing = +1;                                // moving right
    int row = randomSpriteY(PAC_RADIUS);
    pac.pacX = -PAC_RADIUS;                         // Pac-Man off left edge
    pac.pacY = row;
    pac.ghostX = pac.pacX + GHOST_LEAD;             // ghost runs ahead
    pac.ghostY = row;
    pac.dotY = row;
    pac.numDots = 0;                                // no dots in revenge scene
    pac.dotEaten = 0xFFFFFFFF;
    pac.ghostFrightened = true;
    pac.ghostColor = randomGhostColor(true);
    pac.mouthPhase = 0;
    pac.sceneStartTime = millis();
}

void initPacDemo() {
    display.fillScreen(PAC_BG);
    initPacScene1();
    lastFrameTime = millis();
}

void drawDots() {
    for (int i = 0; i < pac.numDots; i++) {
        if (pac.dotEaten & (1UL << i)) continue;
        int dx = PAC_DOT_SPACING / 2 + i * PAC_DOT_SPACING;
        display.fillCircle(dx, pac.dotY, PAC_DOT_SIZE, PAC_DOT_COLOR);
    }
}

void drawPacFrame() {
    unsigned long now = millis();

    // ---- Erase previous frame regions (just around sprites) ----
    int eraseR = PAC_RADIUS + 6;       // covers skirt animation + sub-pixel motion
    eraseRect((int)pac.pacX   - eraseR, (int)pac.pacY   - eraseR, eraseR * 2, eraseR * 2);
    eraseRect((int)pac.ghostX - eraseR, (int)pac.ghostY - eraseR, eraseR * 2, eraseR * 2);

    // ---- Update state ----
    pac.mouthPhase += 0.35;
    if (pac.mouthPhase > TWO_PI) pac.mouthPhase -= TWO_PI;

    switch (pac.scene) {
        case SCENE_CHASE: {
            pac.pacX   -= PAC_SPEED_CHASE;
            pac.ghostX -= GHOST_SPEED_CHASE;
            // Eat dots Pac-Man passes over
            for (int i = 0; i < pac.numDots; i++) {
                if (pac.dotEaten & (1UL << i)) continue;
                int dx = PAC_DOT_SPACING / 2 + i * PAC_DOT_SPACING;
                if (abs(dx - (int)pac.pacX) < PAC_RADIUS &&
                    abs(pac.dotY - (int)pac.pacY) < PAC_RADIUS) {
                    pac.dotEaten |= (1UL << i);
                }
            }
            // When ghost exits left edge, pause then start scene 2
            if (pac.ghostX < -PAC_RADIUS * 2) {
                pac.scene = SCENE_PAUSE_AFTER_CHASE;
                pac.sceneStartTime = now;
                display.fillScreen(PAC_BG);
            }
            break;
        }
        case SCENE_PAUSE_AFTER_CHASE: {
            if (now - pac.sceneStartTime > PAC_SCENE_PAUSE) {
                initPacScene2();
                display.fillScreen(PAC_BG);
            }
            return;  // nothing to draw during pause
        }
        case SCENE_REVENGE: {
            pac.pacX   += PAC_SPEED_REVENGE;
            pac.ghostX += GHOST_SPEED_REVENGE;
            // Ghost leads — when ghost exits right edge, pause then loop
            if (pac.ghostX > SCREEN_W + PAC_RADIUS * 2) {
                pac.scene = SCENE_PAUSE_AFTER_REVENGE;
                pac.sceneStartTime = now;
                display.fillScreen(PAC_BG);
            }
            break;
        }
        case SCENE_PAUSE_AFTER_REVENGE: {
            if (now - pac.sceneStartTime > PAC_SCENE_PAUSE) {
                initPacScene1();
                display.fillScreen(PAC_BG);
            }
            return;
        }
    }

    // ---- Draw dots (only in scene 1) ----
    if (pac.scene == SCENE_CHASE) drawDots();

    // ---- Draw sprites ----
    float mouthOpen = (sin(pac.mouthPhase) + 1.0) * 0.5;   // 0..1
    drawPacMan((int)pac.pacX, (int)pac.pacY, PAC_RADIUS, pac.facing, mouthOpen, PAC_YELLOW);

    drawGhost((int)pac.ghostX, (int)pac.ghostY, PAC_RADIUS,
              pac.ghostColor, pac.facing, pac.ghostFrightened);
}


void wakeFromScreenSaver() {
    display.setBrightness(BRIGHTNESS_ON);
    screenState = STATE_CLOCK;
    delay(100);
    lgfx::touch_point_t dummy;
    display.getTouch(&dummy);
    redrawAll();
    lastActivityTime = millis();
}

// =====================================================================
// PREFERENCES
// =====================================================================
void saveCityIndex(int idx) {
    prefs.begin("worldclock", false);
    prefs.putInt("city", idx);
    prefs.end();
}

int loadCityIndex() {
    prefs.begin("worldclock", true);
    int idx = prefs.getInt("city", DEFAULT_CITY_INDEX);
    prefs.end();
    if (idx < 0 || idx >= numCities) idx = DEFAULT_CITY_INDEX;
    return idx;
}
