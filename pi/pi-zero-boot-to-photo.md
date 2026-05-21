# Pi Zero 2W: Boot-to-Photo + Witty Pi 4 Setup

This document captures the working configuration for a Raspberry Pi Zero 2W with the HQ Camera (IMX477) that boots, takes a photo, halts, and is then woken at a configurable interval by a Witty Pi 4 power management board. The cycle repeats indefinitely on battery power.

Starting point on stock image (Pi Zero W): **3 minutes 6 seconds** boot to photo.
Final result (Pi Zero 2W): **~15.5 seconds** boot to photo, **~20 seconds total ON time** per cycle including capture, sync, and shutdown.

---

## Hardware

- Raspberry Pi Zero 2W (quad-core ARMv8, 512 MB RAM) — Pi Zero W also works but boots ~4x slower
- Raspberry Pi HQ Camera (Sony IMX477)
- Witty Pi 4 (UUGear) — provides RTC, scheduled power on/off, e-latching power switch
- USB-to-serial (UART) adapter for headless access via GPIO pins 8/10/6
- microSD card (Raspberry Pi OS Trixie Lite installed)
- Battery + 5V or 6-30V DC power source connected to Witty Pi (NOT to the Pi directly)

---

## High-level architecture

```
[Power on by Witty Pi RTC alarm]
         ↓
[Pi boots ~15.5s with stripped-down systemd]
         ↓
[boot-capture.service runs capture.sh]
         ↓
[capture.sh reads RTC time, takes photo, verifies saved, runs poweroff]
         ↓
[Witty Pi cuts power ~1s after TXD goes LOW]
         ↓
[Witty Pi RTC waits configured interval]
         ↓
[Power on by Witty Pi RTC alarm] — repeat
```

Key design choices:
- **Witty Pi schedule** uses `ON ... WAIT` so it never forces a shutdown — the capture script controls when to halt
- **`boot-capture.service`** starts as early as `local-fs.target` (~6.4s into boot) rather than waiting for full system initialization
- **Cycle interval** is set in the Witty Pi schedule script (e.g., `OFF M5` for 5 minutes between cycles)
- **Timestamps are read directly from the Witty Pi RTC over I2C** to avoid stale Linux system clock issues

---

## Step-by-step replication

### Step 1: Flash Raspberry Pi OS Lite Trixie to SD card

Use Raspberry Pi Imager. Lite is required: desktop variants pull in many additional services that block boot.

### Step 2: Pre-boot config — edit files on SD card before first boot

Mount the SD card on a desktop computer and edit `/boot/firmware/config.txt` and `/boot/firmware/cmdline.txt` (see sections below). This avoids needing to boot with HDMI/keyboard at all.

### Step 3: First boot and confirm UART access

Wire up UART:
- Pi pin 8 (GPIO14 / TX) → USB-serial RX
- Pi pin 10 (GPIO15 / RX) → USB-serial TX
- Pi pin 6 (GND) → USB-serial GND

Connect at **115200 baud** with PuTTY, `screen`, or `minicom`.

For `screen` on macOS/Linux:
```
screen -ls                              # list active sessions
screen -X -S <session> quit             # kill an active one if needed
screen /dev/tty.usbserial-XXX 115200    # connect
```

UART is handled at the kernel level — works even when networking, SSH, and SystemD services are all disabled. Confirm UART works *before* disabling network services or you can lock yourself out and have to pull the SD card.

### Step 4: Enable networking temporarily to install Witty Pi software

```bash
sudo systemctl start wpa_supplicant
sudo systemctl start NetworkManager
sudo nmtui   # connect to your WiFi
ping -c 3 google.com   # confirm
```

### Step 5: Install Witty Pi 4 software (with Pi off, Witty Pi NOT yet mounted)

```bash
cd ~
wget https://www.uugear.com/repo/WittyPi4/install.sh
sudo bash install.sh
sudo reboot
```

