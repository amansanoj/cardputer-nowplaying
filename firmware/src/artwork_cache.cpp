#if __has_include("artwork_cache.h")
#include "artwork_cache.h"
#else
#include "../include/artwork_cache.h"
#endif

ArtworkCache::ArtworkCache()
  : storageType(STORAGE_NONE), fsPtr(nullptr), sdSPI(HSPI) {}

bool ArtworkCache::begin() {
  Serial.println("[ArtworkCache] Initializing on-device artwork cache...");

  // 1. Try MicroSD Card on dedicated hardware SPI bus (HSPI)
  sdSPI.begin(SD_SPI_SCK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);

  bool sdOk = SD.begin(SD_SPI_CS, sdSPI, 25000000);
  if (!sdOk && SD_SPI_CS != SD_SPI_CS_ALT) {
    sdOk = SD.begin(SD_SPI_CS_ALT, sdSPI, 25000000);
  }

  if (sdOk) {
    storageType = STORAGE_SD;
    fsPtr = &SD;
    uint64_t cardSizeMB = SD.cardSize() / (1024 * 1024);
    Serial.printf("[ArtworkCache] MicroSD Card mounted successfully! Size: %llu MB\n", cardSizeMB);

    if (!SD.exists("/art")) {
      SD.mkdir("/art");
    }
    return true;
  }

  Serial.println("[ArtworkCache] MicroSD card not detected. Falling back to LittleFS on flash...");

  // 2. Fallback: LittleFS on internal 8MB flash
  if (LittleFS.begin(true)) {
    storageType = STORAGE_LITTLEFS;
    fsPtr = &LittleFS;
    Serial.printf("[ArtworkCache] LittleFS mounted on internal flash! Total: %u KB, Used: %u KB\n",
                  (unsigned int)(LittleFS.totalBytes() / 1024),
                  (unsigned int)(LittleFS.usedBytes() / 1024));

    if (!LittleFS.exists("/art")) {
      LittleFS.mkdir("/art");
    }
    return true;
  }

  Serial.println("[ArtworkCache] Warning: No storage system available (SD or LittleFS).");
  storageType = STORAGE_NONE;
  fsPtr = nullptr;
  return false;
}

const char* ArtworkCache::getStorageName() const {
  switch (storageType) {
    case STORAGE_SD: return "MicroSD Card";
    case STORAGE_LITTLEFS: return "LittleFS (Flash)";
    default: return "None";
  }
}

bool ArtworkCache::hasArtwork(const String& artworkId) {
  if (!fsPtr || artworkId.length() == 0 || artworkId == "none") {
    return false;
  }

  String path = "/art/" + artworkId + ".raw";
  if (fsPtr->exists(path)) {
    File f = fsPtr->open(path, "r");
    if (f) {
      size_t sz = f.size();
      f.close();
      const size_t EXPECTED_SIZE = ARTWORK_SIZE * ARTWORK_SIZE * 2;
      return (sz == EXPECTED_SIZE);
    }
  }
  return false;
}

bool ArtworkCache::loadArtwork(const String& artworkId, uint8_t* buffer, size_t bufferSize) {
  if (!fsPtr || artworkId.length() == 0 || buffer == nullptr) {
    return false;
  }

  String path = "/art/" + artworkId + ".raw";
  File f = fsPtr->open(path, "r");
  if (!f) {
    return false;
  }

  size_t bytesRead = f.read(buffer, bufferSize);
  f.close();

  const size_t EXPECTED_SIZE = ARTWORK_SIZE * ARTWORK_SIZE * 2;
  return (bytesRead == EXPECTED_SIZE);
}

bool ArtworkCache::saveArtwork(const String& artworkId, const uint8_t* buffer, size_t bufferSize) {
  if (!fsPtr || artworkId.length() == 0 || buffer == nullptr || bufferSize == 0) {
    return false;
  }

  String path = "/art/" + artworkId + ".raw";
  File f = fsPtr->open(path, "w");
  if (!f) {
    Serial.printf("[ArtworkCache] Failed to open %s for writing!\n", path.c_str());
    return false;
  }

  size_t written = f.write(buffer, bufferSize);
  f.close();

  Serial.printf("[ArtworkCache] Cached %u bytes to %s on %s\n", (unsigned int)written, path.c_str(), getStorageName());
  return (written == bufferSize);
}
