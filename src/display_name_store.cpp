#include "display_name_store.h"

#include <string.h>
#ifndef UNIT_TEST
#include <LittleFS.h>
#endif
namespace display_names {
namespace {
constexpr char kMagic[4] = {'L', 'R', 'N', 'M'};

Header makeHeader(uint32_t checksum) {
  Header header{};
  memcpy(header.magic, kMagic, sizeof(kMagic));
  header.version = kFormatVersion;
  header.record_count = kRecordCount;
  header.checksum = checksum;
  return header;
}

#ifndef UNIT_TEST
constexpr char kPath[] = "/display_names.dat";
constexpr char kNewPath[] = "/display_names.new";
constexpr char kBakPath[] = "/display_names.bak";

bool validFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != kFileBytes) return false;

  Header header{};
  if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
          sizeof(header) ||
      memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
      header.version != kFormatVersion || header.record_count != kRecordCount ||
      header.reserved != 0) {
    return false;
  }

  uint32_t checksum = checksumBegin();
  uint32_t seen[kRecordCount]{};
  size_t seenCount = 0;
  for (size_t i = 0; i < kRecordCount; ++i) {
    Record record{};
    if (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) !=
            sizeof(record) ||
        !validateRecord(record)) {
      return false;
    }
    if (record.chip_id != 0) {
      for (size_t j = 0; j < seenCount; ++j) {
        if (seen[j] == record.chip_id) return false;
      }
      seen[seenCount++] = record.chip_id;
    }
    checksum = checksumUpdate(
        checksum, reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  }
  return checksum == header.checksum;
}

const char *activePath() {
  if (validFile(kPath)) return kPath;
  if (validFile(kNewPath)) return kNewPath;
  if (validFile(kBakPath)) return kBakPath;
  return nullptr;
}

bool installNew() {
  if (!validFile(kNewPath)) return false;
  LittleFS.remove(kBakPath);
  if (validFile(kPath) && !LittleFS.rename(kPath, kBakPath)) return false;
  if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
  if (!LittleFS.rename(kNewPath, kPath)) {
    if (!LittleFS.exists(kPath) && validFile(kBakPath)) {
      LittleFS.rename(kBakPath, kPath);
    }
    return false;
  }
  return validFile(kPath);
}

bool rewrite(uint32_t chipId, const char *name) {
  const char *sourcePath = activePath();
  File source;
  size_t targetIndex = kRecordCount;
  size_t firstEmptyIndex = sourcePath ? kRecordCount : 0;

  if (sourcePath) {
    source = LittleFS.open(sourcePath, "r");
    if (!source || !source.seek(sizeof(Header), SeekSet)) return false;
    for (size_t i = 0; i < kRecordCount; ++i) {
      Record record{};
      if (source.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) !=
          sizeof(record)) {
        return false;
      }
      if (record.chip_id == chipId) targetIndex = i;
      if (record.chip_id == 0 && firstEmptyIndex == kRecordCount) {
        firstEmptyIndex = i;
      }
    }
    if (!source.seek(sizeof(Header), SeekSet)) return false;
  }

  if (targetIndex == kRecordCount && name[0] != '\0') {
    targetIndex = firstEmptyIndex;
  }
  if (name[0] != '\0' && targetIndex == kRecordCount) return false;
  if (name[0] == '\0' && targetIndex == kRecordCount) return true;

  LittleFS.remove(kNewPath);
  File target = LittleFS.open(kNewPath, "w");
  if (!target) return false;

  Header header = makeHeader(0);
  if (target.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) !=
      sizeof(header)) {
    target.close();
    LittleFS.remove(kNewPath);
    return false;
  }

  uint32_t checksum = checksumBegin();
  for (size_t i = 0; i < kRecordCount; ++i) {
    Record record{};
    if (source &&
        source.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) !=
            sizeof(record)) {
      target.close();
      LittleFS.remove(kNewPath);
      return false;
    }
    if (i == targetIndex) {
      record = Record{};
      if (name[0] != '\0') {
        record.chip_id = chipId;
        memcpy(record.display_name, name, strlen(name) + 1);
      }
    }
    checksum = checksumUpdate(
        checksum, reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    if (target.write(reinterpret_cast<const uint8_t *>(&record),
                     sizeof(record)) != sizeof(record)) {
      target.close();
      LittleFS.remove(kNewPath);
      return false;
    }
  }
  if (source) source.close();

  header = makeHeader(checksum);
  if (!target.seek(0, SeekSet) ||
      target.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) !=
          sizeof(header)) {
    target.close();
    LittleFS.remove(kNewPath);
    return false;
  }
  target.flush();
  target.close();
  return installNew();
}
#endif
}