Important: **run install in your home directory** (`/home/epochs2/`). The install script places files in whatever directory you run it from. If you accidentally run it elsewhere, see "Fix Witty Pi install path" below.

### Step 6: Power off, mount Witty Pi 4 on GPIO header, power on via Witty Pi

After this point, **always power the Pi via the Witty Pi** (USB-C, XH2.54 connector, or VIN header on Witty Pi), NOT direct to the Pi.

Verify communication:
```bash
sudo i2cdetect -y 1
```
Should show `0x08` (Witty Pi MCU).

```bash
sudo /home/epochs2/wittypi/wittyPi.sh
```
Confirm menu loads and shows current temperature, system time, RTC time.

### Step 7: Set Witty Pi power cut delay to 1 second

Default is 7 seconds (battery waste). With `sync` before poweroff, 1s is safe:

```bash
sudo i2cset -y 1 0x08 21 10    # register 21, value 10 = 1.0 seconds
sudo i2cget -y 1 0x08 21       # verify (should return 0x0a)
```

This persists in the Witty Pi MCU's flash and survives reboots/power cuts.

### Step 8: Set "Default state when powered" to ON

So the Pi auto-starts when external power is connected:

```bash
sudo /home/epochs2/wittypi/wittyPi.sh
# choose 11 (View/change other settings) → 1 (Default state when powered) → set to 1 (ON)
```

### Step 9: Strip down systemd and disable cloud-init

See "Disabled services" section below.

### Step 10: Set up camera capture pipeline

See "Camera capture pipeline" section below.

### Step 11: Create Witty Pi schedule

See "Witty Pi schedule script" section below.

### Step 12: Disable SSH (replaced by UART)

```bash
sudo systemctl disable ssh
sudo systemctl disable sshswitch
```

Now deploy.

---

## `/boot/firmware/config.txt`

```ini
[all]
# Camera - auto-detect disabled, overlay specified manually
camera_auto_detect=0
display_auto_detect=0
dtoverlay=imx477

# HDMI - disable all probing
hdmi_blanking=2
hdmi_ignore_edid=0xa5000080
hdmi_ignore_cec_init=1
hdmi_ignore_cec=1
disable_fw_kms_setup=1
disable_overscan=1
enable_tvout=0

# I2C probing - disable all
force_eeprom_read=0
disable_poe_fan=1
ignore_lcd=1
disable_touchscreen=1

# Audio off
dtparam=audio=off

# Boot speed
boot_delay=0
disable_splash=1
arm_boost=1

# GPU memory for camera
gpu_mem=128

dtparam=i2c1=on
dtparam=i2c_arm=on
enable_uart=1
dtoverlay=disable-bt
```

Important: all entries must be under `[all]` — entries above the `[all]` section may not apply correctly on all Pi models and can cause the camera overlay to silently fail.

Reasoning:

- **`camera_auto_detect=0` + `dtoverlay=imx477`** — skip auto-detection and hardcode the camera. Note: `vcgencmd get_camera` will still report `detected=0` even when the camera is working — that's a legacy command that doesn't understand libcamera. Use `rpicam-jpeg -o test.jpg` to confirm functionality.
- **`display_auto_detect=0`, all HDMI options, `disable_fw_kms_setup=1`** — no display attached, so don't waste time probing display hardware.
- **`disable_touchscreen=1`, `ignore_lcd=1`, `disable_poe_fan=1`, `force_eeprom_read=0`** — disable I2C probes for hardware that isn't present.
- **`dtparam=audio=off`** — no audio used.
- **`boot_delay=0`** — removes bootloader delay window.
- **`disable_splash=1`** — removes rainbow GPU splash screen.
- **`gpu_mem=128`** — HQ Camera needs ≥128 MB GPU memory.
- **`enable_uart=1`** — turns on hardware UART for serial console. Critical: TXD (GPIO-14) is what Witty Pi monitors to detect shutdown.
- **`dtoverlay=disable-bt`** — frees the proper hardware UART (PL011) from the Bluetooth chip; without this, the serial console uses the slower clock-coupled mini-UART.

