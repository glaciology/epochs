
// ─────────────────────────────────────────────────────────────────────────────
//  CONFIG FILE  (CONFIG.TXT on SD)
// ─────────────────────────────────────────────────────────────────────────────
/*
  Expected CONFIG.TXT format (one setting per line, key=value):

    LOG_MODE(1=interval,2=season)=1
    PHOTO_INTERVAL_S(mode1,>=30)=60
    SUMMER_INTERVAL_S(mode2,>=30)=60
    WINTER_INTERVAL_S(mode2,>=30)=3600
    SUMMER_START_MONTH=4
    SUMMER_END_MONTH=9
    SUMMER_START_DAY=21
    SUMMER_END_DAY=21
    LED_INDICATORS(0-1)=1
    MEASURE_BATTERY(0-1)=0
    BAT_SHUTDOWN_V(volts)=3.50
    GPS_SYNC_TIMEOUT_S=180
*/

void getConfig() {
  if (!configFile.open("CONFIG.TXT", O_READ)) {
    DBGLN("Warning: CONFIG.TXT not found. Using defaults and creating template.");
    createDefaultConfig();
    blinkLed(6, 100);
    return;
  }

  int n;
  while ((n = configFile.fgets(line, sizeof(line))) > 0) {
    size_t len = strcspn(line, "\r\n");
    line[len] = '\0';

    char* key = strtok(line, "=");
    if (!key) continue;
    char* val = strtok(NULL, "=");
    if (!val) continue;

    if (strstr(key, "LOG_MODE"))               logMode           = (byte)atoi(val);
    else if (strstr(key, "PHOTO_INTERVAL_S"))  photoInterval     = (uint32_t)atol(val);
    else if (strstr(key, "SUMMER_INTERVAL_S")) summerInterval    = (uint32_t)atol(val);
    else if (strstr(key, "WINTER_INTERVAL_S")) winterInterval    = (uint32_t)atol(val);
    else if (strstr(key, "SUMMER_START_MONTH"))startMonth        = (byte)atoi(val);
    else if (strstr(key, "SUMMER_END_MONTH"))  endMonth          = (byte)atoi(val);
    else if (strstr(key, "SUMMER_START_DAY"))  startDay          = (byte)atoi(val);
    else if (strstr(key, "SUMMER_END_DAY"))    endDay            = (byte)atoi(val);
    else if (strstr(key, "LED_INDICATORS"))    ledBlink          = (atoi(val) != 0);
    else if (strstr(key, "MEASURE_BATTERY"))   measureBattery    = (atoi(val) != 0);
    else if (strstr(key, "BAT_SHUTDOWN_V"))    shutdownThreshold = atof(val);
    else if (strstr(key, "GPS_SYNC_TIMEOUT_S"))gpsSyncTimeoutMs  = (uint32_t)atol(val) * 1000UL;
  }

  configFile.close();

  // Enforce floors
  if (photoInterval   < MIN_INTERVAL_S) photoInterval   = MIN_INTERVAL_S;
  if (summerInterval  < MIN_INTERVAL_S) summerInterval  = MIN_INTERVAL_S;
  if (winterInterval  < MIN_INTERVAL_S) winterInterval  = MIN_INTERVAL_S;
  if (logMode < 1 || logMode > 2)       logMode         = 1;

  DBG("Info: Config loaded. Mode="); DBG(logMode);
  DBG(" photoInterval="); DBG(photoInterval);
  DBG(" summerInterval="); DBG(summerInterval);
  DBG(" winterInterval="); DBGLN(winterInterval);
}

void createDefaultConfig() {
  if (!configFile.open("CONFIG.TXT", O_CREAT | O_WRITE | O_TRUNC)) {
    DBGLN("Warning: Could not create CONFIG.TXT.");
    return;
  }
  configFile.println("LOG_MODE(1=interval,2=season)=1");
  configFile.println("PHOTO_INTERVAL_S(mode1,min30s)=10");
  configFile.println("SUMMER_INTERVAL_S(mode2,min30s)=60");
  configFile.println("WINTER_INTERVAL_S(mode2,min30s)=3600");
  configFile.println("SUMMER_START_MONTH=4");
  configFile.println("SUMMER_END_MONTH=9");
  configFile.println("SUMMER_START_DAY=21");
  configFile.println("SUMMER_END_DAY=21");
  configFile.println("LED_INDICATORS(0-1)=1");
  configFile.println("MEASURE_BATTERY(0-1)=0");
  configFile.println("BAT_SHUTDOWN_V(volts)=3.50");
  configFile.println("GPS_SYNC_TIMEOUT_S=180");
  configFile.println(SOFTWARE_VERSION " end;");
  configFile.sync();
  configFile.close();
  DBGLN("Info: Default CONFIG.TXT written.");
}


