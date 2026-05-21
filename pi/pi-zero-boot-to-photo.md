# Pi Zero W / Zero 2W: Boot-to-Photo Setup

This document captures the working configuration for a Raspberry Pi that boots, captures a single photo with the Raspberry Pi HQ Camera (IMX477), and is intended to be powered off externally.

Starting point on a stock image: **3 minutes 6 seconds** boot to photo.
Current result (Pi Zero 2W, ongoing): **~14 seconds** boot to photo.
Document covers both Pi Zero W and Zero 2W — the Zero 2W is significantly faster at every stage.

---

## Hardware

- Raspberry Pi Zero W (single-core ARMv6, 512 MB RAM) *or* Pi Zero 2W (quad-core ARMv8, 512 MB RAM)
- Raspberry Pi HQ Camera (Sony IMX477)
- USB-to-serial (UART) adapter for headless access via GPIO pins 8/10/6
- microSD card (Raspberry Pi OS Trixie Lite installed)

The Zero 2W is ~4-5x faster at every stage. The procedure is identical for both.

---

## Operating system

- **Raspberry Pi OS Trixie Lite** (Debian 13, headless)
- Pi Zero W: `armv6l`, kernel `6.12.75+rpt-rpi-v6`
- Pi Zero 2W: `aarch64`, kernel `6.12.75+rpt-rpi-v8`

Lite is required: the desktop variants pull in many additional services that block boot.

---

## Access method

**UART over GPIO**, not SSH or WiFi.

Wiring:
- Pi pin 8 (GPIO14 / TX) → USB-serial RX
- Pi pin 10 (GPIO15 / RX) → USB-serial TX
- Pi pin 6 (GND) → USB-serial GND

Connect at **115200 baud** with PuTTY, `screen`, or `minicom`.
For screen:
`screen -ls` + `screen -X -S 37333 quit` to kill any active (e.g., 37333) ports. Then, `screen /dev/tty.usbserial-AR0JUS7T 115200`

Why: UART is handled at the kernel level — works even when networking, SSH, and SystemD services are all disabled. It's the only access method that won't break as we strip services. Confirm UART works *before* disabling network services or you can lock yourself out and have to pull the SD card.

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

- **`camera_auto_detect=0` + `dtoverlay=imx477`** — skip auto-detection and hardcode the camera; saves probe time. Note: `vcgencmd get_camera` will still report `detected=0` even when the camera is working — this is a legacy command that doesn't understand libcamera. Use `rpicam-jpeg -o test.jpg` to confirm the camera is functional.
- **`display_auto_detect=0`, all HDMI options, `disable_fw_kms_setup=1`** — no display attached, so don't waste time probing/initializing anything display-related.
- **`disable_touchscreen=1`, `ignore_lcd=1`, `disable_poe_fan=1`, `force_eeprom_read=0`** — disable I2C probes for hardware that isn't present.
- **`dtparam=audio=off`** — no audio used.
- **`boot_delay=0`** — removes the bootloader delay window (small saving but free).
- **`disable_splash=1`** — removes the rainbow GPU splash screen.
- **`gpu_mem=128`** — the HQ Camera needs ≥128 MB of GPU memory for the legacy camera pipeline.
- **`enable_uart=1`** — turns on the hardware UART for serial console access.
- **`dtoverlay=disable-bt`** — frees up the proper hardware UART (PL011) from the Bluetooth chip; without this, the serial console uses the slower mini-UART which is clock-coupled to the CPU and unreliable.

---

## `/boot/firmware/cmdline.txt`

Single line:

```
console=serial0,115200 console=tty1 root=PARTUUID=ebf780e1-02 rootfstype=ext4 fsck.repair=yes rootwait quiet cfg80211.ieee80211_regdom=US
```

Reasoning:

- **`console=serial0,115200`** — routes the kernel/console to UART so we can interact over serial.
- **`console=tty1`** — kept as secondary console (harmless).
- **`quiet`** — suppresses kernel boot output to reduce serial-port-bound logging time.
- **`rootwait`** — wait for the SD card root partition to appear before mounting (necessary).

What was removed:

- **`modules-load=dwc2,g_ether`** — was set up for USB Ethernet gadget mode; not needed when we're not using network at all.
- **A separate attempt to add `modules-load=imx477,bcm2835_unicam_legacy,...`** — this turned out to be ignored by the kernel (`modules-load=` is a SystemD parameter, not a kernel one). Camera modules are now loaded via `/etc/modules-load.d/camera.conf` instead (see below).
- **`ds=nocloud;...` and cloud-init related parameters** — removed along with cloud-init purge.

