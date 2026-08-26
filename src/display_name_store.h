#pragma once
#include <stddef.h>
#include <stdint.h>
namespace display_names {
constexpr size_t kMaxNameChars = 16;
constexpr size_t kRecordCount = 13;
constexpr uint8_t kFormatVersion = 1;
#pragma pack(push, 1)
struct Header { char magic[4]; uint8_t version; uint8_t record_count; uint16_t reserved; uint32_t checksum; };
struct Record { uint32_t chip_id; char display_name[kMaxNameChars + 1]; };
#pragma pack(pop)
static_assert(sizeof(Header) == 12, "display-name header size changed");
static_assert(sizeof(Record) == 21, "display-name record size changed");
constexpr size_t kFileBytes = sizeof(Header) + sizeof(Record) * kRecordCount;
enum class RecoverySource : uint8_t { None, Primary, NewFile, Backup };
bool normalize(const char *input, char output[kMaxNameChars + 1]);
bool isPrintableNameChar(char c);
uint32_t checksumBegin();
uint32_t checksumUpdate(uint32_t checksum, const uint8_t *data, size_t len);
bool validateRecord(const Record &record);
bool validateImage(const uint8_t *data, size_t len);
RecoverySource chooseRecoverySource(bool primaryValid, bool newValid,
                                    bool backupValid);
#ifdef UNIT_TEST
bool initializeImage(uint8_t data[kFileBytes]);
bool lookupImage(const uint8_t data[kFileBytes], uint32_t chipId,
                 char output[kMaxNameChars + 1]);
bool updateImage(uint8_t data[kFileBytes], uint32_t chipId,
                 const char *displayName);
#endif
#ifndef UNIT_TEST
bool recover();
bool lookup(uint32_t chipId, char output[kMaxNameChars + 1]);
bool set(uint32_t chipId, const char *displayName);
bool clear(uint32_t chipId);
bool clearAll();
#endif
}
