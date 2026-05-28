/*
  arducam_hal.cpp
  ---------------
  Platform HAL shim for the arducam_mega library on Apollo3 / SparkFun Redboard Artemis.

  WHY THIS FILE EXISTS
  The arducam_mega library is split into two layers:
    - A portable C core  (Arducam/ArducamCamera.c)  — sensor logic
    - A platform C++ wrapper (Arducam_Mega.cpp)      — Arduino SPI / GPIO glue

  The C core calls four extern symbols for CS and SPI I/O:
    arducamCsOutputMode()
    arducamSpiCsPinLow()
    arducamSpiCsPinHigh()
    (SPI transfer is handled inside the wrapper via a function pointer)

  On the Apollo3 core v1.2.3 the C↔C++ linkage resolution fails at link time
  because the wrapper's definitions are not visible to the C translation unit.
  This file re-defines those four symbols in a C++ file with explicit
  extern "C" export so the linker can find them from the C object.

  IMPORTANT: PIN_CAM_CS must match the value defined in CamLogger.ino.
  If you change the CS pin there, change it here too, or better yet
  include a shared header.
*/

#include <Arduino.h>
#include <SPI.h>

// ── Must match PIN_CAM_CS in CamLogger.ino ──────────────────────────────────
static const uint8_t CAM_CS_PIN = 4;   // *** keep in sync with epochs.ino ***

// ── HAL symbols exported to C linkage ───────────────────────────────────────
extern "C" {

void arducamCsOutputMode(uint8_t cs) { pinMode(cs, OUTPUT); digitalWrite(cs, HIGH); }
void arducamSpiCsPinLow(uint8_t cs)  { digitalWrite(cs, LOW); }
void arducamSpiCsPinHigh(uint8_t cs) { digitalWrite(cs, HIGH); }

}  // extern "C"


extern "C" void arducamSpiBlockTransfer(uint8_t val, uint8_t* buf, uint32_t len) {
    memset(buf, val, len);      // fill buf with the value to send (0x00)
    SPI.transfer(buf, len);     // Apollo3 SPI.transfer(buf, len) does full-duplex
                                // — sends buf contents, overwrites with received bytes
}