---

## `/etc/modules-load.d/camera.conf`

New file:

```
imx477
bcm2835_unicam_legacy
bcm2835_v4l2
bcm2835_isp
```

Reasoning: forces these camera-related kernel modules to load as early as possible via SystemD's `systemd-modules-load.service`. Without this, the modules were being loaded later (after udev triggered them), which delayed sensor detection by several seconds. Adding this file moved the `imx477: Device found is imx477` dmesg line from ~41s to ~34s into boot on the Zero W.

---

## Disabled services

The stock Raspberry Pi OS Lite Trixie image includes cloud-init (unusually for an embedded image — possibly from this particular image build). It was contributing ~14-18 seconds to boot on the Zero 2W alone.

Commands run:

```bash
# Remove cloud-init entirely (it's not needed for embedded)
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

Reasoning for each:

| Service | Time saved | Why disabled |
|---|---|---|
| cloud-init (all 4 services) | ~14-18s | Designed for cloud images that pull config from a network metadata service. Useless on an embedded device and adds significant waiting time for network/metadata that will never arrive. |
| `NetworkManager` | ~14s | Adds up to 60 seconds waiting for DHCP. No network needed for this project. |
| `NetworkManager-wait-online` | ~1.3s | Waits for NM to report online status. Pointless without NM. |
| `wpa_supplicant` | ~300ms | WiFi authentication daemon; not needed. |
| `avahi-daemon` | ~380ms | mDNS service discovery; not needed. Also provides `.local` hostname resolution — disable only if not needed. |
| `systemd-timesyncd` | ~360ms | NTP time sync; requires network. Disable only if accurate timestamps are not required. |
| `systemd-rfkill` | ~100ms | Radio kill switch handler; no radios in use. |
| `rpi-eeprom-update` | ~100ms | Firmware update checker. |
| `keyboard-setup` | ~714ms | No keyboard attached. |
| `ssh` / `sshswitch` | ~725ms | Replaced by UART for access. |
| `e2scrub_reap` | ~530ms | Filesystem maintenance scrubbing. |
| `systemd-binfmt` | ~350ms | Registers binary formats for cross-arch execution. Not needed. |
| `modprobe@drm` | ~310ms | DRM display driver. No display attached. |

---

## Camera capture pipeline

On the Pi Zero 2W, capture is triggered by a **SystemD service** (`boot-capture.service`) that starts as soon as `local-fs.target` is reached, rather than waiting for the full `multi-user.target`. This is earlier than a udev-triggered approach on the Zero 2W because the camera is available quickly relative to the filesystem.

On the Pi Zero W, a **udev rule** approach works better because the IMX477 sensor takes ~34 seconds to be detected — firing on `/dev/video0` creation ensures the script runs at the earliest possible moment regardless of how long detection takes.

### SystemD service approach (Pi Zero 2W)

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
- **`After=local-fs.target`** — starts as soon as local filesystems are mounted, without waiting for the rest of sysinit. `sysinit.target` was reached at 6.8s; `local-fs.target` at 6.4s — small but free.
- **`DefaultDependencies=no`** — prevents systemd from adding implicit ordering dependencies that would delay the service.
- **`User=root`** — avoids waiting for `user@1000.service` (the user session manager) which added ~1s to the critical path.
- **`Type=oneshot`** — service is considered done when the script exits; correct for a single capture.

Enable it:
```bash
sudo systemctl daemon-reload
sudo systemctl enable boot-capture.service
```

### udev rule approach (Pi Zero W, or as fallback)

### `/etc/udev/rules.d/99-camera-capture.rules`

```
SUBSYSTEM=="video4linux", KERNEL=="video0", ACTION=="add", RUN+="/bin/systemd-run --no-block /home/epochs1/capture.sh"
```

Reasoning:
- Fires when `/dev/video0` is created by the kernel.
- `systemd-run --no-block` hands the script off to SystemD so it runs without blocking udev (udev rules have strict timing limits and processes that hang will be killed).

### `/home/epochs2/capture.sh`

```bash
#!/bin/bash

PHOTO_DIR=/home/epochs2/photos
LOG=/home/epochs2/capture.log
TIMESTAMP=$(date +%Y-%m-%d_%H-%M-%S)
OUT="${PHOTO_DIR}/snap_${TIMESTAMP}.jpg"

mkdir -p "$PHOTO_DIR"

KTIME=$(awk '{print $1}' /proc/uptime)
echo "[$TIMESTAMP] starting capture (uptime=${KTIME}s)" >> "$LOG"
START=$(date +%s%N)

