import unittest
from chip_id_helper import parse_canonical_chip_id

class ChipIdHelperTest(unittest.TestCase):
    def test_esptool_output_with_msb(self):
        output = "Chip ID: 0x800af8d9\nMAC: 00:11:22:33:44:55"
        self.assertEqual(parse_canonical_chip_id(output), "000af8d9")

    def test_esptool_output_normal(self):
        output = "Chip ID: 0x0048cb85\nMAC: 00:11:22:33:44:56"
        self.assertEqual(parse_canonical_chip_id(output), "0048cb85")

    def test_raw_hex_with_msb(self):
        self.assertEqual(parse_canonical_chip_id("800af8d9"), "000af8d9")

    def test_prefixed_hex_with_msb(self):
        self.assertEqual(parse_canonical_chip_id("0x800af8d9"), "000af8d9")

    def test_lrs_prefix_with_msb(self):
        self.assertEqual(parse_canonical_chip_id("lrs-800af8d9"), "000af8d9")

    def test_max_u32(self):
        self.assertEqual(parse_canonical_chip_id("0xFFFFFFFF"), "00ffffff")

    def test_wider_than_u32_rejected(self):
        with self.assertRaises(ValueError):
            parse_canonical_chip_id("0x100000000")  # 33-bit
        with self.assertRaises(ValueError):
            parse_canonical_chip_id("100000000")

    def test_invalid_formats_rejected(self):
        with self.assertRaises(ValueError):
            parse_canonical_chip_id("not-a-chip-id")
        with self.assertRaises(ValueError):
            parse_canonical_chip_id("lrs-zzzzzzzz")
        with self.assertRaises(ValueError):
            parse_canonical_chip_id("0xGGGGGGGG")

    def test_password_derivation(self):
        from factory_provision import derive_password
        # Verify that the derived password for 000af8d9 matches the firmware default: a396f780
        self.assertEqual(derive_password("000af8d9"), "a396f780")

if __name__ == "__main__":
    unittest.main()
