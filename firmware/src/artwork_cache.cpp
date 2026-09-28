#if __has_include("artwork_cache.h")
#include "artwork_cache.h"
#else
#include "../include/artwork_cache.h"
#endif

ArtworkCache::ArtworkCache()
  : storageType(STORAGE_NONE), fsPtr(nullptr), sdSPI(HSPI) {}

bool ArtworkCache::begin() {
  Serial.println("[ArtworkCache] Initializing on-device artwork cache...");

#if HAS_SD_CARD
  // 1. Try MicroSD Card on dedicated hardware SPI bus (HSPI)
  if (SD_SPI_SCK >= 0 && SD_SPI_CS >= 0) {
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
    Serial.println("[ArtworkCache] MicroSD card not detected in slot. Falling back to LittleFS on flash...");
  }
#endif

  // 2. Fall back to LittleFS on internal SPI flash
  if (LittleFS.begin(true)) {
    storageType = STORAGE_LITTLEFS;
    fsPtr = &LittleFS;
    Serial.println("[ArtworkCache] LittleFS mounted successfully on internal flash!");
    if (!LittleFS.exists("/art")) {
      LittleFS.mkdir("/art");
    }
    return true;
  }

  Serial.println("[ArtworkCache] No SD card or LittleFS mounted. Direct stream active.");
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

bool ArtworkCache::isValidId(const String& artworkId) {
  if (artworkId.length() == 0 || artworkId.length() > 32 || artworkId == "none") {
    return false;
  }
  for (unsigned int i = 0; i < artworkId.length(); i++) {
    char c = artworkId.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              (c == '_') || (c == '-');
    if (!ok) return false;
  }
  return true;
}

bool ArtworkCache::hasArtwork(const String& artworkId) {
  if (!fsPtr || !isValidId(artworkId)) {
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
  if (!fsPtr || !isValidId(artworkId) || buffer == nullptr) {
    return false;
  }

  String path = "/art/" + artworkId + ".raw";
  File f = fsPtr->open(path, "r");
  if (!f) {
    return false;
  }

  const size_t EXPECTED_SIZE = ARTWORK_SIZE * ARTWORK_SIZE * 2;
  if (f.size() != EXPECTED_SIZE || bufferSize < EXPECTED_SIZE) {
    f.close();
    return false;
  }

  size_t bytesRead = f.read(buffer, EXPECTED_SIZE);
  f.close();

  if (bytesRead == EXPECTED_SIZE) {
    touchLRU(artworkId);
    return true;
  }
  return false;
}

void ArtworkCache::touchLRU(const String& artworkId) {
  if (storageType != STORAGE_LITTLEFS || !fsPtr || !isValidId(artworkId)) {
    return;
  }

  // Read existing IDs
  String ids[MAX_LITTLEFS_ARTWORKS + 5];
  size_t count = 0;

  if (fsPtr->exists("/art/lru.txt")) {
    File f = fsPtr->open("/art/lru.txt", "r");
    if (f) {
      while (f.available() && count < (MAX_LITTLEFS_ARTWORKS + 5)) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && line != artworkId && isValidId(line)) {
          ids[count++] = line;
        }
      }
      f.close();
    }
  }

  // Append current artworkId at the end (most recently used)
  if (count < (MAX_LITTLEFS_ARTWORKS + 5)) {
    ids[count++] = artworkId;
  }

  // Write back updated LRU list
  File f = fsPtr->open("/art/lru.txt", "w");
  if (f) {
    for (size_t i = 0; i < count; i++) {
      f.println(ids[i]);
    }
    f.close();
  }
}

void ArtworkCache::evictOldestIfNeeded() {
  if (storageType != STORAGE_LITTLEFS || !fsPtr) {
    return;
  }

  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  size_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;

  File dir = LittleFS.open("/art");
  if (!dir || !dir.isDirectory()) {
    return;
  }

  size_t fileCount = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String p = entry.name();
      if (p.endsWith(".raw")) {
        fileCount++;
      }
    }
    entry = dir.openNextFile();
  }
  dir.close();

  // If file count exceeds MAX_LITTLEFS_ARTWORKS or free flash is under 64KB, evict oldest
  if (fileCount >= MAX_LITTLEFS_ARTWORKS || freeBytes < 64 * 1024) {
    String oldestId = "";
    String remainingIds[MAX_LITTLEFS_ARTWORKS + 5];
    size_t count = 0;

    if (fsPtr->exists("/art/lru.txt")) {
      File f = fsPtr->open("/art/lru.txt", "r");
      if (f) {
        while (f.available() && count < (MAX_LITTLEFS_ARTWORKS + 5)) {
          String line = f.readStringUntil('\n');
          line.trim();
          if (line.length() > 0 && isValidId(line)) {
            if (oldestId.length() == 0) {
              oldestId = line; // First entry is the least recently used
            } else {
              remainingIds[count++] = line;
            }
          }
        }
        f.close();
      }
    }

    if (oldestId.length() > 0) {
      String targetPath = "/art/" + oldestId + ".raw";
      Serial.printf("[ArtworkCache] LittleFS quota reached (%u files, %u bytes free). LRU evicting %s...\n",
                    (unsigned int)fileCount, (unsigned int)freeBytes, targetPath.c_str());
      LittleFS.remove(targetPath);

      // Rewrite updated lru.txt
      File f = fsPtr->open("/art/lru.txt", "w");
      if (f) {
        for (size_t i = 0; i < count; i++) {
          f.println(remainingIds[i]);
        }
        f.close();
      }
    } else {
      // Fallback if lru.txt was empty or missing
      File dir2 = LittleFS.open("/art");
      if (dir2 && dir2.isDirectory()) {
        File ent = dir2.openNextFile();
        while (ent) {
          if (!ent.isDirectory() && String(ent.name()).endsWith(".raw")) {
            String pathToRemove = ent.path();
            Serial.printf("[ArtworkCache] Fallback evicting %s\n", pathToRemove.c_str());
            LittleFS.remove(pathToRemove);
            break;
          }
          ent = dir2.openNextFile();
        }
        dir2.close();
      }
    }
  }
}

bool ArtworkCache::saveArtwork(const String& artworkId, const uint8_t* buffer, size_t bufferSize) {
  if (!fsPtr || !isValidId(artworkId) || buffer == nullptr || bufferSize == 0) {
    return false;
  }

  // Manage flash capacity before writing to LittleFS
  evictOldestIfNeeded();

  String path = "/art/" + artworkId + ".raw";
  File f = fsPtr->open(path, "w");
  if (!f) {
    Serial.printf("[ArtworkCache] Failed to open %s for writing!\n", path.c_str());
    return false;
  }

  size_t written = f.write(buffer, bufferSize);
  f.close();

  if (written == bufferSize) {
    touchLRU(artworkId);
    Serial.printf("[ArtworkCache] Cached %u bytes to %s on %s\n", (unsigned int)written, path.c_str(), getStorageName());
    return true;
  }
  return false;
}
