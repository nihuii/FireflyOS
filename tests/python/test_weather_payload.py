import struct
import unittest


SCHEMA = 1
MAX_CITY_BYTES = 31
STALE_AFTER = 3 * 60 * 60
OLD_AFTER = 24 * 60 * 60


def encode_weather(city, current, high, low, code, updated):
    city_bytes = city.encode("utf-8")
    if not 1 <= len(city_bytes) <= MAX_CITY_BYTES:
        raise ValueError("city length")
    if not all(-1000 <= value <= 700 for value in (current, high, low)):
        raise ValueError("temperature")
    if high < low or not 0 <= code <= 0xFFFF or updated <= 0:
        raise ValueError("fields")
    return struct.pack("<BBHhhhq", SCHEMA, len(city_bytes), code,
                       current, high, low, updated) + city_bytes


def decode_weather(payload, now):
    if len(payload) < 19:
        return None
    schema, city_len, code, current, high, low, updated = struct.unpack(
        "<BBHhhhq", payload[:18]
    )
    if schema != SCHEMA or city_len not in range(1, MAX_CITY_BYTES + 1):
        return None
    if len(payload) != 18 + city_len:
        return None
    try:
        city = payload[18:].decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        return None
    if not all(-1000 <= value <= 700 for value in (current, high, low)):
        return None
    if high < low or updated <= 0 or updated > now + 300:
        return None
    return city, current, high, low, code, updated


def freshness(updated, now):
    age = max(0, now - updated)
    if age > OLD_AFTER:
        return "old"
    if age > STALE_AFTER:
        return "stale"
    return "fresh"


class WeatherPayloadContractTests(unittest.TestCase):
    def test_phone_payload_round_trip_and_bounds(self):
        payload = encode_weather("深圳", 255, 301, 220, 2, 1_000)
        self.assertEqual(
            ("深圳", 255, 301, 220, 2, 1_000),
            decode_weather(payload, 1_060),
        )
        with self.assertRaises(ValueError):
            encode_weather("x" * 32, 255, 301, 220, 2, 1_000)
        with self.assertRaises(ValueError):
            encode_weather("深圳", 1_001, 301, 220, 2, 1_000)
        self.assertIsNone(decode_weather(payload[:-1], 1_060))

    def test_three_and_twenty_four_hour_boundaries(self):
        self.assertEqual("fresh", freshness(1_000, 1_000 + STALE_AFTER))
        self.assertEqual("stale", freshness(1_000, 1_001 + STALE_AFTER))
        self.assertEqual("stale", freshness(1_000, 1_000 + OLD_AFTER))
        self.assertEqual("old", freshness(1_000, 1_001 + OLD_AFTER))

    def test_timestamp_and_utf8_validation(self):
        payload = bytearray(encode_weather("深圳", 255, 301, 220, 2, 1_000))
        payload[-1] = 0xFF
        self.assertIsNone(decode_weather(payload, 1_060))
        future = encode_weather("深圳", 255, 301, 220, 2, 1_361)
        self.assertIsNone(decode_weather(future, 1_060))


if __name__ == "__main__":
    unittest.main()