rpicam-jpeg --nopreview \
    -o "$OUT" \
    --width 4056 --height 3040 \
    --quality 95 \
    --metering average \
    --timeout 1000

EXIT=$?
END=$(date +%s%N)
ELAPSED=$(( (END - START) / 1000000 ))
UPTIME=$(awk '{print $1}' /proc/uptime)

echo "[$TIMESTAMP] done exit=$EXIT time=${ELAPSED}ms boot_to_photo=${UPTIME}s file=$OUT" >> "$LOG"
sync
```

Reasoning:
- **`--nopreview`** — without this, `rpicam-jpeg` tries to create a DRM preview window even on headless systems, which consumes resources and can cause failures.
- **`--timeout 1000`** — 1 second for auto-exposure / auto-white-balance to settle. The default is 3000ms. Can be reduced to 500ms for faster captures at the cost of exposure accuracy. Measured difference: ~1.3s saved vs `--timeout 3000`.
- **`/proc/uptime`** — read at both start and end of the script; gives exact kernel-relative timestamps so boot-to-photo time is directly measurable without correlating log files.
- **`sync`** — flushes the JPEG to the SD card before the external power switch can cut power. Essential.
- **Full resolution (4056×3040)** — using the HQ Camera's native sensor resolution. Drop to 2028×1520 if speed matters more than image quality; this also avoids a double mode-selection that libcamera performs when switching from preview to capture resolution.

Make it executable:
```bash
chmod +x /home/epochs2/capture.sh
mkdir -p /home/epochs2/photos
```

---

## Timing breakdown (Pi Zero 2W, current)

From `systemd-analyze critical-chain boot-capture.service`:

```
boot-capture.service +4.58s
└─local-fs.target @6.37s
  └─boot-firmware.mount @6.25s +113ms
    └─systemd-fsck @5.03s +1.21s
      └─dev-disk-by-partuuid @5.01s
```

| Phase | Time | Notes |
|---|---|---|
| Kernel boot | 3.15s | Fixed |
| SD card enumeration | ~5.0s | Kernel-level, cannot be reduced |
| fsck | ~1.2s | Keep — SD cards corrupt easily without it |
| Filesystem mount + sysinit | ~1.4s | Mostly fixed overhead |
| Script start (uptime) | ~9.7s | From `capture.log` |
| libcamera init + capture | ~4.0s | Main remaining target |
| **Total boot to photo** | **~13.7s** | From `boot_to_photo=` in capture.log |

libcamera itself accounts for most of the capture time — it loads tuning files, initialises the IPA, and performs two mode selections (one at preview resolution, one at full capture resolution). The 4s figure includes all of this plus the `--timeout 1000` AE settling window.

---

## Verification

After replicating, confirm with:

```bash
# Total boot time
systemd-analyze

# What's slowest
systemd-analyze blame | head -20

# Critical path to capture service
systemd-analyze critical-chain boot-capture.service

# Capture confirmation and timing
cat /home/epochs2/capture.log | tail -5
ls -lt /home/epochs2/photos/ | head -3

# Service run log
journalctl -u boot-capture.service --no-pager

# When the sensor was detected
sudo dmesg | grep imx477
```

Expected output in `capture.log`:
```
[2026-05-20_13-00-00] starting capture (uptime=9.67s)
[2026-05-20_13-00-00] done exit=0 time=4015ms boot_to_photo=13.71s file=/home/epochs2/photos/snap_...jpg
```

---

## Current bottlenecks (not yet addressed)

The remaining boot time is dominated by:

1. **~5 seconds** waiting for `dev-disk-by-partuuid` (SD card boot partition) to enumerate. Likely a combination of SD card speed and driver initialisation order. Kernel-level — no easy fix without custom kernel work or switching to a faster SD card.
2. **~1.2 seconds** of fsck on `/boot/firmware` every boot. Could be disabled (`fsck.repair=no` in cmdline.txt) but not recommended — SD cards are vulnerable to corruption on unclean power cuts, which is exactly what this project does.
3. **~4 seconds** of libcamera initialisation + capture. Reducing resolution to 2028×1520 saves ~0.5-1s by avoiding the double mode selection. Reducing `--timeout` below 1000ms saves more but risks poorly exposed photos.
4. **~3.15 seconds** of kernel boot time. Addressable only with a custom kernel build (removing unused drivers, modules, etc.) — significant effort for marginal gains given the Zero 2W's speed.

Next steps to investigate:
- Reducing capture resolution to 2028×1520 and measuring the time saving
- Custom init script (replacing SystemD entirely) to eliminate the ~6s userspace overhead before the service starts
- Faster SD card to reduce enumeration time
