#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "display_ui.h"
#include "config.h"

#define BLE_DEVICE_NAME        "Cardputer-NowPlaying"
#define BLE_SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_METADATA_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_CONTROL_UUID  "beb5483f-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_ARTWORK_UUID  "beb54840-36e1-4688-b7f5-ea07361b26a8"

class BleManager : public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
  BleManager();
  ~BleManager();
  bool begin();
  bool isConnected() const { return deviceConnected; }
  bool popTrackUpdate(TrackInfo& info);
  bool sendCommand(const String& cmd);
  bool requestArtwork(const String& artworkId);
  bool hasNewArtwork(uint8_t* buffer, size_t bufferSize);

  // BLEServerCallbacks
  void onConnect(BLEServer* pServer) override;
  void onDisconnect(BLEServer* pServer) override;

  // BLECharacteristicCallbacks
  void onWrite(BLECharacteristic* pCharacteristic) override;

private:
  BLEServer* pServer;
  BLEService* pService;
  BLECharacteristic* pCharMetadata;
  BLECharacteristic* pCharControl;
  BLECharacteristic* pCharArtwork;

  bool deviceConnected;
  bool oldDeviceConnected;

  SemaphoreHandle_t bleMutex;
  TrackInfo pendingTrack;
  bool hasPendingTrack;

  // Binary artwork chunk assembly (supports arbitrary order, up to 64 chunks)
  static const size_t MAX_ART_CHUNKS = 64;
  uint8_t artReceiveBuffer[ARTWORK_SIZE * ARTWORK_SIZE * 2];
  size_t artBytesReceived;
  bool chunkReceived[MAX_ART_CHUNKS];
  uint16_t expectedTotalChunks;
  uint16_t chunksReceivedCount;
  bool artComplete;
  unsigned long lastChunkTime;
  String currentArtId;
};
