#include <unity.h>
#include "sensor_registry.h"

void test_sensor_reading_sizeof() {
  TEST_ASSERT_EQUAL_UINT32(8, sizeof(SensorReading));
}

void test_sensor_registry_upsert_overwrite() {
  SensorRegistry reg;
  reg.clear();

  SensorReading r1{100, SensorKind::TemperatureC, SensorState::Ok, 0, 1};
  TEST_ASSERT_TRUE(reg.upsert(r1));
  TEST_ASSERT_EQUAL_UINT8(1, reg.count());

  // Upsert same kind/instance to overwrite value
  SensorReading r2{150, SensorKind::TemperatureC, SensorState::Ok, 0, 1};
  TEST_ASSERT_TRUE(reg.upsert(r2));
  TEST_ASSERT_EQUAL_UINT8(1, reg.count());

  SensorReading out{};
  TEST_ASSERT_TRUE(reg.find(SensorKind::TemperatureC, 0, out));
  TEST_ASSERT_EQUAL_INT32(150, out.value);
}

void test_sensor_registry_full_capacity() {
  SensorRegistry reg;
  reg.clear();

  for (uint8_t i = 0; i < SensorRegistry::MAX_SENSORS; ++i) {
    SensorReading r{i * 10, SensorKind::TemperatureC, SensorState::Ok, i, 1};
    TEST_ASSERT_TRUE(reg.upsert(r));
  }
  TEST_ASSERT_EQUAL_UINT8(SensorRegistry::MAX_SENSORS, reg.count());

  // Attempt 7th upsert (should fail)
  SensorReading rExtra{999, SensorKind::DryContact, SensorState::Ok, 0, 0};
  TEST_ASSERT_FALSE(reg.upsert(rExtra));
  TEST_ASSERT_EQUAL_UINT8(SensorRegistry::MAX_SENSORS, reg.count());
}

void test_sensor_registry_find() {
  SensorRegistry reg;
  reg.clear();

  SensorReading r1{1, SensorKind::DryContact, SensorState::Ok, 0, 0};
  SensorReading r2{245, SensorKind::TemperatureC, SensorState::Ok, 0, 1};
  TEST_ASSERT_TRUE(reg.upsert(r1));
  TEST_ASSERT_TRUE(reg.upsert(r2));

  SensorReading out{};
  TEST_ASSERT_TRUE(reg.find(SensorKind::DryContact, 0, out));
  TEST_ASSERT_EQUAL_INT32(1, out.value);

  TEST_ASSERT_FALSE(reg.find(SensorKind::TankLevel, 0, out));
}

void test_sensor_registry_clamp_int32_to_int16() {
  // Compression clamping logic helper test
  auto compressValue = [](int32_t val, SensorState st, SensorState &outSt) -> int16_t {
    outSt = st;
    if (val < -32768) {
      return -32768;
    } else if (val > 32767) {
      outSt = SensorState::Overrange;
      return 32767;
    }
    return static_cast<int16_t>(val);
  };

  SensorState outSt;
  TEST_ASSERT_EQUAL_INT16(500, compressValue(500, SensorState::Ok, outSt));
  TEST_ASSERT_EQUAL(SensorState::Ok, outSt);

  TEST_ASSERT_EQUAL_INT16(32767, compressValue(40000, SensorState::Ok, outSt));
  TEST_ASSERT_EQUAL(SensorState::Overrange, outSt);

  TEST_ASSERT_EQUAL_INT16(-32768, compressValue(-50000, SensorState::Ok, outSt));
  TEST_ASSERT_EQUAL(SensorState::Ok, outSt);
}

void test_sensor_registry_compression_decompression() {
  SensorReading orig{125, SensorKind::TemperatureC, SensorState::Ok, 2, 1};

  // Simulate compression to wire format
  int32_t val = orig.value;
  SensorState st = orig.state;
  if (val < -32768) val = -32768;
  else if (val > 32767) { val = 32767; st = SensorState::Overrange; }
  int16_t val16 = static_cast<int16_t>(val);

  uint8_t wire[4];
  wire[0] = (static_cast<uint8_t>(orig.kind) << 4) | (orig.instance & 0x0F);
  wire[1] = (static_cast<uint8_t>(st) << 4) | (orig.scale & 0x0F);
  wire[2] = static_cast<uint8_t>(val16 & 0xFFU);
  wire[3] = static_cast<uint8_t>((val16 >> 8) & 0xFFU);

  // Decompress from wire format
  uint8_t kindVal = wire[0] >> 4;
  uint8_t instVal = wire[0] & 0x0F;
  SensorReading decomp{};
  decomp.kind = static_cast<SensorKind>(kindVal);
  decomp.instance = instVal;
  decomp.state = static_cast<SensorState>(wire[1] >> 4);
  decomp.scale = wire[1] & 0x0F;
  int16_t valDec16 = static_cast<int16_t>(wire[2] | (static_cast<uint16_t>(wire[3]) << 8));
  decomp.value = valDec16;

  TEST_ASSERT_EQUAL(static_cast<uint8_t>(orig.kind), static_cast<uint8_t>(decomp.kind));
  TEST_ASSERT_EQUAL_UINT8(orig.instance, decomp.instance);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(orig.state), static_cast<uint8_t>(decomp.state));
  TEST_ASSERT_EQUAL_UINT8(orig.scale, decomp.scale);
  TEST_ASSERT_EQUAL_INT32(orig.value, decomp.value);
}

void test_sensor_registry_multiple_instances() {
  SensorRegistry reg;
  reg.clear();

  SensorReading t0{220, SensorKind::TemperatureC, SensorState::Ok, 0, 1};
  SensorReading t1{250, SensorKind::TemperatureC, SensorState::Ok, 1, 1};
  TEST_ASSERT_TRUE(reg.upsert(t0));
  TEST_ASSERT_TRUE(reg.upsert(t1));

  SensorReading out{};
  TEST_ASSERT_TRUE(reg.find(SensorKind::TemperatureC, 0, out));
  TEST_ASSERT_EQUAL_INT32(220, out.value);

  TEST_ASSERT_TRUE(reg.find(SensorKind::TemperatureC, 1, out));
  TEST_ASSERT_EQUAL_INT32(250, out.value);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_sensor_reading_sizeof);
  RUN_TEST(test_sensor_registry_upsert_overwrite);
  RUN_TEST(test_sensor_registry_full_capacity);
  RUN_TEST(test_sensor_registry_find);
  RUN_TEST(test_sensor_registry_clamp_int32_to_int16);
  RUN_TEST(test_sensor_registry_compression_decompression);
  RUN_TEST(test_sensor_registry_multiple_instances);
  return UNITY_END();
}