bool isPrintableNameChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' ||
         c == '.';
}

bool normalize(const char *input, char output[kMaxNameChars + 1]) {
  if (!input || !output) return false;
  while (*input == ' ') ++input;
  const char *end = input + strlen(input);
  while (end > input && end[-1] == ' ') --end;
  const size_t len = static_cast<size_t>(end - input);
  if (len > kMaxNameChars) return false;
  for (size_t i = 0; i < len; ++i) {
    if (!isPrintableNameChar(input[i])) return false;
    output[i] = input[i];
  }
  output[len] = '\0';
  return true;
}

uint32_t checksumBegin() { return 2166136261UL; }

uint32_t checksumUpdate(uint32_t checksum, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    checksum ^= data[i];
    checksum *= 16777619UL;
  }
  return checksum;
}

bool validateRecord(const Record &record) {
  if (!memchr(record.display_name, '\0', sizeof(record.display_name))) {
    return false;
  }
  if (record.chip_id == 0) return record.display_name[0] == '\0';
  char normalized[kMaxNameChars + 1];
  return normalize(record.display_name, normalized) && normalized[0] != '\0' &&
         strcmp(normalized, record.display_name) == 0;
}

bool validateImage(const uint8_t *data, size_t len) {
  if (!data || len != kFileBytes) return false;
  Header header{};
  memcpy(&header, data, sizeof(header));
  if (memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
      header.version != kFormatVersion || header.record_count != kRecordCount ||
      header.reserved != 0) {
    return false;
  }
  uint32_t checksum = checksumBegin();
  uint32_t seen[kRecordCount]{};
  size_t seenCount = 0;
  for (size_t i = 0; i < kRecordCount; ++i) {
    Record record{};
    const uint8_t *recordData =
        data + sizeof(Header) + i * sizeof(Record);
    memcpy(&record, recordData, sizeof(record));
    if (!validateRecord(record)) return false;
    if (record.chip_id != 0) {
      for (size_t j = 0; j < seenCount; ++j) {
        if (seen[j] == record.chip_id) return false;
      }
      seen[seenCount++] = record.chip_id;
    }
    checksum = checksumUpdate(checksum, recordData, sizeof(record));
  }
  return checksum == header.checksum;
}

RecoverySource chooseRecoverySource(bool primaryValid, bool newValid,
                                    bool backupValid) {
  if (primaryValid) return RecoverySource::Primary;
  if (newValid) return RecoverySource::NewFile;
  if (backupValid) return RecoverySource::Backup;
  return RecoverySource::None;
}

#ifdef UNIT_TEST
bool initializeImage(uint8_t data[kFileBytes]) {
  if (!data) return false;
  memset(data, 0, kFileBytes);
  const uint32_t checksum = checksumUpdate(
      checksumBegin(), data + sizeof(Header), sizeof(Record) * kRecordCount);
  const Header header = makeHeader(checksum);
  memcpy(data, &header, sizeof(header));
  return true;
}

bool lookupImage(const uint8_t data[kFileBytes], uint32_t chipId,
                 char output[kMaxNameChars + 1]) {
  if (!chipId || !output || !validateImage(data, kFileBytes)) return false;
  output[0] = '\0';
  for (size_t i = 0; i < kRecordCount; ++i) {
    Record record{};
    memcpy(&record, data + sizeof(Header) + i * sizeof(Record), sizeof(record));
    if (record.chip_id == chipId) {
      memcpy(output, record.display_name, sizeof(record.display_name));
      break;
    }
  }
  return true;
}