---

## `/boot/firmware/cmdline.txt`

Single line:

```
console=serial0,115200 console=tty1 root=PARTUUID=ebf780e1-02 rootfstype=ext4 fsck.repair=yes rootwait quiet cfg80211.ieee80211_regdom=US
```

Update the `PARTUUID` to match your SD card (find it via `sudo blkid`).

Reasoning:
- **`console=serial0,115200`** — routes kernel/console to UART for serial access.
- **`console=tty1`** — secondary console (harmless).
- **`quiet`** — suppresses kernel boot output to reduce serial-port-bound logging time.
- **`rootwait`** — waits for SD card root partition to appear before mounting (necessary).

Removed compared to stock image:
- **`modules-load=dwc2,g_ether`** — was for USB Ethernet gadget mode; not needed without network.
- **`ds=nocloud;...`** — cloud-init parameters, removed with cloud-init purge.

---

## `/etc/modules-load.d/camera.conf`

New file:

```
imx477
bcm2835_unicam_legacy
bcm2835_v4l2
bcm2835_isp
```

Forces camera-related kernel modules to load as early as possible via `systemd-modules-load.service`. Without this, modules loaded later (after udev triggered them), delaying sensor detection.

---

## Disabled services

The stock Raspberry Pi OS Lite Trixie image includes cloud-init (unusually for an embedded image). It was contributing ~14-18 seconds to boot on the Zero 2W.

```bash
# Remove cloud-init entirely (not needed for embedded use)
sudo systemctl disable cloud-init-local cloud-init-network cloud-final
sudo apt purge cloud-init rpi-cloud-init-mods -y
sudo rm -rf /etc/cloud /var/lib/cloud

# Disable services that block boot waiting on network/discovery
sudo systemctl disable NetworkManager
sudo systemctl disable NetworkManager-wait-online
sudo systemctl disable wpa_supplicant
sudo systemctl disable avahi-daemon
sudo systemctl disable systemd-timesyncd
sudo systemctl disable systemd-rfkill
sudo systemctl mask systemd-rfkill.socket
sudo systemctl disable rpi-eeprom-update
sudo systemctl disable keyboard-setup

# Additional services identified as unnecessary
sudo systemctl disable e2scrub_reap
sudo systemctl disable sshswitch
sudo systemctl disable systemd-binfmt
sudo systemctl mask modprobe@drm.service

# Once UART is confirmed working, disable SSH
sudo systemctl disable ssh

sudo apt autoremove -y
```

| Service | Time saved | Why disabled |
|---|---|---|
| cloud-init (all 4 services) | ~14-18s | Cloud image provisioning. Useless on embedded. |
| `NetworkManager` | ~14s | Adds up to 60s waiting for DHCP. No network needed. |
| `NetworkManager-wait-online` | ~1.3s | Waits for NM to be online. Pointless without NM. |
| `wpa_supplicant` | ~300ms | WiFi authentication. Not needed. |
| `avahi-daemon` | ~380ms | mDNS / `.local` hostname resolution. |
| `systemd-timesyncd` | ~360ms | NTP. Witty Pi RTC provides time instead. |
| `systemd-rfkill` | ~100ms | Radio kill switch handler. |
| `rpi-eeprom-update` | ~100ms | Firmware update checker. |
| `keyboard-setup` | ~714ms | No keyboard attached. |
| `ssh` / `sshswitch` | ~725ms | Replaced by UART for access. |
| `e2scrub_reap` | ~530ms | Filesystem maintenance scrubbing. |
| `systemd-binfmt` | ~350ms | Cross-arch binary execution. |
| `modprobe@drm` | ~310ms | DRM display driver. No display. |

---

## Fix Witty Pi install path (if needed)

The Witty Pi installer puts files in the directory you ran it from, and hardcodes that path into `/etc/init.d/wittypi`. If you ran it somewhere other than `~`, fix the init script:

