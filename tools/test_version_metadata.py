import unittest

from version_metadata import version_parts


class VersionMetadataTest(unittest.TestCase):
    def test_semver_with_suffix(self):
        self.assertEqual(version_parts("0.9.2-dev"), [0, 9, 2])

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
