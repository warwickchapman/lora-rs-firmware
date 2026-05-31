import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG_CPP = ROOT / "src" / "config_store.cpp"
SERIAL_ADMIN_CPP = ROOT / "src" / "serial_admin.cpp"


def allowed_fields():
    text = CONFIG_CPP.read_text(encoding="utf-8")
    match = re.search(r"kAllowedFields\[\]\s*=\s*\{(?P<body>.*?)\};", text, re.S)
    if not match:
        raise AssertionError("kAllowedFields not found")
    return set(re.findall(r'"([^"]+)"', match.group("body")))


def saved_fields():
    text = CONFIG_CPP.read_text(encoding="utf-8")
    save_match = re.search(r"bool ConfigStore::save\(\).*?\{(?P<body>.*?)const size_t estimatedBytes", text, re.S)
    if not save_match:
        raise AssertionError("ConfigStore::save body not found")
    body = save_match.group("body")
    fields = set(re.findall(r'doc\["([^"]+)"\]', body))
    fields.update(re.findall(r'writeAddressList\(doc,\s*"([^"]+)"', body))
    return fields


def serial_admin_config_fields():
    text = SERIAL_ADMIN_CPP.read_text(encoding="utf-8")
    write_match = re.search(r"void writeSettingsJson\(.*?\{(?P<body>.*?)\n\}", text, re.S)
    if not write_match:
        raise AssertionError("writeSettingsJson body not found")
    body = write_match.group("body")
    fields = set(re.findall(r'doc\["([^"]+)"\]', body))
    fields.update(re.findall(r'writeAddressArray\(doc,\s*"([^"]+)"', body))
    return fields


class ConfigSchemaDocsTest(unittest.TestCase):
    def test_allowed_config_fields_are_saved(self):
        self.assertEqual(allowed_fields(), saved_fields())

    def test_serial_admin_exposes_persistent_config_fields(self):
        admin_fields = serial_admin_config_fields()
        internal_fields = {"known_peer_chip_ids"}
        missing = (allowed_fields() - internal_fields) - admin_fields
        self.assertEqual(missing, set())


if __name__ == "__main__":
    unittest.main()
