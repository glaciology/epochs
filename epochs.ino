/*
  CamLogger — Timelapse Camera Firmware
  Artemis + Adafruit GPS Logging Shield + ArduCAM

  Hardware:
  - SparkFun Redboard Artemis (Apollo3)
  - Adafruit GPS Logging Shield (MTK3339 GPS + microSD on SPI)
  - ArduCAM Mega (arducam_mega library) on a second SPI bus
  - Optional battery voltage divider circuit (enable via config)

  Log Modes:
  - Mode 1 : Photo every N seconds (interval mode), sleeps between shots. Floor: 30s.
  - Mode 2 : Summer/Winter — every summerInterval seconds in summer,
             every winterInterval seconds in winter. Both floors: 30s.

  GPS / RTC notes:
  - RTC sync attempted ONCE on first power-up using GPS NMEA (GPRMC / GPGGA).
  - Coin-cell backed RTC holds time across sleep cycles; sync is NOT repeated.
  - If GPS fix is unavailable (indoor), a warning is logged and the coin-cell
    time is used — firmware does NOT halt.

  SPI bus:
  - Single shared SPI bus (default SPI object) for both SD and ArduCAM Mega.
  - Devices are selected independently via their own CS pins.
  - SD CS : PIN_SD_CS (Adafruit shield default = 10)
  - Cam CS: PIN_CAM_CS — choose any free GPIO, e.g. D2, D3, or D4.

  GPS Serial:
  - SoftwareSerial on pins 8 (RX) / 7 (TX) per Adafruit GPS shield documentation.

  WDT: 12-second interrupt, 24-second reset (mirrors OGRE firmware).

  Dependencies:
  - Apollo3 Arduino Core v1.2.3
  - SdFat Library v2.1.0
  - MicroNMEA Library (https://github.com/stevemarple/MicroNMEA)
  - arducam_mega Library (https://github.com/ArduCAM/ArduCAM_Mega)

  Attributions:
  - Sleep / WDT / RTC structure adapted from SparkFun OGRE firmware (D. Pickell, 2026)
    and A. Garbo GVMS. See LICENSE / README.
*/

// ─────────────────────────────────────────────────────────────────────────────
//  VERSION
// ─────────────────────────────────────────────────────────────────────────────
#define SOFTWARE_VERSION   "V1.0.0"
#define STAT_REGISTER_ADDRESS  0x4FFFF000
#define UNIQUE_ID_ADDRESS       0x40020004
volatile uint32_t *unique_chip_id = (uint32_t *)UNIQUE_ID_ADDRESS;

// ─────────────────────────────────────────────────────────────────────────────
//  LIBRARIES
// ─────────────────────────────────────────────────────────────────────────────
#include <SPI.h>
#include <WDT.h>
#include <RTC.h>
#include <SdFat.h>
#include <Arducam_Mega.h>      // https://github.com/ArduCAM/ArduCAM_Mega

// ─────────────────────────────────────────────────────────────────────────────
//  GPS SERIAL — uncomment ONE block
// ─────────────────────────────────────────────────────────────────────────────

// GPS — SoftwareSerial on pins 8 (RX from GPS TX) / 7 (TX to GPS RX)
// Per Adafruit GPS Logging Shield documentation (jumper set to SW serial side).
#include <SoftwareSerial.h>
#define GPS_SS_RX  1
#define GPS_SS_TX  0
SoftwareSerial gpsSerial(GPS_SS_RX, GPS_SS_TX);
//Adafruit_GPS GPS(&Serial1);
//Adafruit_GPS GPS(&gpsSerial);
#include <MicroNMEA.h>
char nmeabuffer[200];
MicroNMEA nmea(nmeabuffer, sizeof(nmeabuffer));

// ─────────────────────────────────────────────────────────────────────────────
//  PIN DEFINITIONS
// ─────────────────────────────────────────────────────────────────────────────

// --- SD card (Adafruit GPS Logging Shield) ---
const byte PIN_SD_CS          = 10;       // Shield hard-wired default

// --- ArduCAM Mega — any free GPIO works; D4 used here as a safe default ---
// Avoid D0/D1 (Serial), D7/D8 (GPS SoftwareSerial), D10 (SD CS), D11-D13 (SPI).
const byte PIN_CAM_CS         = 4;        // *** change to D2, D3, or D4 as needed ***
const byte PIN_CAM_POWER      = 3;        // GPIO to switch camera Vcc (active HIGH = on)

// --- Onboard LED ---
const byte LED                = 19;       // Redboard Artemis built-in LED

// --- Battery monitoring (optional) ---
const byte BAT                = 35;       // Analog input
const byte BAT_CNTRL          = 22;       // Drive HIGH to enable voltage divider

// ─────────────────────────────────────────────────────────────────────────────
//  SPI BUS
// ─────────────────────────────────────────────────────────────────────────────
// Both the SD card and the ArduCAM Mega share the default SPI bus.
// Each device has its own CS pin; only one is asserted at a time.
// SdFat and arducam_mega both call SPI.beginTransaction() internally,
// so there is no conflict as long as CS lines are never overlapped.
// MHZ: 24
#define SD_CONFIG SdSpiConfig(PIN_SD_CS, SHARED_SPI, SD_SCK_MHZ(8), &SPI)

