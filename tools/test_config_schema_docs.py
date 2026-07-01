import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG_CPP = ROOT / "src" / "config_store.cpp"
CONFIG_SERIALIZER_CPP = ROOT / "src" / "config_serializer.cpp"
CONFIG_FIELDS_CPP = ROOT / "src" / "config_fields.cpp"

# Explicit table of derived/metadata fields emitted by writeSettingsJson
# but not persisted in kAllowedFields, with short reasons.
DERIVED_METADATA_EXCEPTIONS = {
    "wifi_sta_password_set": "Boolean flag indicating if wifi_sta_password is set",
    "mqtt_password_set": "Boolean flag indicating if mqtt_password is set",
    "fleet_passphrase_set": "Boolean flag indicating if fleet_passphrase is set",
    "fleet_passphrase_default": "Boolean flag indicating if fleet_passphrase is still the default value",
    "admin_password_set": "Boolean flag indicating if admin_password is set",
    "computed_lan_hostname": "Status value returning either configured lan_hostname or default lrs-<chip_id>",
    "role": "Derived string flag indicating if the device is a gateway or remote (derived from role_tx)"
}



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
    text = CONFIG_SERIALIZER_CPP.read_text(encoding="utf-8")
    # Look for writeSettingsJsonObject (the real implementation) first,
    # fall back to writeSettingsJson (the wrapper) for older code.
    write_match = re.search(r"void writeSettingsJsonObject\(.*?\{(?P<body>.*?)\n\}", text, re.S)
    if not write_match:
        write_match = re.search(r"void writeSettingsJson\(.*?\{(?P<body>.*?)\n\}", text, re.S)
    if not write_match:
        raise AssertionError("writeSettingsJsonObject/writeSettingsJson body not found")
    body = write_match.group("body")
    fields = set(re.findall(r'(?:doc|config)\["([^"]+)"\]', body))
    fields.update(re.findall(r'writeAddressArray(?:Impl)?\((?:doc|config),\s*"([^"]+)"', body))
    return fields


def config_fields_classification():
    text = CONFIG_FIELDS_CPP.read_text(encoding="utf-8")
    fields = {}
    matches = re.findall(r'\{\s*"([^"]+)"\s*,\s*ConfigFieldClass::(\w+)\s*\}', text)
    for name, classification in matches:
        fields[name] = classification
    return fields


class ConfigSchemaDocsTest(unittest.TestCase):
    def test_allowed_config_fields_are_saved(self):
        self.assertEqual(allowed_fields(), saved_fields())

    def test_serial_admin_exposes_persistent_config_fields(self):
        admin_fields = serial_admin_config_fields()
        internal_fields = {"known_peer_chip_ids"}
        # Remove computed/metadata suffix fields from the writeSettingsJson list for allowed fields comparison
        stripped_admin_fields = admin_fields - set(DERIVED_METADATA_EXCEPTIONS.keys())
        missing = (allowed_fields() - internal_fields) - stripped_admin_fields
        self.assertEqual(missing, set(), f"Persisted fields in kAllowedFields missing from writeSettingsJson: {missing}")

    def test_all_allowed_fields_classified(self):
        allowed = allowed_fields()
        classifications = config_fields_classification()
        missing = allowed - set(classifications.keys())
        self.assertEqual(missing, set(), f"Fields in kAllowedFields missing classification: {missing}")
        extra = set(classifications.keys()) - allowed
        self.assertEqual(extra, set(), f"Classified fields not in kAllowedFields: {extra}")

    def test_retained_config_fields_are_non_secret(self):
        classifications = config_fields_classification()
        secret_keywords = ["password", "passphrase", "secret"]
        for name, classification in classifications.items():
            if classification == "RetainedConfig":
                for keyword in secret_keywords:
                    self.assertNotIn(keyword, name, f"RetainedConfig field '{name}' contains secret keyword '{keyword}'!")
            elif classification == "SecretMetadata":
                has_secret_keyword = any(keyword in name for keyword in secret_keywords)
                self.assertTrue(has_secret_keyword, f"SecretMetadata field '{name}' does not contain any secret keywords!")

    def test_all_emitted_json_fields_are_classified_or_excepted(self):
        emitted_fields = serial_admin_config_fields()
        classifications = config_fields_classification()
        for field in emitted_fields:
            is_classified = (field in classifications)
            is_excepted = (field in DERIVED_METADATA_EXCEPTIONS)
            self.assertTrue(
                is_classified or is_excepted,
                f"Field '{field}' emitted by writeSettingsJson is neither classified in kConfigFields nor listed in DERIVED_METADATA_EXCEPTIONS!"
            )


if __name__ == "__main__":
    unittest.main()