bool updateImage(uint8_t data[kFileBytes], uint32_t chipId,
                 const char *displayName) {
  if (!chipId || !validateImage(data, kFileBytes)) return false;
  char normalized[kMaxNameChars + 1];
  if (!normalize(displayName, normalized)) return false;
  size_t targetIndex = kRecordCount;
  size_t firstEmptyIndex = kRecordCount;
  for (size_t i = 0; i < kRecordCount; ++i) {
    Record record{};
    memcpy(&record, data + sizeof(Header) + i * sizeof(Record), sizeof(record));
    if (record.chip_id == chipId) targetIndex = i;
    if (record.chip_id == 0 && firstEmptyIndex == kRecordCount) {
      firstEmptyIndex = i;
    }
  }
  if (targetIndex == kRecordCount && normalized[0] != '\0') {
    targetIndex = firstEmptyIndex;
  }
  if (normalized[0] != '\0' && targetIndex == kRecordCount) return false;
  if (targetIndex < kRecordCount) {
    Record record{};
    if (normalized[0] != '\0') {
      record.chip_id = chipId;
      memcpy(record.display_name, normalized, strlen(normalized) + 1);
    }
    memcpy(data + sizeof(Header) + targetIndex * sizeof(Record), &record,
           sizeof(record));
  }
  const uint32_t checksum = checksumUpdate(
      checksumBegin(), data + sizeof(Header), sizeof(Record) * kRecordCount);
  const Header header = makeHeader(checksum);
  memcpy(data, &header, sizeof(header));
  return validateImage(data, kFileBytes);
}
#endif

#ifndef UNIT_TEST
bool recover() {
  const bool primaryValid = validFile(kPath);
  const bool newValid = !primaryValid && validFile(kNewPath);
  const bool backupValid = !primaryValid && !newValid && validFile(kBakPath);
  switch (chooseRecoverySource(primaryValid, newValid, backupValid)) {
  case RecoverySource::Primary:
    LittleFS.remove(kNewPath);
    return true;
  case RecoverySource::NewFile:
    if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
    return LittleFS.rename(kNewPath, kPath) && validFile(kPath);
  case RecoverySource::Backup:
    if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
    LittleFS.remove(kNewPath);
    return LittleFS.rename(kBakPath, kPath) && validFile(kPath);
  case RecoverySource::None:
    break;
  }
  LittleFS.remove(kPath);
  LittleFS.remove(kNewPath);
  LittleFS.remove(kBakPath);
  return !LittleFS.exists(kPath) && !LittleFS.exists(kNewPath) &&
         !LittleFS.exists(kBakPath);
}

bool lookup(uint32_t chipId, char output[kMaxNameChars + 1]) {
  if (!output || !chipId) return false;
  output[0] = '\0';
  const char *path = activePath();
  if (!path) return true;
  File file = LittleFS.open(path, "r");
  if (!file || !file.seek(sizeof(Header), SeekSet)) return false;
  for (size_t i = 0; i < kRecordCount; ++i) {
    Record record{};
    if (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) !=
        sizeof(record)) {
      return false;
    }
    if (record.chip_id == chipId) {
      memcpy(output, record.display_name, sizeof(record.display_name));
      return true;
    }
  }
  return true;
}

bool set(uint32_t chipId, const char *displayName) {
  if (!chipId) return false;
  char normalized[kMaxNameChars + 1];
  return normalize(displayName, normalized) && rewrite(chipId, normalized);
}

bool clear(uint32_t chipId) { return set(chipId, ""); }

bool clearAll() {
  LittleFS.remove(kPath);
  LittleFS.remove(kNewPath);
  LittleFS.remove(kBakPath);
  return !LittleFS.exists(kPath) && !LittleFS.exists(kNewPath) &&
         !LittleFS.exists(kBakPath);
}
#endif
}
