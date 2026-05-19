// ─────────────────────────────────────────────────────────────────────────────
//  BUS INIT / DEINIT
// ─────────────────────────────────────────────────────────────────────────────
void initializeBuses() {
  pinMode(PIN_CAM_CS, OUTPUT);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_CAM_CS, HIGH);  // deassert camera CS before SPI bus starts
  SPI.begin();
  delay(5);
}

void deinitializeBuses() {
  SPI.end();
  online.uSD = false;
  online.cam = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CAMERA POWER
// ─────────────────────────────────────────────────────────────────────────────
void cameraPowerOn() {
  pinMode(PIN_CAM_POWER, OUTPUT);
  digitalWrite(PIN_CAM_POWER, HIGH);
  delay(100);   // allow sensor to stabilise
}

void cameraPowerOff() {
  digitalWrite(PIN_CAM_POWER, LOW);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SLEEP / WAKE
// ─────────────────────────────────────────────────────────────────────────────
void goToSleep() {
#if DEBUG
  Serial.end();
#endif

  power_adc_disable();
  digitalWrite(LED, LOW);

  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM0);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM1);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM2);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM3);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM4);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM5);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_ADC);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_UART0);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_UART1);

  // Disable all pads except essential power/control pins
  for (int x = 0; x < 50; x++) {
    if (x != 4) {
      am_hal_gpio_pinconfig(x, g_AM_HAL_GPIO_DISABLE);
    }
  }

  am_hal_stimer_config(AM_HAL_STIMER_CFG_CLEAR | AM_HAL_STIMER_CFG_FREEZE);
  am_hal_stimer_config(AM_HAL_STIMER_XTAL_32KHZ);

  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_ALL);
  am_hal_pwrctrl_memory_deepsleep_retain(AM_HAL_PWRCTRL_MEM_SRAM_384K);

  am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);

  // ── waking up ──
  wakeFromSleep();
}

void wakeFromSleep() {
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_MAX);
  am_hal_stimer_config(AM_HAL_STIMER_CFG_CLEAR | AM_HAL_STIMER_CFG_FREEZE);
  am_hal_stimer_config(AM_HAL_STIMER_HFRC_3MHZ);
  petDog();

#if DEBUG
  Serial.begin(115200);
#endif

  if (measureBattery) {
    ap3_adc_setup();
    ap3_set_pin_to_analog(BAT);
  }
}


// ─────────────────────────────────────────────────────────────────────────────
//  BATTERY
// ─────────────────────────────────────────────────────────────────────────────
float measBat() {
  analogReadResolution(14);
  pinMode(BAT_CNTRL, OUTPUT);
  digitalWrite(BAT_CNTRL, HIGH);
  delay(1);
  int m0 = analogRead(BAT);
  delay(10);
  int m1 = analogRead(BAT);
  digitalWrite(BAT_CNTRL, LOW);
  float v0 = (float)m0 * gain / 16384.0f + offset_v;
  float v1 = (float)m1 * gain / 16384.0f + offset_v;
  return (v0 + v1) / 2.0f;
}

void checkBattery() {
  if (!measureBattery) return;

  if (measBat() < shutdownThreshold) {
    DBGLN("Info: Battery low — sleeping until recharged.");
    online.chargedBat = false;
    deinitializeBuses();

    while (measBat() < shutdownThreshold + 0.1f) {
      goToSleep();
      petDog();
    }
    lowBatCounter++;
    online.chargedBat = true;
    DBGLN("Info: Battery recovered.");
    initializeBuses();
    configureSD();
  }
}
