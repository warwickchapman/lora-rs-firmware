import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def message_types_from_header():
    text = (ROOT / "src" / "radio_protocol.h").read_text(encoding="utf-8")
    enum_body = re.search(r"enum class MessageType[^{]*\{(?P<body>.*?)\};", text, re.S)
    if not enum_body:
        raise AssertionError("MessageType enum not found")
    entries = {}
    for name, code in re.findall(r"^\s*([A-Za-z0-9_]+)\s*=\s*'([^']+)'", enum_body.group("body"), re.M):
        entries[name] = code
    return entries


class ProtocolDocsTest(unittest.TestCase):
    def test_protocol_docs_list_all_message_types(self):
        docs = (ROOT / "docs" / "PROTOCOL.md").read_text(encoding="utf-8")
        for name, code in message_types_from_header().items():
            with self.subTest(message_type=name):
                self.assertIn(f"`{name}` (`'{code}'`)", docs)


if __name__ == "__main__":
    unittest.main()
