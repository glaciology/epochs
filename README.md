# EPOCHS V1.0.0
**Timelapse Camera Firmware for SparkFun Redboard Artemis**

Open-source timelapse camera firmware for long-term field deployment. Designed for tundra monitoring and similar applications requiring battery-powered, unattended operation over weeks to months to years!

---

## Hardware

| Component | Part | Notes |
|-----------|------|-------|
| Microcontroller | SparkFun Redboard Artemis | Apollo3 / AM_HAL |
| GPS + SD Shield | Adafruit Ultimate GPS Logging Shield | PA6H / MTK3339 module |
| Camera | ArduCAM Mega 5MP | OV5642 sensor |
| Camera power switch | Pololu D36V6F5 5V Buck Regulator | EN pin controlled by firmware |
| microSD card | Class 10 / U1, 32GB recommended | FAT32, see below |

---

## Wiring

### Adafruit GPS Logging Shield
Stack directly onto the Redboard Artemis. The shield occupies:
- **SPI bus** (pins 11/12/13) — shared with ArduCAM
- **SD CS** — pin 10 (hard-wired on shield)
- **GPS serial** — direct-connect mode: GPS TX → D0 (Artemis RX), GPS RX → D1 (Artemis TX). SET DIP SWITCH ACCORDINGLY!

> Note: The shield has a solder jumper to route GPS serial to either hardware serial (pins 0/1) or software serial (pins 7/8). This firmware uses **direct/hardware serial mode** wired to pins 0/1.

### ArduCAM Mega
| ArduCAM Pin | Redboard Artemis Pin |
|-------------|----------------------|
| CS | D4 (configurable, see `PIN_CAM_CS`) |
| MOSI | D11 (shared SPI) |
| MISO | D12 (shared SPI) |
| SCK | D13 (shared SPI) |
| VCC | Pololu D36V6F5 output (3.3V) |
| GND | GND |

### Pololu D36V6F5 Camera Power Switch
| Pololu Pin | Connection |
|------------|------------|
| VIN | Battery / main supply |
| VOUT | ArduCAM VCC |
| GND | GND |
| EN | D3 (Artemis GPIO, `PIN_CAM_POWER`) |

The EN pin is active-high with an internal pull-up. The Artemis drives it LOW during sleep to cut camera power (<2 μA regulator draw). Drive HIGH to enable.

### Pin Summary
| Pin | Function |
|-----|----------|
| D0 | GPS TX (hardware serial RX) |
| D1 | GPS RX (hardware serial TX) |
| D3 | Camera power enable (Pololu EN) |
| D4 | ArduCAM CS |
| D10 | SD card CS (shield default) |
| D11 | SPI MOSI (shared) |
| D12 | SPI MISO (shared) |
| D13 | SPI SCK (shared) |
| D19 | Onboard LED |
| D22 | Battery monitor enable (optional) |
| D35 | Battery voltage ADC (optional) |

---

## LED Indicators

Note: With prototype, this functionality does not yet work, until we make a custom PCB that has a toggle LED (Redboard does not, sadly).
| Pattern | Meaning |
|---------|---------|
| 10 blinks on boot | Setup complete |
| 6 rapid blinks | CONFIG.TXT not found — defaults used, template written |
| 2 blinks repeating | SD card failed — check card and reboot |
| 1 blink per WDT cycle | Sleeping normally (~every 12s) |

---

## Log Modes

### Mode 1 — Interval
Takes a photo every N seconds. Sleeps between shots.

- Set `LOG_MODE=1`
- Set `PHOTO_INTERVAL_S` to desired interval
- Minimum interval: 30 seconds (enforced by firmware to prevent WDT issues)

### Mode 2 — Summer/Winter
Takes photos at a faster rate during summer months and a slower rate during winter. Useful for capturing seasonal glacier dynamics without wasting battery in winter.

- Set `LOG_MODE=2`
- Set `SUMMER_INTERVAL_S` for the active season interval
- Set `WINTER_INTERVAL_S` for the dormant season interval
- Define the summer window with `SUMMER_START_MONTH`, `SUMMER_END_MONTH`, `SUMMER_START_DAY`, `SUMMER_END_DAY`
- Both intervals enforce the 30-second minimum floor

