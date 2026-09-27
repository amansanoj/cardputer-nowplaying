#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <SPI.h>
#include "config.h"

enum StorageType {
  STORAGE_NONE = 0,
  STORAGE_SD,
  STORAGE_LITTLEFS
};

class ArtworkCache {
public:
  ArtworkCache();
  bool begin();
  bool hasArtwork(const String& artworkId);
  bool loadArtwork(const String& artworkId, uint8_t* buffer, size_t bufferSize);
  bool saveArtwork(const String& artworkId, const uint8_t* buffer, size_t bufferSize);
  StorageType getStorageType() const { return storageType; }
  const char* getStorageName() const;

private:
  StorageType storageType;
  fs::FS* fsPtr;
  SPIClass sdSPI;
};
