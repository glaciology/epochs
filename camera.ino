// ─────────────────────────────────────────────────────────────────────────────
//  TAKE PHOTO AND SAVE TO SD
// ─────────────────────────────────────────────────────────────────────────────
void takePhoto() {
  DBGLN("Info: Powering on camera...");
  digitalWrite(PIN_SD_CS,  HIGH);  // explicitly deassert SD before any camera work
  digitalWrite(PIN_CAM_CS, HIGH);  // camera also deasserted until we need it
  printDateTime();
  cameraPowerOff();
  delay(100);
  cameraPowerOn();
  delay(500);
  DBGLN("Info: cam poweron");
  petDog();
  cam.reset(); 
  DBGLN("Info: cam reset");
  delay(500);   
  cam.begin();
  DBGLN("Info: cam begin");
  petDog();
  delay(500);

  // Build filename
  rtc.getTime();
  char imgFileName[32];
  sprintf(imgFileName, "IMG_20%02d%02d%02d_%02d%02d%02d_%04d.jpg",
          rtc.year, rtc.month, rtc.dayOfMonth,
          rtc.hour, rtc.minute, rtc.seconds,
          photoCounter);

  ArducamCamera* camInst = cam.getCameraInstance();
  camInst->arducamCameraOp->flushFifo(camInst);
  DBGLN("Info: flushFifo done");
  camInst->arducamCameraOp->clearFifoFlag(camInst);
  camInst->burstFirstFlag = 0;  
  DBGLN("Info: clearFifo done");
  delay(50);

  cam.setManualFocus(0);  // 0 = infinity
  DBGLN("Info: setManualFocus done");
  cam.setAutoWhiteBalance(TRUE);                    // TRUE = auto, FALSE = manual
  DBGLN("Info: setAWB done");
//  cam.setAutoWhiteBalanceMode(CAM_WHITE_BALANCE_AUTO); // AUTO, SUNNY, CLOUDY, OFFICE, HOME
//  cam.setAutoExposure(TRUE);                        // TRUE = auto
//  cam.setAutoISOSensitive(TRUE);                    // TRUE = auto ISO
//  cam.setImageQuality(HIGH_QUALITY);                // LOW_QUALITY, NORMAL_QUALITY, HIGH_QUALITY
  delay(200);
//  CamStatus status = cam.takePicture(CAM_IMAGE_MODE_QVGA, CAM_IMAGE_PIX_FMT_JPG); // lower quality
  CamStatus status =cam.takePicture(CAM_IMAGE_MODE_WQXGA2, CAM_IMAGE_PIX_FMT_JPG); // 2592×1944 (5MP only)
  petDog();

  if (status != CAM_ERR_SUCCESS) {
    DBG("Warning: takePicture() status: "); DBGLN((int)status);
    cameraPowerOff();
    logDebug("CAM_CAPTURE_ERR");
    return;
  }

  uint32_t imgLen = cam.getTotalLength();
  DBG("Info: Image length: "); DBGLN(imgLen);

  if (imgLen == 0) {
    DBGLN("Warning: Zero length image.");
    cameraPowerOff();
    logDebug("CAM_ZERO_LEN");
    return;
  }

  if (!imageFile.open(imgFileName, O_CREAT | O_WRITE | O_TRUNC)) {
    DBGLN("Warning: Failed to open image file.");
    cameraPowerOff();
    logDebug("CAM_FILE_ERR");
    return;
  }

  // Drain FIFO to SD in chunks using readBuff()
  const uint16_t CHUNK = 64;
  static uint8_t buf[CHUNK];
  bool ok = true;

  printDateTime();
  while (cam.getReceivedLength() > 0) {
//    petDog();
    uint16_t toRead = (cam.getReceivedLength() >= CHUNK) ? CHUNK : (uint16_t)cam.getReceivedLength();    cam.readBuff(buf, toRead);
    if (imageFile.write(buf, toRead) != toRead) {
      DBGLN("Warning: SD write failed.");
      writeFailCounter++;
      ok = false;
      break;
    }
  }

  imageFile.sync();
  if (!imageFile.close()) {
    DBGLN("Warning: image file close failed.");
    closeFailCounter++;
  }
  cameraPowerOff();

  // Ensure FIFO state is clean for next cycle
  camInst->receivedLength = 0;
  camInst->totalLength    = 0;
  camInst->burstFirstFlag = 0;

  if (ok) {
    photoCounter++;
    online.cam = true;
    DBG("Info: Photo saved: "); DBG(imgFileName);
    DBG(" ("); DBG(imgLen); DBGLN(" bytes)");
  } else {
    online.cam = false;
    logDebug("CAM_WRITE_ERR");
  }
  printDateTime();
}