---

## CONFIG.TXT

Place `CONFIG.TXT` in the root of the microSD card. If not found, the firmware writes a default template and uses hardcoded defaults.

```
LOG_MODE(1=interval,2=season)=1
PHOTO_INTERVAL_S(mode1,min30s)=3600
SUMMER_INTERVAL_S(mode2,min30s)=1800
WINTER_INTERVAL_S(mode2,min30s)=86400
SUMMER_START_MONTH=4
SUMMER_END_MONTH=9
SUMMER_START_DAY=21
SUMMER_END_DAY=21
LED_INDICATORS(0-1)=1
MEASURE_BATTERY(0-1)=0
BAT_SHUTDOWN_V(volts)=3.50
GPS_SYNC_TIMEOUT_S=300
```

### Parameter Reference

| Parameter | Description | Default |
|-----------|-------------|---------|
| `LOG_MODE` | 1 = interval, 2 = summer/winter | 1 |
| `PHOTO_INTERVAL_S` | Seconds between photos (mode 1) | 3600 |
| `SUMMER_INTERVAL_S` | Seconds between photos in summer (mode 2) | 1800 |
| `WINTER_INTERVAL_S` | Seconds between photos in winter (mode 2) | 86400 |
| `SUMMER_START_MONTH` | Month summer begins (1–12, inclusive) | 4 |
| `SUMMER_END_MONTH` | Month summer ends (1–12, inclusive) | 9 |
| `SUMMER_START_DAY` | Day of start month summer begins | 21 |
| `SUMMER_END_DAY` | Day of end month summer ends | 21 |
| `LED_INDICATORS` | 0 = LEDs off during sleep/log, 1 = on | 1 |
| `MEASURE_BATTERY` | 0 = disabled, 1 = enabled (requires hardware) | 0 |
| `BAT_SHUTDOWN_V` | Battery voltage below which system sleeps | 3.50 |
| `GPS_SYNC_TIMEOUT_S` | Seconds to wait for GPS fix on boot | 300 |

> **Important:** Do not use `>=` or `<=` in the key names — the `=` character is used as the delimiter. The firmware parses on the first `=` only.

---

## GPS / RTC Sync

On first power-up, the firmware attempts to sync the Artemis RTC from the GPS module. The GPS module (PA6H/MTK3339) has a coin-cell battery on the shield that maintains its last known time and satellite almanac between power cycles, enabling fast re-acquisition outdoors (~5–15 seconds with almanac vs. ~45–90 seconds cold).

**Sync behaviour:**
- If the Artemis RTC already holds a valid year (≥ 2024), GPS sync is skipped — the coin cell kept the RTC running across sleep cycles
- If RTC is invalid, firmware waits up to `GPS_SYNC_TIMEOUT_S` for a confirmed GPS fix (GPRMC status `A`)
- If no fix is obtained (e.g. indoors), a warning is logged and the coin-cell RTC time is used — **firmware does not halt**
- GPS is put into standby mode after sync to save power

**First deployment outdoors:** place the unit outside with a clear sky view and power on. GPS sync typically completes within 1–2 minutes. After that, the coin cell handles all subsequent boots.

---

## SD Card Recommendations

| Parameter | Recommendation |
|-----------|----------------|
| Capacity | 32 GB (FAT32) |
| Speed class | Class 10 / U1 minimum |
| Format | FAT32 (exFAT not recommended for reliability) |
| Brand | SanDisk, Samsung, or Lexar industrial-grade preferred for cold climates |
### Storage Estimates (5MP WQXGA2, ~900 KB/image)

| Interval | Images/day | MB/day | Days on 32GB |
|----------|------------|--------|--------------|
| 30s | 2880 | ~2520 MB | ~13 days |
| 5 min | 288 | ~252 MB | ~130 days |
| 1 hour | 24 | ~21 MB | ~1560 days |
| Summer 30min / Winter 6hr | ~50/day avg | ~44 MB | ~745 days |

> A 32GB card at 1-hour intervals lasts years. Size the interval to your deployment duration.

---

## File Structure on SD Card

