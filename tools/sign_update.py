#!/usr/bin/env python3
"""Create a signed FireflyOS update manifest from a real firmware image."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
from typing import Mapping, Sequence

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature


SCHEMA = 1
OTA_SLOT_SIZE = 0xB00000
CANONICAL_MAGIC = b"FFOTA1\0\0"
P256_ORDER = int(
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
    16,
)


def _fixed_utf8(value: object, field: str) -> bytes:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field} must be a non-empty string")
    encoded = value.encode("utf-8")
    if len(encoded) > 15 or b"\0" in encoded:
        raise ValueError(f"{field} must fit in 15 UTF-8 bytes")
    return encoded + b"\0" * (16 - len(encoded))


def _uint32(value: object, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field} must be an integer")
    if value < 0 or value > 0xFFFFFFFF:
        raise ValueError(f"{field} is outside uint32")
    return value


def _digest_bytes(value: object) -> bytes:
    if not isinstance(value, str) or len(value) != 64:
        raise ValueError("sha256 must contain 64 hexadecimal characters")
    try:
        digest = bytes.fromhex(value)
    except ValueError as exc:
        raise ValueError("sha256 must be hexadecimal") from exc
    if len(digest) != 32:
        raise ValueError("sha256 must contain 32 bytes")
    return digest


def canonical_manifest_bytes(manifest: Mapping[str, object]) -> bytes:
    schema = manifest.get("schema")
    if schema != SCHEMA:
        raise ValueError(f"schema must be {SCHEMA}")
    product = _fixed_utf8(manifest.get("product"), "product")
    version = _fixed_utf8(manifest.get("version"), "version")
    build = _uint32(manifest.get("build"), "build")
    min_build = _uint32(manifest.get("min_build"), "min_build")
    size = _uint32(manifest.get("size"), "size")
    digest = _digest_bytes(manifest.get("sha256"))
    return CANONICAL_MAGIC + struct.pack(
        "<H16s16sIII32s",
        schema,
        product,
        version,
        build,
        min_build,
        size,
        digest,
    )


def _read_pem(path: Path) -> bytes:
    if not Path(path).is_file():
        raise ValueError("signing key must be a regular file")
    lines = Path(path).read_bytes().splitlines()
    return b"\n".join(line for line in lines if not line.lstrip().startswith(b"#")) + b"\n"


def _load_private_key(path: Path):
    try:
        key = serialization.load_pem_private_key(_read_pem(path), password=None)
    except (TypeError, ValueError) as exc:
        raise ValueError("signing key is not a valid unencrypted PEM private key") from exc
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("signing key must use ECDSA P-256")
    return key


def sign_firmware(
    firmware: Path,
    product: str,
    version: str,
    build: int,
    min_build: int,
    private_key_path: Path,
) -> dict[str, object]:
    firmware = Path(firmware)
    if not firmware.is_file():
        raise ValueError("firmware must be a regular file")
    size = firmware.stat().st_size
    if size <= 0:
        raise ValueError("firmware must not be empty")
    if size > OTA_SLOT_SIZE:
        raise ValueError("firmware exceeds the 11MB OTA slot")
    build = _uint32(build, "build")
    min_build = _uint32(min_build, "min_build")
    if build <= min_build:
        raise ValueError("build must be greater than min_build")

    digest = hashlib.sha256()
    with firmware.open("rb") as source:
        while True:
            block = source.read(64 * 1024)
            if not block:
                break
            digest.update(block)

    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "product": product,
        "version": version,
        "build": build,
        "min_build": min_build,
        "size": size,
        "sha256": digest.hexdigest(),
    }
    canonical = canonical_manifest_bytes(manifest)
    private_key = _load_private_key(Path(private_key_path))
    der_signature = private_key.sign(canonical, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_signature)
    if s > P256_ORDER // 2:
        s = P256_ORDER - s
    manifest["signature"] = (
        r.to_bytes(32, "big") + s.to_bytes(32, "big")
    ).hex()
    return manifest


def _parse_args(argv: Sequence[str] | None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--product", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", required=True, type=int)
    parser.add_argument("--min-build", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    key_value = os.environ.get("FIREFLY_SIGNING_KEY", "")
    if not key_value:
        raise ValueError("FIREFLY_SIGNING_KEY must name the offline private key")
    manifest = sign_firmware(
        firmware=args.firmware,
        product=args.product,
        version=args.version,
        build=args.build,
        min_build=args.min_build,
        private_key_path=Path(key_value),
    )
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".part")
    temporary.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, output)
    print(f"signed {args.firmware}: {manifest['size']} bytes -> {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