```bash
sudo sed -i 's|/wrong/path/wittypi/|/home/epochs2/wittypi/|g' /etc/init.d/wittypi
sudo systemctl restart wittypi
sudo systemctl status wittypi
```

You can also `mv` the `wittypi/` and `uwi/` directories to the right location and update the init script to match.

---

## Camera capture pipeline

### `/etc/systemd/system/boot-capture.service`

```ini
[Unit]
Description=Boot photo capture
After=local-fs.target
DefaultDependencies=no

[Service]
Type=oneshot
ExecStart=/home/epochs2/capture.sh
User=root
RemainAfterExit=no

[Install]
WantedBy=multi-user.target
```

Reasoning:
- **`After=local-fs.target`** — starts as soon as local filesystems are mounted (~6.4s), without waiting for the rest of sysinit (~6.8s) or user sessions (~7.5s).
- **`DefaultDependencies=no`** — prevents systemd from adding implicit ordering dependencies.
- **`User=root`** — avoids waiting for `user@1000.service` (~1s saving). The script needs root for `/sbin/poweroff` anyway.
- **`Type=oneshot`** — service is done when script exits.

Enable:
```bash
sudo systemctl daemon-reload
sudo systemctl enable boot-capture.service
```

### `/home/epochs2/capture.sh`

```bash
#!/bin/bash

PHOTO_DIR=/home/epochs2/photos
LOG=/home/epochs2/capture.log
CAPTURE_TIMEOUT=5     # max seconds rpicam-still can run before being killed
HALT_AFTER_CAPTURE=1  # set to 0 to disable shutdown for debugging

mkdir -p "$PHOTO_DIR"

# Read time directly from Witty Pi RTC via I2C. This bypasses the stale
# Linux system clock that gets restored to the last-saved value at boot
# (since we disabled systemd-timesyncd). Registers 58-64 hold the RTC
# time in BCD format on the Witty Pi 4's MCU at I2C address 0x08.
read_bcd() {
    local val=$(i2cget -y 1 0x08 $1)
    val=${val#0x}
    printf "%d" $(( (16#${val:0:1} * 10) + 16#${val:1:1} ))
}

SEC=$(read_bcd 58)
MIN=$(read_bcd 59)
HOUR=$(read_bcd 60)
DAY=$(read_bcd 61)
MONTH=$(read_bcd 63)
YEAR=$(read_bcd 64)
TIMESTAMP=$(printf "20%02d-%02d-%02d_%02d-%02d-%02d" $YEAR $MONTH $DAY $HOUR $MIN $SEC)

OUT="${PHOTO_DIR}/snap_${TIMESTAMP}.jpg"
KTIME=$(awk '{print $1}' /proc/uptime)
echo "[$TIMESTAMP] starting capture (uptime=${KTIME}s)" >> "$LOG"

START=$(date +%s%N)

# `timeout` kills rpicam-still if it hangs past CAPTURE_TIMEOUT.
# Output is silenced to avoid blocking on UART transmission.
timeout ${CAPTURE_TIMEOUT}s rpicam-still --nopreview \
    -o "$OUT" \
    --width 4056 --height 3040 \
    --quality 95 \
    --metering average \
    --timeout 1000 \
    >/dev/null 2>&1

EXIT=$?
END=$(date +%s%N)
ELAPSED=$(( (END - START) / 1000000 ))
UPTIME=$(awk '{print $1}' /proc/uptime)

# Verify file exists and is non-empty before declaring success
if [ -s "$OUT" ]; then
    SIZE=$(stat -c%s "$OUT")
    echo "[$TIMESTAMP] done exit=$EXIT time=${ELAPSED}ms size=${SIZE}B boot_to_photo=${UPTIME}s" >> "$LOG"
    SAVED=1
else
    echo "[$TIMESTAMP] FAILED exit=$EXIT time=${ELAPSED}ms file_missing_or_empty" >> "$LOG"
    SAVED=0
fi

# Flush this file specifically (faster than full system sync)
sync -d "$OUT" 2>/dev/null || sync

# Power off unless MAINTENANCE flag is set (escape hatch for debugging)
if [ "$HALT_AFTER_CAPTURE" -eq 1 ] && [ ! -f /boot/firmware/MAINTENANCE ]; then
    if [ $SAVED -eq 1 ]; then
        echo "[$TIMESTAMP] powering off after successful save" >> "$LOG"
    else
        echo "[$TIMESTAMP] powering off after FAILED capture (will retry next cycle)" >> "$LOG"
    fi
    sleep 0.5
    /sbin/poweroff
else
    if [ -f /boot/firmware/MAINTENANCE ]; then
        echo "[$TIMESTAMP] MAINTENANCE flag set - skipping poweroff" >> "$LOG"
    fi
fi
```

