import unittest

from version_metadata import version_dev_build, version_parts


class VersionMetadataTest(unittest.TestCase):
    def test_semver_with_suffix(self):
        self.assertEqual(version_parts("0.9.2-dev"), [0, 9, 2])

    def test_dev_build_revision_keeps_protocol_parts_stable(self):
        self.assertEqual(version_parts("0.9.2~7"), [0, 9, 2])

    def test_dev_build_revision_is_parsed_separately(self):
        self.assertEqual(version_dev_build("0.9.2~7"), 7)
        self.assertEqual(version_dev_build("v0.9.2~18"), 18)
        self.assertEqual(version_dev_build("0.9.2-dev"), 0)

    def test_dev_build_revision_is_clamped_to_uint16(self):
        self.assertEqual(version_dev_build("0.9.2~70000"), 65535)

    def test_leading_v(self):
        self.assertEqual(version_parts("v1.2.3"), [1, 2, 3])

    def test_missing_parts_default_to_zero(self):
        self.assertEqual(version_parts("1.2"), [1, 2, 0])
        self.assertEqual(version_parts("1"), [1, 0, 0])

    def test_parts_are_clamped_to_byte_range(self):
        self.assertEqual(version_parts("300.2.999"), [255, 2, 255])

    def test_empty_or_defaultish_inputs(self):
        self.assertEqual(version_parts(""), [0, 0, 0])
        self.assertEqual(version_parts(None), [0, 0, 0])
        self.assertEqual(version_parts("dev"), [0, 0, 0])


if __name__ == "__main__":
    unittest.main()
