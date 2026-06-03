import unittest

from release_flasher_assets import expected_assets, normalize_tag, version_from_tag


class ReleaseAssetContractTest(unittest.TestCase):
    def test_normalize_tag_accepts_plain_version(self):
        self.assertEqual(normalize_tag("1.0.0"), "v1.0.0")

    def test_normalize_tag_keeps_tag_version(self):
        self.assertEqual(normalize_tag("v1.0.0-rc.1"), "v1.0.0-rc.1")

    def test_version_from_tag_strips_leading_v(self):
        self.assertEqual(version_from_tag("v1.0.0-rc.1"), "1.0.0-rc.1")

    def test_expected_release_assets_match_current_contract(self):
        assets = expected_assets("1.0.0")
        self.assertEqual(len(assets), 10)
        self.assertIn("lrs-firmware-1.0.0-za.bin", assets)
        self.assertIn("lrs-firmware-1.0.0-us.bin", assets)
        self.assertIn("lrs-firmware-1.0.0-eu.bin", assets)
        self.assertIn("thanda-lora-flasher-1.0.0-macos-arm64-portable.zip", assets)
        self.assertIn("thanda-lora-flasher-1.0.0-macos-x86_64-portable.zip", assets)
        self.assertNotIn("thanda-lora-flasher-1.0.0-macos-arm64.dmg", assets)


if __name__ == "__main__":
    unittest.main()
