#!/bin/bash

PHOTO_DIR=/home/epochs2/photos
LOG=/home/epochs2/capture.log
CAPTURE_TIMEOUT=5
HALT_AFTER_CAPTURE=1

mkdir -p "$PHOTO_DIR"

# Read time directly from Witty Pi RTC (bypasses stale system clock)
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

if [ -s "$OUT" ]; then
    SIZE=$(stat -c%s "$OUT")
    echo "[$TIMESTAMP] done exit=$EXIT time=${ELAPSED}ms size=${SIZE}B boot_to_photo=${UPTIME}s" >> "$LOG"
    SAVED=1
else
    echo "[$TIMESTAMP] FAILED exit=$EXIT time=${ELAPSED}ms file_missing_or_empty" >> "$LOG"
    SAVED=0
fi

sync -d "$OUT" 2>/dev/null || sync

if [ "$HALT_AFTER_CAPTURE" -eq 1 ] && [ ! -f /boot/firmware/MAINTENANCE ]; then
    if [ $SAVED -eq 1 ]; then
        echo "[$TIMESTAMP] halting after successful save" >> "$LOG"
    else
        echo "[$TIMESTAMP] halting after FAILED capture (will retry next cycle)" >> "$LOG"
    fi
    #sleep 0.5
    #sudo gpioset gpiochip0 4=0
    #sleep 5
    sudo poweroff
else
    if [ -f /boot/firmware/MAINTENANCE ]; then
        echo "[$TIMESTAMP] MAINTENANCE flag set - skipping halt" >> "$LOG"
    fi
fi