Make executable:
```bash
chmod +x /home/epochs2/capture.sh
mkdir -p /home/epochs2/photos
```

Notes:
- **`/sbin/poweroff` not `/sbin/halt`** — `halt` historically only stops the CPU but leaves the system powered. On the Pi this meant TXD didn't go LOW and Witty Pi never cut power. `poweroff` runs the full systemd shutdown sequence which brings TXD low.
- **RTC-direct timestamp** — necessary because we disabled `systemd-timesyncd`. The Linux `fake-hwclock` returns the time saved at last shutdown, which doesn't advance between boots. Reading the Witty Pi RTC directly gives the real wall time.
- **`--timeout 1000`** — 1 second for auto-exposure / auto-white-balance to settle. Default is 3000ms; reducing saves ~1.3s.
- **`sync -d "$OUT"`** — flushes only this specific file to disk. Faster than full system `sync`.
- **MAINTENANCE flag** — if `/boot/firmware/MAINTENANCE` exists, the script skips power-off. Useful for debugging without auto-shutdown.

---

## Witty Pi schedule script

The schedule controls when the Pi is woken up. Using `WAIT` means the Witty Pi never forces a shutdown — `capture.sh` is responsible for halting via `/sbin/poweroff`.

Create `/home/epochs2/wittypi/schedules/on_wait_off_5m.wpi`:

```
# Pi stays on under script control (WAIT), off for 5 minutes between cycles
BEGIN 2025-01-01 00:00:00
END   2099-12-31 23:59:59
ON    M1 WAIT
OFF   M5
```

The `M1` is a placeholder — it's only used for loop math when `runScript.sh` recalculates state on boot. Since `WAIT` is present, Witty Pi never enforces a shutdown at that time.

Apply the schedule:
```bash
sudo /home/epochs2/wittypi/wittyPi.sh
```
Choose **6** (Choose schedule script), then pick `on_wait_off_5m.wpi`.

To change interval, create another `.wpi` file with different `OFF` value and select it from the menu.

---

## Pause / debug workflow

When you need to stop the cycle to debug:

**Quick stop (Pi will boot, capture, NOT power off — stays on indefinitely):**
```bash
sudo touch /boot/firmware/MAINTENANCE
```

With our `WAIT` schedule there's no shutdown alarm armed, only a startup alarm — which is fine.

**Full stop (also clear all Witty Pi alarms):**
```bash
sudo /home/epochs2/wittypi/wittyPi.sh
# choose 12 (Reset data) → 7 (Perform all actions above)
```

**Resume cycle:**
```bash
sudo rm /boot/firmware/MAINTENANCE
# Re-apply schedule via wittyPi.sh option 6 if you cleared it
sudo poweroff
```

---

## Retrieving photos

The system runs with networking and SSH disabled. To retrieve photos, temporarily re-enable both:

```bash
# Prevent auto-shutdown while transferring
sudo touch /boot/firmware/MAINTENANCE

# Bring network and SSH up
sudo systemctl start wpa_supplicant
sudo systemctl start NetworkManager
sudo nmtui                  # connect to WiFi
sudo systemctl start ssh

# Get IP address
hostname -I
```

