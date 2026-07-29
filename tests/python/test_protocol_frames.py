import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAGIC = b"FF"
VERSION = 1
HEADER_SIZE = 11
MAX_PAYLOAD = 1024
ACK_REQUIRED = 0x01
FRAGMENT = 0x04
LAST_FRAGMENT = 0x08

HELLO_GOLDEN = bytes.fromhex("4646010100010000004de0")
NOTIFICATION_GOLDEN = bytes.fromhex(
    "464601200134120600e525e69da5e794b5"
)


def crc16_ccitt(data: bytes, seed: int = 0xFFFF) -> int:
    crc = seed
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(message_type: int, flags: int, sequence: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    prefix = MAGIC + bytes((VERSION, message_type, flags))
    prefix += struct.pack("<HH", sequence, len(payload))
    crc = crc16_ccitt(prefix + payload)
    return prefix + struct.pack("<H", crc) + payload


def decode_frame(encoded: bytes) -> tuple[int, int, int, bytes]:
    if len(encoded) < HEADER_SIZE:
        raise ValueError("too short")
    if encoded[:2] != MAGIC:
        raise ValueError("bad magic")
    if encoded[2] != VERSION:
        raise ValueError("bad version")
    payload_length = struct.unpack_from("<H", encoded, 7)[0]
    if payload_length > MAX_PAYLOAD:
        raise ValueError("payload too large")
    if len(encoded) != HEADER_SIZE + payload_length:
        raise ValueError("length mismatch")
    expected_crc = struct.unpack_from("<H", encoded, 9)[0]
    actual_crc = crc16_ccitt(encoded[:9] + encoded[11:])
    if actual_crc != expected_crc:
        raise ValueError("crc mismatch")
    return encoded[3], encoded[4], struct.unpack_from("<H", encoded, 5)[0], encoded[11:]


class SequenceWindow:
    def __init__(self) -> None:
        self._has_value = False
        self._latest = 0

    def accept(self, sequence: int) -> bool:
        if not self._has_value:
            self._has_value = True
            self._latest = sequence
            return True
        delta = (sequence - self._latest) & 0xFFFF
        if delta == 0 or delta >= 0x8000:
            return False
        self._latest = sequence
        return True


def reassemble(fragments: list[bytes]) -> bytes:
    output = bytearray()
    expected_sequence = None
    expected_count = None
    for expected_index, encoded in enumerate(fragments):
        message_type, flags, sequence, payload = decode_frame(encoded)
        if not flags & FRAGMENT or len(payload) < 2:
            raise ValueError("not a fragment")
        index, count = payload[:2]
        if index != expected_index or count == 0 or count > 7:
            raise ValueError("bad fragment order")
        if expected_sequence is None:
            expected_sequence, expected_count = sequence, count
        if sequence != expected_sequence or count != expected_count:
            raise ValueError("fragment mismatch")
        if message_type != 0x20:
            raise ValueError("message type mismatch")
        output.extend(payload[2:])
        if len(output) > MAX_PAYLOAD:
            raise ValueError("payload too large")
        is_last = bool(flags & LAST_FRAGMENT)
        if is_last != (index + 1 == count):
            raise ValueError("bad last fragment")
    if len(fragments) != expected_count:
        raise ValueError("incomplete fragments")
    return bytes(output)


class ProtocolFrameTests(unittest.TestCase):
    def test_empty_hello_golden_frame(self):
        self.assertEqual(HELLO_GOLDEN, encode_frame(0x01, 0, 1, b""))
        self.assertEqual((0x01, 0, 1, b""), decode_frame(HELLO_GOLDEN))

    def test_utf8_notification_golden_frame(self):
        payload = "来电".encode("utf-8")
        self.assertEqual(
            NOTIFICATION_GOLDEN,
            encode_frame(0x20, ACK_REQUIRED, 0x1234, payload),
        )
        self.assertEqual(payload, decode_frame(NOTIFICATION_GOLDEN)[3])

    def test_bad_crc_is_rejected(self):
        corrupted = bytearray(NOTIFICATION_GOLDEN)
        corrupted[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "crc mismatch"):
            decode_frame(bytes(corrupted))

    def test_duplicate_sequence_is_rejected_after_complete_frame(self):
        window = SequenceWindow()
        self.assertTrue(window.accept(0x1234))
        self.assertFalse(window.accept(0x1234))
        self.assertTrue(window.accept(0x1235))
        self.assertFalse(window.accept(0x1234))

    def test_three_fragments_reassemble_in_order(self):
        payload = "萤火虫协议分片".encode("utf-8")
        chunks = (payload[:7], payload[7:15], payload[15:])
        fragments = [
            encode_frame(
                0x20,
                FRAGMENT | (LAST_FRAGMENT if index == 2 else 0),
                77,
                bytes((index, 3)) + chunk,
            )
            for index, chunk in enumerate(chunks)
        ]
        self.assertEqual(payload, reassemble(fragments))

    def test_1025_byte_payload_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "payload too large"):
            encode_frame(0x20, 0, 1, bytes(1025))

    def test_frozen_protocol_files_and_production_codec_exist(self):
        protocol_types = ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "protocol" / "ProtocolTypes.h"
        frame_header = protocol_types.with_name("FrameCodec.h")
        frame_source = protocol_types.with_name("FrameCodec.cpp")
        document = ROOT / "docs" / "模块说明" / "07-BLE协议.md"
        for path in (protocol_types, frame_header, frame_source, document):
            self.assertTrue(path.is_file(), f"missing protocol artifact: {path}")

        types_text = protocol_types.read_text(encoding="utf-8")
        docs_text = document.read_text(encoding="utf-8")
        for value in (
            "7b7f0001-4f53-4653-8000-ff1e00000001",
            "7b7f0002-4f53-4653-8000-ff1e00000001",
            "7b7f0003-4f53-4653-8000-ff1e00000001",
            "7b7f0004-4f53-4653-8000-ff1e00000001",
            HELLO_GOLDEN.hex(),
            NOTIFICATION_GOLDEN.hex(),
        ):
            self.assertTrue(value in types_text or value in docs_text, value)

        codec_text = frame_header.read_text(encoding="utf-8") + frame_source.read_text(encoding="utf-8")
        self.assertIn("uint8_t payload[kMaxPayload]", codec_text)
        self.assertNotIn("std::vector", codec_text)
        self.assertNotIn("String", codec_text)

    def test_unknown_message_type_returns_protocol_error(self):
        source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "ConnectivityService.cpp"
        ).read_text(encoding="utf-8")
        start = source.index("void ConnectivityService::processEncoded")
        end = source.index("void ConnectivityService::processFrame", start)
        body = source[start:end]
        self.assertIn("isKnownMessageType", body)
        self.assertIn("sendProtocolError", body)


if __name__ == "__main__":
    unittest.main()