```
/
├── CONFIG.TXT              ← user settings (edit this)
├── dbg_CA392551.csv        ← debug log (named by chip ID)
└── IMG_20260519_120000_0001.jpg   ← images (timestamped)
```

Image filenames: `IMG_YYYYMMDD_HHMMSS_NNNN.jpg`
- `YYYYMMDD` — date from RTC at capture time
- `HHMMSS` — time from RTC at capture time
- `NNNN` — sequential counter (resets on power cycle)

---

## Debug Log (CSV)

The debug file records one row per photo cycle. Columns:

```
datetime, counter, onlineSD, onlineCam, rtcSync,
photos, wdtMax, writeFails, closeFails, logMode,
errorCode, lowBat, tempC, battV, [reset codes]
```

Error codes logged in the `errorCode` field:

| Code | Meaning |
|------|---------|
| `ok` | Successful photo cycle |
| `CAM_CAPTURE_ERR` | `takePicture()` returned non-success |
| `CAM_ZERO_LEN` | Camera returned 0-byte image |
| `CAM_FILE_ERR` | Could not open image file on SD |
| `CAM_WRITE_ERR` | SD write failed during FIFO drain |
| `CAM_CORRUPT` | JPEG verification failed (bad header/footer) |
| `RTC_NO_FIX` | GPS sync timed out — coin cell time used |

---

## Dependencies

Install via Arduino Library Manager:

| Library | Version | Source |
|---------|---------|--------|
| Apollo3 Arduino Core | 1.2.3 | SparkFun boards manager |
| SdFat | 2.1.0 | Library manager |
| MicroNMEA | latest | Library manager |
| arducam_mega | latest | https://github.com/ArduCAM/ArduCAM_Mega |

### Library Patches Required

**1. `ArducamCamera.c` — remove SPI from constructor:**

In `cameraInit()`, comment out `arducamSpiBegin()` and `arducamSpiCsPinLow()` to prevent SPI transactions before `setup()` runs.

**2. `Platform.h` — enable block SPI transfer:**

Add to `Platform.h`:
```c
#define arducamSpiBlockTransfer arducamSpiBlockTransfer
void arducamSpiBlockTransfer(uint8_t val, uint8_t* buf, uint32_t len);
```

**3. `arducam_hal.cpp` — Apollo3 HAL shim:**

Include the provided `arducam_hal.cpp` in your sketch folder. This resolves C/C++ linkage issues with the Apollo3 core and provides the CS pin and SPI transfer functions the library needs.

---

## Power Budget (Approximate)

**Runtime estimates at 1-hour interval (3s active per cycle, 7000 mAh LiPo):**
**Without custom PCB, measured photo current 200mA for 3 seconds, 30 mA for sleep**

| State | Current Draw |
|-------|-------------|
| Active capture cycle | ~160 mA for ~3 seconds |
| Sleep (current hardware) | ~9 mA (SD card + GPS quiescent draw) |
| Sleep (custom PCB target) | ~2 μA (SD card and GPS switched off via MOSFET) |

**Runtime estimates (3s active per cycle, 7000 mAh LiPo):**

| Interval | Hardware | Avg current | Est. runtime |
|----------|----------|------------|--------------|
| 30 min | Current (Redboard + shield) | ~9.27 mA | ~31 days |
| 30 min | Custom PCB (target) | ~0.027 mA | ~30 years |
| 1 hour | Current (Redboard + shield) | ~9.13 mA | ~32 days |
| 1 hour | Custom PCB (target) | ~0.013 mA | ~60 years |
| 5 min | Current (Redboard + shield) | ~9.80 mA | ~30 days |
| 5 min | Custom PCB (target) | ~0.160 mA | ~5 years |

---

## Troubleshooting

**SD card fails to initialize:**
- Wait 2–3 seconds after power-on before judging — the card needs rail stabilisation time
- Try a different card; some cards are incompatible with SdFat at 24 MHz
- Reduce `SD_SCK_MHZ(24)` to `SD_SCK_MHZ(16)` in the firmware if issues persist

**WDT resets during operation:**
- Ensure `MIN_INTERVAL_S` is at least 30 seconds
- At 5MP resolution, a capture cycle takes ~3–4 seconds; intervals below 30s risk WDT collision

---

## License

Open source. See LICENSE file.
