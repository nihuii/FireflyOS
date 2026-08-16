import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature


ROOT = Path(__file__).resolve().parents[2]
SIGNER = ROOT / "tools" / "sign_update.py"
PRIVATE_KEY = ROOT / "tests" / "fixtures" / "update_test_private_key.pem"
PUBLIC_KEY = ROOT / "tests" / "fixtures" / "update_test_public_key.pem"


def load_signer():
    if not SIGNER.is_file():
        raise AssertionError("offline update signer must exist")
    spec = importlib.util.spec_from_file_location("firefly_sign_update", SIGNER)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def verify_manifest(signer, manifest):
    public_text = "\n".join(
        line for line in PUBLIC_KEY.read_text(encoding="ascii").splitlines()
        if not line.startswith("#")
    ).encode("ascii")
    public_key = serialization.load_pem_public_key(public_text)
    raw = bytes.fromhex(manifest["signature"])
    der = encode_dss_signature(
        int.from_bytes(raw[:32], "big"),
        int.from_bytes(raw[32:], "big"),
    )
    public_key.verify(
        der,
        signer.canonical_manifest_bytes(manifest),
        ec.ECDSA(hashes.SHA256()),
    )


class UpdateManifestTests(unittest.TestCase):
    def test_signer_derives_size_hash_and_valid_raw_p256_signature(self):
        signer = load_signer()
        with tempfile.TemporaryDirectory() as temp_dir:
            firmware = Path(temp_dir) / "Firefly.bin"
            payload = b"FireflyOS signed update fixture\0" * 37
            firmware.write_bytes(payload)
            manifest = signer.sign_firmware(
                firmware=firmware,
                product="FireflyOS",
                version="0.1.1",
                build=101,
                min_build=100,
                private_key_path=PRIVATE_KEY,
            )
        self.assertEqual(
            set(manifest),
            {"schema", "product", "version", "build", "min_build",
             "size", "sha256", "signature"},
        )
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["size"], len(payload))
        self.assertEqual(manifest["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(len(bytes.fromhex(manifest["signature"])), 64)
        verify_manifest(signer, manifest)

    def test_canonical_bytes_are_fixed_length_and_ignore_json_order(self):
        signer = load_signer()
        manifest = {
            "signature": "00" * 64,
            "sha256": "11" * 32,
            "size": 4096,
            "min_build": 100,
            "build": 101,
            "version": "0.1.1",
            "product": "FireflyOS",
            "schema": 1,
        }
        canonical = signer.canonical_manifest_bytes(manifest)
        self.assertEqual(len(canonical), 86)
        self.assertEqual(canonical[:8], b"FFOTA1\0\0")
        reordered = dict(reversed(list(manifest.items())))
        self.assertEqual(canonical, signer.canonical_manifest_bytes(reordered))

    def test_signer_rejects_invalid_metadata_curve_and_oversized_image(self):
        signer = load_signer()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            firmware = root / "Firefly.bin"
            firmware.write_bytes(b"image")
            bad_key = root / "p384.pem"
            bad_key.write_bytes(ec.generate_private_key(ec.SECP384R1()).private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            ))
            cases = (
                {"product": "", "version": "0.1.1", "build": 101,
                 "min_build": 100, "private_key_path": PRIVATE_KEY},
                {"product": "FireflyOS", "version": "x" * 16, "build": 101,
                 "min_build": 100, "private_key_path": PRIVATE_KEY},
                {"product": "FireflyOS", "version": "0.1.1", "build": 100,
                 "min_build": 100, "private_key_path": PRIVATE_KEY},
                {"product": "FireflyOS", "version": "0.1.1", "build": 101,
                 "min_build": 100, "private_key_path": bad_key},
            )
            for case in cases:
                with self.subTest(case=case), self.assertRaises(ValueError):
                    signer.sign_firmware(firmware=firmware, **case)

            firmware.write_bytes(b"x")
            with firmware.open("r+b") as stream:
                stream.truncate(0xB00000 + 1)
            with self.assertRaisesRegex(ValueError, "OTA slot"):
                signer.sign_firmware(
                    firmware, "FireflyOS", "0.1.1", 101, 100, PRIVATE_KEY
                )

    def test_cli_requires_environment_key_and_writes_actual_manifest(self):
        signer = load_signer()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            firmware = root / "Firefly.bin"
            output = root / "manifest.json"
            firmware.write_bytes(b"release candidate bytes")
            args = [
                "--firmware", str(firmware),
                "--product", "FireflyOS",
                "--version", "0.1.1",
                "--build", "101",
                "--min-build", "100",
                "--output", str(output),
            ]
            with mock.patch.dict(os.environ, {"FIREFLY_SIGNING_KEY": ""}):
                with self.assertRaisesRegex(ValueError, "FIREFLY_SIGNING_KEY"):
                    signer.main(args)
            with mock.patch.dict(
                os.environ, {"FIREFLY_SIGNING_KEY": str(PRIVATE_KEY)}
            ):
                self.assertEqual(signer.main(args), 0)
            manifest = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(manifest["size"], firmware.stat().st_size)
            verify_manifest(signer, manifest)

    def test_tampering_breaks_signature(self):
        signer = load_signer()
        with tempfile.TemporaryDirectory() as temp_dir:
            firmware = Path(temp_dir) / "Firefly.bin"
            firmware.write_bytes(b"authentic image")
            manifest = signer.sign_firmware(
                firmware, "FireflyOS", "0.1.1", 101, 100, PRIVATE_KEY
            )
        manifest["build"] = 102
        with self.assertRaises(InvalidSignature):
            verify_manifest(signer, manifest)


if __name__ == "__main__":
    unittest.main()
