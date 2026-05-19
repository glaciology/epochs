// ─────────────────────────────────────────────────────────────────────────────
//  RTC ALARM — configure next wakeup
// ─────────────────────────────────────────────────────────────────────────────
void configureNextAlarm() {
  am_hal_rtc_int_clear(AM_HAL_RTC_INT_ALM);
  rtc.getTime();

  uint32_t interval = 0;

  if (logMode == 1) {
    interval = max((uint32_t)MIN_INTERVAL_S, photoInterval);
  }
  else if (logMode == 2) {
    // determine summer or winter
    int m = rtc.month;
    int d = rtc.dayOfMonth;

    summerMode = (startMonth <= endMonth)
                   ? (m >= startMonth && m <= endMonth)
                   : (m >= startMonth || m <= endMonth);

    if (summerMode) {
      summerMode = (m != startMonth || d >= startDay) &&
                   (m != endMonth   || d <= endDay);
    }

    interval = summerMode
                 ? max((uint32_t)MIN_INTERVAL_S, summerInterval)
                 : max((uint32_t)MIN_INTERVAL_S, winterInterval);

    DBG("Info: Season mode: "); DBGLN(summerMode ? "SUMMER" : "WINTER");
  }

  time_t nextWake = rtc.getEpoch() + interval;
  struct tm t;
  gmtime_r(&nextWake, &t);
  rtc.setAlarm(t.tm_hour, t.tm_min, t.tm_sec, 0, t.tm_mday, t.tm_mon + 1);
  rtc.setAlarmMode(1);  // exact date match
  rtc.attachInterrupt();
  alarmFlag = false;

  DBG("Info: Next photo alarm in "); DBG(interval); DBGLN("s");
  printAlarm();
}

// ─────────────────────────────────────────────────────────────────────────────
//  GPS RTC SYNC  (first boot only; coin-cell fallback on failure)
// ─────────────────────────────────────────────────────────────────────────────
void syncRtcFromGps() {
  DBGLN("Info: Attempting RTC sync from GPS...");

  // GPS may be at 57600 from previous session — reset to 9600
//  gpsSerial.begin(57600);
//  delay(200);
//  gpsSerial.println("$PMTK251,9600*17");
//  delay(200);
//  gpsSerial.end();
//  delay(500);

  // Reconnect at 9600
  gpsSerial.begin(9600);
  gpsSerial.println("$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29"); // RMC only
  gpsSerial.println("$PMTK220,1000*1F"); // 1Hz
  delay(500);

  unsigned long t0        = millis();
  unsigned long lastPrint = millis();
  bool synced             = false;

  while (!synced && (millis() - t0 < (unsigned long)gpsSyncTimeoutMs)) {
    petDog();

    while (gpsSerial.available()) {
      char ch = gpsSerial.read();
      nmea.process(ch);

      // Accept time even without a position fix — date/time fields
      // are valid in GPRMC as soon as the module has satellite time,
      // indicated by a non-zero year regardless of fix status.
      if (nmea.isValid() &&
          nmea.getYear() >= 24 && nmea.getYear() < 99 &&
          nmea.getMonth() >= 1 && nmea.getMonth() <= 12 &&
          nmea.getDay() >= 1 && nmea.getDay() <= 31) {
      
        rtc.setTime(nmea.getHour(), nmea.getMinute(), nmea.getSecond(),
                    nmea.getHundredths(), nmea.getDay(), nmea.getMonth(),
                    (uint8_t)nmea.getYear());
        online.rtcSync = true;
        synced = true;
        DBGLN("Info: RTC synced with confirmed GPS fix:");
        printDateTime();
        break;
      }
    }

    if (millis() - lastPrint > 2000) {
      lastPrint = millis();
      DBG("Info: Waiting... year="); DBG(nmea.getYear());
      DBG(" fix="); DBGLN(nmea.isValid() ? "YES" : "NO");
    }
  }

  // Log GPS position to debug file at sync time
  if (synced && nmea.isValid()) {
    if (!debugFile.open(debugFileName, O_APPEND | O_WRITE)) {
      DBGLN("Warning: Could not open debug file for GPS position.");
    } else {
      debugFile.print("GPS_FIX,");
      debugFile.print(nmea.getLatitude() / 1000000., 6);  debugFile.print(",");
      debugFile.print(nmea.getLongitude() / 1000000., 6); debugFile.print(",");
      long alt = 0;
      nmea.getAltitude(alt);
      debugFile.print(alt / 1000., 1);     debugFile.print("m,");
      debugFile.print((int)nmea.getNumSatellites());       debugFile.print("sats,");
      debugFile.print(nmea.getHDOP() / 10., 1);           debugFile.println("hdop");
      debugFile.sync();
      debugFile.close();
      DBG("Info: GPS position logged: ");
      DBG(nmea.getLatitude());  DBG(", ");
      DBGLN(nmea.getLongitude());
    }
  }

  // Put GPS to standby
  gpsSerial.println("$PMTK161,0*28");
  gpsSerial.println("$PMTK225,4*2F");  // enter backup mode
  gpsSerial.end();
//  pinMode(GPS_SS_RX, INPUT);   // float RX — stops any leakage through GPS TX line
//  pinMode(GPS_SS_TX, INPUT);   // float TX

  if (!synced) {
    DBGLN("Warning: GPS fix not obtained. Using coin-cell RTC time.");
  }
}
// ─────────────────────────────────────────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────────────────────────────────────────
void blinkLed(byte flashes, unsigned int delayMs) {
  for (byte i = 0; i < flashes; i++) {
    digitalWrite(LED, HIGH); delay(delayMs);
    digitalWrite(LED, LOW);  delay(delayMs);
  }
}

void printDateTime() {
  rtc.getTime();
  char buf[25];
  sprintf(buf, "20%02d-%02d-%02d %02d:%02d:%02d",
          rtc.year, rtc.month, rtc.dayOfMonth,
          rtc.hour, rtc.minute, rtc.seconds);
  DBGLN(buf);
}

void printAlarm() {
  rtc.getAlarm();
  char buf[25];
  sprintf(buf, "ALM 20%02d-%02d-%02d %02d:%02d:%02d",
          rtc.year, rtc.alarmMonth, rtc.alarmDayOfMonth,
          rtc.alarmHour, rtc.alarmMinute, rtc.alarmSeconds);
  DBGLN(buf);
}
