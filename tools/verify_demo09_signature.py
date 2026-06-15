#!/usr/bin/env python3
"""Verify SE05x Demo09 secp256k1 signature material on a PC.

This tool verifies the signature printed by Demo09:

  PUBLIC_KEY_UNCOMPRESSED_04XY
  DIGEST_SHA256_INPUT
  SIGNATURE_DER

The SE private key is not exported. The PC only checks that the DER ECDSA
signature is valid for the printed public key and 32-byte digest.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils


HEX_LABELS = (
    "PUBLIC_KEY_UNCOMPRESSED_04XY",
    "DIGEST_SHA256_INPUT",
    "SIGNATURE_DER",
)


def _clean_hex(text: str) -> bytes:
    pairs = re.findall(r"[0-9a-fA-F]{2}", text)
    if not pairs:
        raise ValueError("no hex bytes found")
    return bytes(int(pair, 16) for pair in pairs)


def extract_hex_block(log_text: str, label: str) -> bytes:
    lines = log_text.splitlines()
    for index, line in enumerate(lines):
        marker = f"{label} len="
        if marker not in line or "hex=" not in line:
            continue

        hex_text = line.split("hex=", 1)[1]
        for continuation in lines[index + 1 :]:
            stripped = continuation.strip()
            if not stripped:
                break
            if re.match(r"^[0-9a-fA-F]{2}(?:\s+[0-9a-fA-F]{2})*$", stripped):
                hex_text += " " + stripped
                continue
            break

        return _clean_hex(hex_text)

    raise ValueError(f"missing {label} in Demo09 output")


def parse_demo09_material(log_text: str) -> dict[str, bytes]:
    return {label: extract_hex_block(log_text, label) for label in HEX_LABELS}


def verify_material(material: dict[str, bytes]) -> None:
    public_key_bytes = material["PUBLIC_KEY_UNCOMPRESSED_04XY"]
    digest = material["DIGEST_SHA256_INPUT"]
    signature = material["SIGNATURE_DER"]

    if len(public_key_bytes) != 65 or public_key_bytes[0] != 0x04:
        raise ValueError(
            "PUBLIC_KEY_UNCOMPRESSED_04XY must be 65 bytes and start with 0x04"
        )
    if len(digest) != 32:
        raise ValueError("DIGEST_SHA256_INPUT must be exactly 32 bytes")

    public_key = ec.EllipticCurvePublicKey.from_encoded_point(
        ec.SECP256K1(), public_key_bytes
    )
    public_key.verify(signature, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))


def read_log_file(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def capture_from_serial(port: str, baudrate: int, command: str, timeout: float) -> str:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required for --port mode. Install it with: pip install pyserial"
        ) from exc

    if not command.endswith("\r") and not command.endswith("\n"):
        command += "\r"

    with serial.Serial(port, baudrate=baudrate, timeout=0.2) as ser:
        ser.reset_input_buffer()
        ser.write(command.encode("ascii"))
        ser.flush()

        deadline = time.monotonic() + timeout
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            data = ser.read(4096)
            if data:
                chunks.append(data)
                text = b"".join(chunks).decode("utf-8", errors="replace")
                if "se05x-wallet-demo>" in text:
                    return text

        return b"".join(chunks).decode("utf-8", errors="replace")


def print_material_summary(material: dict[str, bytes]) -> None:
    public_key = material["PUBLIC_KEY_UNCOMPRESSED_04XY"]
    digest = material["DIGEST_SHA256_INPUT"]
    signature = material["SIGNATURE_DER"]

    print("Parsed Demo09 material:")
    print(f"  public key 04XY : {len(public_key)} bytes")
    print(f"  digest          : {digest.hex().upper()}")
    print(f"  signature DER   : {len(signature)} bytes")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Verify SE05x Demo09 secp256k1 signature output."
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--log", type=Path, help="Text file containing Demo09 serial output")
    source.add_argument("--port", help="Serial port, for example COM9")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--command", default="AT+S", help="Demo09 command for --port mode")
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument(
        "--save-log",
        type=Path,
        help="Optional path to save captured serial output in --port mode",
    )
    args = parser.parse_args(argv)

    try:
        if args.log is not None:
            log_text = read_log_file(args.log)
        else:
            log_text = capture_from_serial(
                args.port, args.baudrate, args.command, args.timeout
            )
            if args.save_log is not None:
                args.save_log.write_text(log_text, encoding="utf-8")

        material = parse_demo09_material(log_text)
        print_material_summary(material)
        verify_material(material)
        print("VERIFY OK: signature matches public key and digest.")
        return 0
    except InvalidSignature:
        print("VERIFY FAIL: signature does not match public key and digest.", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