Then from your computer:
```bash
scp -r epochs2@<pi-ip>:/home/epochs2/photos/ ~/Downloads/
```

When done:
```bash
sudo systemctl stop ssh
sudo systemctl stop NetworkManager
sudo systemctl stop wpa_supplicant
sudo rm /boot/firmware/MAINTENANCE
sudo poweroff
```

Next boot resumes the photo cycle without network.

---

## Timing breakdown (Pi Zero 2W, current)

| Phase | Time | Notes |
|---|---|---|
| Kernel boot | 3.15s | Fixed |
| SD card enumeration | ~5.0s | Kernel-level, cannot be reduced |
| fsck | ~1.2s | Keep — SD cards corrupt easily without it |
| Filesystem mount + sysinit | ~1.4s | Mostly fixed overhead |
| Script start (uptime) | ~9.7s | From `capture.log` |
| libcamera init + capture | ~4.0s | Tuning file load, IPA init, double mode-select, AE settle |
| **Total boot to photo** | **~15.5s** | From `boot_to_photo=` in capture.log |
| File sync + poweroff sequence | ~4.5s | systemd shutdown + Witty Pi 1s delay |
| **Total ON time per cycle** | **~20s** | Power-on to power-cut, measured at battery |

---

## Power measurements (actual)

Measured from the Witty Pi `Iout` reading at 5V output:
- **Active (boot + capture + shutdown):** 0.5A @ 5V → **2.5W** at the Pi's 5V rail, **20 seconds** per cycle
- **Standby (Pi off, Witty Pi only):** 0.0002A @ 5V → **0.001W** → essentially negligible

For a battery powering through the Witty Pi DC/DC converter (~85% efficiency at 12V→5V):
- Active draw from battery: ~2.94W
- Standby draw from battery: ~0.0012W

---

## Battery life estimates (12V 7Ah lead-acid battery)

Assumptions:
- 12V × 7Ah = 84 Wh nominal capacity
- 50% depth-of-discharge for lead-acid longevity → **42 Wh usable**
- Witty Pi DC/DC converter ~85% efficient at 12V→5V
- 20 seconds active per cycle, OFF interval as listed

| OFF interval | Cycles/day | Active Wh/day | Standby Wh/day | Total Wh/day | Battery runtime |
|---|---|---|---|---|---|
| 30 sec | 1728 | 28.2 | 0.02 | 28.2 | **1.5 days** |
| 1 min | 1080 | 17.6 | 0.02 | 17.7 | **2.4 days** |
| 5 min | 270 | 4.4 | 0.03 | 4.4 | **9.5 days** |
| 15 min | 94 | 1.5 | 0.03 | 1.6 | **27 days** |
| 30 min | 48 | 0.78 | 0.03 | 0.80 | **52 days** |
| 1 hour | 24 | 0.39 | 0.03 | 0.42 | **3.3 months** |
| 6 hours | 4 | 0.07 | 0.03 | 0.09 | **15 months** |
| 24 hours | 1 | 0.02 | 0.03 | 0.05 | **31 months** (but standby self-discharge dominates) |

Notes:
- At very long intervals, the **battery's own self-discharge** (~3-5% per month for lead-acid) becomes the limiting factor, not the load. The 31-month figure for 24-hour intervals is theoretical — in practice you'd be lucky to get 12-18 months from a single charge regardless of load.
- These figures assume the battery is full at start and not recharging.
- If your battery is a lithium chemistry instead of lead-acid, you can use closer to 80-90% DoD safely, giving roughly 1.7x the runtime estimates above.

---

## Verification

After replicating, confirm:

```bash
# Total boot time
systemd-analyze

# What's slowest
systemd-analyze blame | head -20

# Critical path to capture service
systemd-analyze critical-chain boot-capture.service

# Capture log
tail -10 /home/epochs2/capture.log
ls -lt /home/epochs2/photos/ | head -5

# Witty Pi status and next alarm
sudo /home/epochs2/wittypi/wittyPi.sh
# look for "Schedule next startup at: ..."

# Witty Pi log
tail -20 /home/epochs2/wittypi/wittyPi.log
```