// ─────────────────────────────────────────────────────────────────────────────
//  SD CARD
// ─────────────────────────────────────────────────────────────────────────────
void configureSD() {
  if (!sd.begin(SD_CONFIG)) {
    delay(2000);
    if (!sd.begin(SD_CONFIG)) {
      DBGLN("Warning: SD init failed twice.");
      online.uSD = false;
      while (1) {
        blinkLed(2, 250);
        delay(2000);
      }
    }
  }
  DBGLN("Info: SD initialized.");
  online.uSD = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DEBUG FILE
// ─────────────────────────────────────────────────────────────────────────────
void createDebugFile() {
  volatile uint32_t *stat_reg = (uint32_t *)STAT_REGISTER_ADDRESS;
  uint32_t stat_val = *stat_reg;
  String statCodes = getStatusCodes(stat_val);

  sprintf(debugFileName, "dbg_%lx.csv", (unsigned long)cachedChipId);

  if (!debugFile.open(debugFileName, O_CREAT | O_APPEND | O_WRITE)) {
    DBGLN("Warning: Could not create debug file.");
    return;
  }

  // Header — only written when file is new (O_APPEND skips if data exists)
  if (debugFile.size() == 0) {
    debugFile.print(SOFTWARE_VERSION);
    debugFile.println(
      ",counter,onlineSD,onlineCam,rtcSync,"
      "photos,wdtMax,writeFails,closeFails,logMode,"
      "errorCode,lowBat,tempC,battV," + statCodes);
  }

  if (!debugFile.sync())  DBGLN("Warning: debug file sync failed.");
  if (!debugFile.close()) DBGLN("Warning: debug file close failed.");
}

void logDebug(const char* errorCode) {
  debugCounter++;

  if (!debugFile.open(debugFileName, O_APPEND | O_WRITE)) {
    DBGLN("Warning: Could not open debug file.");
    return;
  }

  rtc.getTime();
  char dt[25];
  sprintf(dt, "20%02d-%02d-%02d %02d:%02d:%02d",
          rtc.year, rtc.month, rtc.dayOfMonth,
          rtc.hour, rtc.minute, rtc.seconds);

  debugFile.print(dt);               debugFile.print(",");
  debugFile.print(debugCounter);     debugFile.print(",");
  debugFile.print(online.uSD);       debugFile.print(",");
  debugFile.print(online.cam);       debugFile.print(",");
  debugFile.print(online.rtcSync);   debugFile.print(",");
  debugFile.print(photoCounter);     debugFile.print(",");
  debugFile.print(wdtCounterMax);    debugFile.print(",");
  debugFile.print(writeFailCounter); debugFile.print(",");
  debugFile.print(closeFailCounter); debugFile.print(",");
  debugFile.print(logMode);          debugFile.print(",");
  debugFile.print(errorCode);        debugFile.print(",");
  debugFile.print(lowBatCounter);    debugFile.print(",");
  debugFile.print(getInternalTemp(), 2); debugFile.print(",");

  if (measureBattery) {
    debugFile.print(measBat(), 2);
  } else {
    debugFile.print("N/A");
  }
  debugFile.println();

  if (!debugFile.sync()) {
    DBGLN("Warning: debug file sync failed.");
  }

  if (!debugFile.timestamp(T_ACCESS,
        (rtc.year + 2000), rtc.month, rtc.dayOfMonth,
        rtc.hour, rtc.minute, rtc.seconds)) {
    DBGLN("Warning: debug timestamp failed.");
  }

  if (!debugFile.close()) {
    DBGLN("Warning: debug file close failed.");
    closeFailCounter++;
  }
}


String getStatusCodes(uint32_t s) {
  String out = "";
  if ((s >> 31) & 1) out += "SBOOT ";
  if ((s >> 30) & 1) out += "FBOOT ";
  if ((s >> 10) & 1) out += "BOBSTAT ";
  if ((s >>  9) & 1) out += "BOFSTAT ";
  if ((s >>  8) & 1) out += "BOCSTAT ";
  if ((s >>  7) & 1) out += "BOUSTAT ";
  if ((s >>  6) & 1) out += "WDRSTAT ";
  if ((s >>  5) & 1) out += "DBGRSTAT ";
  if ((s >>  4) & 1) out += "POIRSTAT ";
  if ((s >>  3) & 1) out += "SWRSTAT ";
  if ((s >>  2) & 1) out += "BORSTAT ";
  if ((s >>  1) & 1) out += "PORSTAT ";
  if ( s        & 1) out += "EXRSTAT ";
  return out;
}