// ─────────────────────────────────────────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────────────────────────────────────────
SdFs      sd;
APM3_RTC  rtc;
APM3_WDT  wdt;
FsFile    imageFile;
FsFile    debugFile;
FsFile    configFile;

// ArduCAM Mega — the library takes the CS pin and manages its own SPI transactions.
// Both SD and camera share the default SPI bus; CS pins keep them from conflicting.
Arducam_Mega  cam(PIN_CAM_CS);

// ─────────────────────────────────────────────────────────────────────────────
//  USER DEFAULT CONFIGURATION  (overridden by CONFIG.TXT on SD if present)
// ─────────────────────────────────────────────────────────────────────────────

// LOG MODE
byte logMode              = 1;        // 1 = interval, 2 = summer/winter

// MODE 1: interval (seconds). Minimum enforced: MIN_INTERVAL_S
uint32_t photoInterval    = 10;       // take a photo every N seconds

// MODE 2: summer/winter interval (seconds). Both floors: MIN_INTERVAL_S
uint32_t summerInterval   = 10;       // photo every N seconds during summer
uint32_t winterInterval   = 3600;     // photo every X seconds during winter
byte     startMonth       = 4;        // summer start month (inclusive)
byte     endMonth         = 9;        // summer end month   (inclusive)
byte     startDay         = 21;       // summer start day   (inclusive)
byte     endDay           = 21;       // summer end day     (inclusive)

// FEATURE FLAGS
bool  ledBlink            = true;     // blink LED during activity
bool  measureBattery      = false;    // requires battery divider circuit on BAT/BAT_CNTRL pins

// BATTERY PARAMETERS (only used if measureBattery == true)
float gain                = 17.2;    // 68k/10k voltage divider
float offset_v            = 0.23;
float shutdownThreshold   = 3.5;     // single-cell LiPo low-voltage cutoff

// GPS / RTC SYNC
uint32_t gpsSyncTimeoutMs = 1UL * 10UL * 1000UL;  // 30 second max wait for fix

// ─────────────────────────────────────────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
#define MIN_INTERVAL_S    10          // absolute minimum sleep interval (seconds)
#define CONFIG_LINE_COUNT 12          // expected lines in CONFIG.TXT

// ─────────────────────────────────────────────────────────────────────────────
//  GLOBAL STATE
// ─────────────────────────────────────────────────────────────────────────────
volatile bool wdtFlag         = false;
volatile bool alarmFlag       = true;   // true on boot so we take a photo immediately
bool          summerMode      = false;  // updated each wake cycle in mode 2

uint32_t      cachedChipId    = 0;
char          debugFileName[20] = "";
char          line[120];

// counters (debug)
unsigned int  syncFailCounter  = 0;
unsigned int  writeFailCounter = 0;
unsigned int  closeFailCounter = 0;
unsigned int  lowBatCounter    = 0;
unsigned int  debugCounter     = 0;
unsigned int  photoCounter     = 0;
volatile int  wdtCounter       = 0;
volatile int  wdtCounterMax    = 0;

struct {
  bool uSD        = false;
  bool cam        = false;
  bool rtcSync    = false;
  bool chargedBat = true;
} online;

// ─────────────────────────────────────────────────────────────────────────────
//  DEBUG MACROS
// ─────────────────────────────────────────────────────────────────────────────
#define DEBUG true

#if DEBUG
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  INTERRUPT HANDLERS
// ─────────────────────────────────────────────────────────────────────────────
extern "C" void am_watchdog_isr(void) {
  wdt.clear();
  if (wdtCounter < 5) wdt.restart();
  wdtFlag = true;
  wdtCounter++;
  if (wdtCounter > wdtCounterMax) wdtCounterMax = wdtCounter;
}

extern "C" void am_rtc_isr(void) {
  am_hal_rtc_int_clear(AM_HAL_RTC_INT_ALM);
  alarmFlag = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("***WELCOME TO EPOCHS LOGGER " SOFTWARE_VERSION "***");
  cachedChipId = *unique_chip_id;
  Serial.print("Unique Chip ID: ");
  Serial.println(cachedChipId, HEX);
  pinMode(LED, OUTPUT);

  configureWdt();
//  checkBattery();
  initializeBuses();
  configureSD();
  createDebugFile();
  getConfig();
  syncRtcFromGps();       // once only; continues on failure (coin cell fallback)
  blinkLed(10, 80);       // 10 blinks = setup complete
  DBGLN("Info: SETUP COMPLETE");
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  if (alarmFlag) {
    petDog();
//    checkBattery();

    Serial.print("On: ");
    printDateTime();
    
    if (!online.uSD) {
      initializeBuses(); 
      configureSD();
    } 
    takePhoto();
    logDebug("ok");
    configureNextAlarm();
    deinitializeBuses();

    Serial.print("Off: ");
    printDateTime(); 
  }

  petDog();
  if (ledBlink) blinkLed(1, 100);
  goToSleep();
}

// ─────────────────────────────────────────────────────────────────────────────
//  WDT
// ─────────────────────────────────────────────────────────────────────────────
void configureWdt() {
  wdt.configure(WDT_1HZ, 12, 24);
  wdt.start();
}

void petDog() {
  wdt.restart();
  wdtFlag   = false;
  wdtCounter = 0;
}
