#include <unity.h>
#include "peer_manager.h"
#include <string.h>

void test_peer_manager_sizeof() {
  TEST_ASSERT_TRUE(sizeof(PeerRuntime) <= 256);
}

void test_peer_manager_lookup_by_address() {
  PeerManager pm;
  pm.begin(1);
  
  // Look up empty table
  TEST_ASSERT_NULL(pm.find(2));

  // Insert one peer
  PeerRuntime* p = pm.findOrCreate(2, 1002);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT8(2, p->address);
  TEST_ASSERT_EQUAL_UINT32(1002, p->chip_id);

  // Look it up
  PeerRuntime* found = pm.find(2);
  TEST_ASSERT_EQUAL_PTR(p, found);
  TEST_ASSERT_EQUAL_UINT8(2, found->address);

  // Non-existent lookup
  TEST_ASSERT_NULL(pm.find(3));

  // Invalid address lookup
  TEST_ASSERT_NULL(pm.find(0));
  TEST_ASSERT_NULL(pm.find(255));
}

void test_peer_manager_lookup_by_index() {
  PeerManager pm;
  pm.begin(1);

  pm.findOrCreate(2, 1002);
  pm.findOrCreate(3, 1003);

  TEST_ASSERT_EQUAL_UINT32(2, pm.count());

  PeerRuntime* p0 = pm.findByIndex(0);
  TEST_ASSERT_NOT_NULL(p0);
  TEST_ASSERT_EQUAL_UINT8(2, p0->address);

  PeerRuntime* p1 = pm.findByIndex(1);
  TEST_ASSERT_NOT_NULL(p1);
  TEST_ASSERT_EQUAL_UINT8(3, p1->address);

  // Bounds check
  TEST_ASSERT_NULL(pm.findByIndex(2));
}

void test_peer_manager_full_saturation() {
  PeerManager pm;
  pm.begin(1);

  for (size_t i = 0; i < LRS_MAX_PEERS; ++i) {
    uint8_t addr = i + 2;
    PeerRuntime* p = pm.findOrCreate(addr, 1000 + addr);
    TEST_ASSERT_NOT_NULL(p);
  }
  TEST_ASSERT_EQUAL_UINT32(LRS_MAX_PEERS, pm.count());

  // Subsequent insertion must return nullptr (No active-peer eviction)
  PeerRuntime* pExtra = pm.findOrCreate(100, 9999);
  TEST_ASSERT_NULL(pExtra);
  TEST_ASSERT_EQUAL_UINT32(LRS_MAX_PEERS, pm.count());
}

void test_peer_manager_forget_and_compaction() {
  PeerManager pm;
  pm.begin(1);

  pm.findOrCreate(2, 1002);
  pm.findOrCreate(3, 1003);
  pm.findOrCreate(4, 1004);

  TEST_ASSERT_EQUAL_UINT32(3, pm.count());

  // Forget the middle peer (address 3)
  TEST_ASSERT_TRUE(pm.forget(3));
  TEST_ASSERT_EQUAL_UINT32(2, pm.count());

  // Ensure peers are compacted (address 4 moves to index 1)
  PeerRuntime* p0 = pm.findByIndex(0);
  TEST_ASSERT_EQUAL_UINT8(2, p0->address);

  PeerRuntime* p1 = pm.findByIndex(1);
  TEST_ASSERT_EQUAL_UINT8(4, p1->address);

  TEST_ASSERT_NULL(pm.findByIndex(2));

  // Forget non-existent
  TEST_ASSERT_FALSE(pm.forget(99));
}

void test_peer_manager_snapshot_consistency() {
  PeerManager pm;
  pm.begin(1);

  PeerRuntime* p = pm.findOrCreate(2, 1002);
  TEST_ASSERT_NOT_NULL(p);
  p->relay_state = 1;
  p->relay_state_known = true;
  p->input_state = 0;
  p->input_state_known = true;
  p->uplink_rssi = -70;
  p->wifi_rssi_dbm = -65;
  p->min_flasher_compat_revision = 7;

  PeerStatusSnapshot snap{};
  TEST_ASSERT_TRUE(pm.buildStatusSnapshot(0, snap));
  TEST_ASSERT_EQUAL_UINT8(2, snap.address);
  TEST_ASSERT_EQUAL_UINT8(1, snap.relay_state);
  TEST_ASSERT_TRUE(snap.relay_state_known);
  TEST_ASSERT_EQUAL_UINT8(0, snap.input_state);
  TEST_ASSERT_TRUE(snap.input_state_known);
  TEST_ASSERT_EQUAL_INT(-70, snap.uplink_rssi);
  TEST_ASSERT_EQUAL_INT(-65, snap.wifi_rssi_dbm);
  TEST_ASSERT_EQUAL_UINT32(1002, snap.chip_id);
  TEST_ASSERT_EQUAL_UINT8(7, snap.min_flasher_compat_revision);

  // Bounds check snapshot
  TEST_ASSERT_FALSE(pm.buildStatusSnapshot(1, snap));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_peer_manager_sizeof);
  RUN_TEST(test_peer_manager_lookup_by_address);
  RUN_TEST(test_peer_manager_lookup_by_index);
  RUN_TEST(test_peer_manager_full_saturation);
  RUN_TEST(test_peer_manager_forget_and_compaction);
  RUN_TEST(test_peer_manager_snapshot_consistency);
  return UNITY_END();
}
