#if __has_include("ble_manager.h")
#include "ble_manager.h"
#else
#include "../include/ble_manager.h"
#endif

BleManager::BleManager()
  : pServer(nullptr), pService(nullptr),
    pCharMetadata(nullptr), pCharControl(nullptr), pCharArtwork(nullptr),
    deviceConnected(false), oldDeviceConnected(false),
    hasPendingTrack(false), artBytesReceived(0), artComplete(false),
    currentArtId("") {
  memset(artReceiveBuffer, 0, sizeof(artReceiveBuffer));
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
        pendingTrack.isRunning = doc["running"] | false;
        pendingTrack.state     = doc["state"] | "stopped";
        pendingTrack.title     = doc["title"] | "";
        pendingTrack.artist    = doc["artist"] | "";
        pendingTrack.album     = doc["album"] | "";
        pendingTrack.duration  = doc["duration"] | 0;
        pendingTrack.elapsed   = doc["elapsed"] | 0;
        pendingTrack.artworkId = doc["artwork_id"] | "none";
        pendingTrack.clock     = doc["clock"] | "--:--";

        hasPendingTrack = true;
      }
    }
  } else if (pCharacteristic == pCharArtwork) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();

    if (len >= 4) {
      uint16_t chunkIdx = (data[0] << 8) | data[1];
      uint16_t totalChunks = (data[2] << 8) | data[3];
      const uint8_t* payload = data + 4;
      size_t payloadLen = len - 4;

      if (chunkIdx == 0) {
        artBytesReceived = 0;
        artComplete = false;
      }

      if (artBytesReceived + payloadLen <= sizeof(artReceiveBuffer)) {
        memcpy(artReceiveBuffer + artBytesReceived, payload, payloadLen);
        artBytesReceived += payloadLen;
      }

      if (chunkIdx + 1 >= totalChunks || artBytesReceived >= sizeof(artReceiveBuffer)) {
        artComplete = true;
        Serial.printf("[BLE] Artwork fully received! (%u bytes)\n", (unsigned int)artBytesReceived);
      }
    }
  }
}

bool BleManager::popTrackUpdate(TrackInfo& info) {
  if (hasPendingTrack) {
    info = pendingTrack;
    hasPendingTrack = false;
    return true;
  }
  return false;
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
  currentArtId = artworkId;
  String req = "GET_ART:" + artworkId;
  return sendCommand(req);
}

bool BleManager::hasNewArtwork(uint8_t* buffer, size_t bufferSize) {
  if (artComplete && buffer != nullptr && bufferSize >= artBytesReceived) {
    memcpy(buffer, artReceiveBuffer, artBytesReceived);
    artComplete = false;
    return true;
  }
  return false;
}