Expected output in `capture.log` after a successful cycle:
```
[2026-05-21_16-30-22] starting capture (uptime=9.67s)
[2026-05-21_16-30-22] done exit=0 time=4015ms size=8231451B boot_to_photo=13.71s
[2026-05-21_16-30-22] powering off after successful save
```

The filenames must show **distinct timestamps for each cycle**. If you see the same timestamp repeated, the RTC-direct timestamp logic isn't working — check that `i2cget -y 1 0x08 58` returns valid BCD data.

---

## Current bottlenecks (not yet addressed)

1. **~5 seconds** waiting for `dev-disk-by-partuuid` (SD card enumeration). Kernel-level — no easy fix without custom kernel work or a faster SD card.
2. **~1.2 seconds** of fsck on `/boot/firmware` every boot. Could be disabled (`fsck.repair=no`) but not recommended given unclean power cuts.
3. **~4 seconds** of libcamera initialization + capture. Reducing resolution to 2028×1520 saves ~0.5-1s by avoiding double mode-selection. Reducing `--timeout` below 1000ms risks poor exposure.
4. **~3.15 seconds** of kernel boot time. Addressable only with custom kernel build.
5. **~4.5 seconds** for poweroff sequence. Most of this is systemd bringing down services; could be reduced with a custom init or by killing services more aggressively before calling poweroff.

Next steps to investigate:
- Reducing capture resolution to 2028×1520 and measuring impact
- Faster SD card (UHS-I A1/A2 rated) to reduce enumeration time
- Custom init script (replacing SystemD entirely) for ~6s further savings
- Tuning Witty Pi pulsing dummy load if using a USB power bank that auto-sleeps

---

## Pi Zero W variation (slower hardware)

The Pi Zero W (single-core ARMv6) has a quirk: the IMX477 sensor takes ~34 seconds to be detected after boot due to a device-tree dependency cycle between the CSI interface and the I2C-attached sensor. A SystemD service starting at `local-fs.target` (~6s) would just wait there idle. Use a **udev rule** instead:

`/etc/udev/rules.d/99-camera-capture.rules`:
```
SUBSYSTEM=="video4linux", KERNEL=="video0", ACTION=="add", RUN+="/bin/systemd-run --no-block /home/epochs1/capture.sh"
```

This fires the script the moment `/dev/video0` is created, regardless of how long detection takes. `systemd-run --no-block` hands off to systemd because udev rules have strict timing limits and processes that hang get killed.

On the Zero 2W (quad-core), the sensor is detected fast enough that the SystemD service approach is simpler and slightly faster.

---

## Replication checklist

1. Flash Raspberry Pi OS Lite Trixie to SD card.
2. Edit `config.txt` and `cmdline.txt` on the SD card before first boot.
3. Boot Pi, confirm UART works at 115200 baud.
4. Enable networking temporarily, install Witty Pi software (in `~`).
5. Power off, mount Witty Pi 4 on GPIO header, power on via Witty Pi.
6. Verify `i2cdetect -y 1` shows `0x08`.
7. Set Witty Pi power cut delay to 1s: `sudo i2cset -y 1 0x08 21 10`
8. Set "Default state when powered" to ON via `wittyPi.sh` option 11.
9. Disable services (cloud-init purge first, then network services, then SSH last).
10. Create `/etc/modules-load.d/camera.conf`.
11. Create `capture.sh` with RTC-direct timestamp + poweroff, and make executable.
12. Create `boot-capture.service` and enable.
13. Test capture manually with MAINTENANCE flag: should produce a photo with current timestamp and NOT halt.
14. Test once without MAINTENANCE: should produce photo, power down cleanly to ~0.0002A standby.
15. Create schedule `.wpi` file with desired OFF interval and apply via `wittyPi.sh` option 6.
16. Reboot. Witty Pi takes over: boots, captures, powers off, cuts power, waits, repeats.
