#if __has_include("ble_manager.h")
#include "ble_manager.h"
#else
#include "../include/ble_manager.h"
#endif

BleManager::BleManager()
  : pServer(nullptr), pService(nullptr),
    pCharMetadata(nullptr), pCharControl(nullptr), pCharArtwork(nullptr),
    deviceConnected(false), oldDeviceConnected(false),
    bleMutex(nullptr), hasPendingTrack(false),
    artBytesReceived(0), expectedTotalChunks(0), chunksReceivedCount(0),
    artComplete(false),
    currentArtId("") {
  bleMutex = xSemaphoreCreateMutex();
  memset(artReceiveBuffer, 0, sizeof(artReceiveBuffer));
  memset(chunkReceived, 0, sizeof(chunkReceived));
}

BleManager::~BleManager() {
  if (bleMutex) {
    vSemaphoreDelete(bleMutex);
    bleMutex = nullptr;
  }
}

bool BleManager::begin() {
  Serial.println("[BLE] Initializing Bluetooth Low Energy stack...");
  BLEDevice::init(BLE_DEVICE_NAME);

  // Set MTU to 512 for fast metadata and artwork transfers
  BLEDevice::setMTU(512);

  // Create GATT Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create GATT Service
  pService = pServer->createService(BLE_SERVICE_UUID);

  // Characteristic: Metadata JSON (Mac writes to Cardputer)
  pCharMetadata = pService->createCharacteristic(
    BLE_CHAR_METADATA_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pCharMetadata->setCallbacks(this);

  // Characteristic: Control / Feedback (Cardputer notifies Mac)
  pCharControl = pService->createCharacteristic(
    BLE_CHAR_CONTROL_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharControl->addDescriptor(new BLE2902());

  // Characteristic: Artwork Stream (Mac writes chunks to Cardputer)
  pCharArtwork = pService->createCharacteristic(
    BLE_CHAR_ARTWORK_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pCharArtwork->setCallbacks(this);

  // Start service
  pService->start();

  // Start Advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone/Mac connections
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as '" BLE_DEVICE_NAME "'...");
  return true;
}

void BleManager::onConnect(BLEServer* pServer) {
  deviceConnected = true;
  Serial.println("[BLE] Central client connected (Mac companion active)!");
}

void BleManager::onDisconnect(BLEServer* pServer) {
  deviceConnected = false;
  Serial.println("[BLE] Client disconnected. Restarting advertising...");
  pServer->startAdvertising();
}

void BleManager::onWrite(BLECharacteristic* pCharacteristic) {
  if (pCharacteristic == pCharMetadata) {
    String value = pCharacteristic->getValue().c_str();
    if (value.length() > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, value);
      if (!err) {
        TrackInfo incoming;
        incoming.isRunning = doc["running"] | false;
        incoming.state     = doc["state"] | "stopped";
        incoming.title     = doc["title"] | "";
        incoming.artist    = doc["artist"] | "";
        incoming.album     = doc["album"] | "";
        incoming.duration  = doc["duration"] | 0;
        incoming.elapsed   = doc["elapsed"] | 0;
        incoming.artworkId = doc["artwork_id"] | "none";
        incoming.clock     = doc["clock"] | "--:--";

        if (bleMutex && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          pendingTrack = incoming;
          hasPendingTrack = true;
          xSemaphoreGive(bleMutex);
        }
      }
    }
  } else if (pCharacteristic == pCharArtwork) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();

    if (len >= 4) {
      uint16_t chunkIdx = (data[0] << 8) | data[1];
      uint16_t totalChunks = (data[2] << 8) | data[3];
      size_t targetOffset = 0;
      const uint8_t* payload = nullptr;
      size_t payloadLen = 0;

      if (len >= 6) {
        // Dynamic MTU 6-byte header with explicit targetOffset
        targetOffset = (data[4] << 8) | data[5];
        payload = data + 6;
        payloadLen = len - 6;
      } else {
        // Legacy 4-byte fixed header fallback
        targetOffset = (size_t)chunkIdx * 480;
        payload = data + 4;
        payloadLen = len - 4;
      }

      if (bleMutex && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lastChunkTime = millis();
        if (totalChunks > 0 && totalChunks <= MAX_ART_CHUNKS) {
          expectedTotalChunks = totalChunks;
        }

        if (chunkIdx < MAX_ART_CHUNKS && (targetOffset + payloadLen <= sizeof(artReceiveBuffer))) {
          memcpy(artReceiveBuffer + targetOffset, payload, payloadLen);
          if (!chunkReceived[chunkIdx]) {
            chunkReceived[chunkIdx] = true;
            chunksReceivedCount++;
          }
          if (targetOffset + payloadLen > artBytesReceived) {
            artBytesReceived = targetOffset + payloadLen;
          }
        }

        if (expectedTotalChunks > 0 && chunksReceivedCount >= expectedTotalChunks && artBytesReceived >= sizeof(artReceiveBuffer)) {
          artComplete = true;
          Serial.printf("[BLE] Artwork all %u chunks validated and complete (%u bytes)!\n",
                        expectedTotalChunks, (unsigned int)artBytesReceived);
        }
        xSemaphoreGive(bleMutex);
      }
    }
  }
}

bool BleManager::popTrackUpdate(TrackInfo& info) {
  bool ret = false;
  if (bleMutex && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (hasPendingTrack) {
      info = pendingTrack;
      hasPendingTrack = false;
      ret = true;
    }
    xSemaphoreGive(bleMutex);
  }
  return ret;
}

bool BleManager::sendCommand(const String& cmd) {
  if (!deviceConnected || !pCharControl) {
    return false;
  }
  pCharControl->setValue(cmd.c_str());
  pCharControl->notify();
  Serial.printf("[BLE] Sent command to Mac: %s\n", cmd.c_str());
  return true;
}

bool BleManager::requestArtwork(const String& artworkId) {
  if (!deviceConnected || artworkId.length() == 0) {
    return false;
  }
  if (bleMutex && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // If we are currently actively receiving chunks for this exact artworkId within the last 3s,
    // don't wipe out the buffer or send duplicate requests!
    if (currentArtId == artworkId && chunksReceivedCount > 0 && !artComplete && (millis() - lastChunkTime < 3000)) {
      xSemaphoreGive(bleMutex);
      Serial.printf("[BLE] Transfer for %s already in progress (%u/%u chunks). Skipping duplicate request.\n",
                    artworkId.c_str(), chunksReceivedCount, expectedTotalChunks);
      return true;
    }
    currentArtId = artworkId;
    artBytesReceived = 0;
    expectedTotalChunks = 0;
    chunksReceivedCount = 0;
    artComplete = false;
    lastChunkTime = millis();
    memset(chunkReceived, 0, sizeof(chunkReceived));
    xSemaphoreGive(bleMutex);
  }
  String req = "GET_ART:" + artworkId;
  return sendCommand(req);
}

bool BleManager::hasNewArtwork(uint8_t* buffer, size_t bufferSize) {
  bool ret = false;
  if (bleMutex && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    // Check for stalled / timed-out transfer
    if (chunksReceivedCount > 0 && !artComplete && (millis() - lastChunkTime > 4000)) {
      Serial.println("[BLE] Artwork packet stream timed out. Resetting state.");
      artBytesReceived = 0;
      expectedTotalChunks = 0;
      chunksReceivedCount = 0;
      memset(chunkReceived, 0, sizeof(chunkReceived));
    }

    if (artComplete && buffer != nullptr && bufferSize >= artBytesReceived) {
      memcpy(buffer, artReceiveBuffer, artBytesReceived);
      artComplete = false;
      ret = true;
    }
    xSemaphoreGive(bleMutex);
  }
  return ret;
}
