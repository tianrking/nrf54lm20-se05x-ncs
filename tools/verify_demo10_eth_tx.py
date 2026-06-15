#!/usr/bin/env python3
"""Verify SE05x Demo10 Ethereum signing output on a PC.

The script checks the material printed by Demo10:

  ETH_SIGNING_RLP
  ETH_SIGNING_HASH_KECCAK256
  ETH_PUBLIC_KEY_UNCOMPRESSED_04XY
  ETH_FROM_ADDRESS
  ETH_SIGNATURE_DER / ETH_SIGNATURE_R / ETH_SIGNATURE_S_LOW
  ETH_RAW_TX_CANDIDATE_V0 / ETH_RAW_TX_CANDIDATE_V1

It does not need an Ethereum node. It parses the transaction fields printed by
the board, recomputes Keccak-256 and legacy RLP, and verifies the ECDSA
signature against the printed public key. Recovery-id selection is still a
wallet/phone step.
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


HEX_BLOCK_LABELS = (
    "ETH_SIGNING_RLP",
    "ETH_PUBLIC_KEY_UNCOMPRESSED_04XY",
    "ETH_SIGNATURE_DER",
)

HEX_INLINE_LABELS = (
    "ETH_SIGNING_HASH_KECCAK256",
    "ETH_FROM_ADDRESS",
    "ETH_TO",
    "ETH_DATA",
    "ETH_SIGNATURE_R",
    "ETH_SIGNATURE_S_LOW",
    "ETH_RAW_TX_CANDIDATE_V0",
    "ETH_RAW_TX_CANDIDATE_V1",
)

INT_LABELS = (
    "ETH_CHAIN_ID",
    "ETH_NONCE",
    "ETH_GAS_PRICE_WEI",
    "ETH_GAS_LIMIT",
    "ETH_VALUE_WEI",
)


def _rotl64(value: int, shift: int) -> int:
    return ((value << shift) | (value >> (64 - shift))) & ((1 << 64) - 1)


def keccak256(data: bytes) -> bytes:
    """Ethereum Keccak-256, not FIPS SHA3-256."""

    rndc = [
        0x0000000000000001,
        0x0000000000008082,
        0x800000000000808A,
        0x8000000080008000,
        0x000000000000808B,
        0x0000000080000001,
        0x8000000080008081,
        0x8000000000008009,
        0x000000000000008A,
        0x0000000000000088,
        0x0000000080008009,
        0x000000008000000A,
        0x000000008000808B,
        0x800000000000008B,
        0x8000000000008089,
        0x8000000000008003,
        0x8000000000008002,
        0x8000000000000080,
        0x000000000000800A,
        0x800000008000000A,
        0x8000000080008081,
        0x8000000000008080,
        0x0000000080000001,
        0x8000000080008008,
    ]
    rotc = [1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
            27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44]
    piln = [10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
            15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1]
    mask = (1 << 64) - 1
    st = [0] * 25
    rate = 136

    def keccakf() -> None:
        nonlocal st
        for rc in rndc:
            bc = [
                st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20]
                for i in range(5)
            ]
            for i in range(5):
                t = bc[(i + 4) % 5] ^ _rotl64(bc[(i + 1) % 5], 1)
                for j in range(0, 25, 5):
                    st[j + i] ^= t

            t = st[1]
            for i in range(24):
                j = piln[i]
                st[j], t = _rotl64(t, rotc[i]), st[j]

            for j in range(0, 25, 5):
                row = st[j:j + 5]
                for i in range(5):
                    st[j + i] ^= (~row[(i + 1) % 5]) & row[(i + 2) % 5]
                    st[j + i] &= mask

            st[0] ^= rc

    offset = 0
    while len(data) - offset >= rate:
        block = data[offset:offset + rate]
        for i, byte in enumerate(block):
            st[i // 8] ^= byte << (8 * (i % 8))
        keccakf()
        offset += rate

    block = bytearray(rate)
    block[: len(data) - offset] = data[offset:]
    block[len(data) - offset] = 0x01
    block[-1] |= 0x80
    for i, byte in enumerate(block):
        st[i // 8] ^= byte << (8 * (i % 8))
    keccakf()

    out = bytearray()
    for lane in st:
        out.extend(lane.to_bytes(8, "little"))
    return bytes(out[:32])


def rlp_encode_bytes(value: bytes) -> bytes:
    if len(value) == 1 and value[0] < 0x80:
        return value
    return _rlp_len(0x80, len(value)) + value


def rlp_encode_int(value: int) -> bytes:
    if value == 0:
        return rlp_encode_bytes(b"")
    return rlp_encode_bytes(value.to_bytes((value.bit_length() + 7) // 8, "big"))


def rlp_encode_list(items: list[bytes]) -> bytes:
    payload = b"".join(items)
    return _rlp_len(0xC0, len(payload)) + payload


def _rlp_len(offset: int, length: int) -> bytes:
    if length <= 55:
        return bytes([offset + length])
    encoded = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([offset + 55 + len(encoded)]) + encoded


def build_signing_rlp(
    nonce: int,
    gas_price: int,
    gas_limit: int,
    to_address: bytes,
    value: int,
    data: bytes,
    chain_id: int,
) -> bytes:
    return rlp_encode_list(
        [
            rlp_encode_int(nonce),
            rlp_encode_int(gas_price),
            rlp_encode_int(gas_limit),
            rlp_encode_bytes(to_address),
            rlp_encode_int(value),
            rlp_encode_bytes(data),
            rlp_encode_int(chain_id),
            rlp_encode_int(0),
            rlp_encode_int(0),
        ]
    )


def build_signed_rlp(
    nonce: int,
    gas_price: int,
    gas_limit: int,
    to_address: bytes,
    value: int,
    data: bytes,
    v: int,
    r: bytes,
    s: bytes,
) -> bytes:
    return rlp_encode_list(
        [
            rlp_encode_int(nonce),
            rlp_encode_int(gas_price),
            rlp_encode_int(gas_limit),
            rlp_encode_bytes(to_address),
            rlp_encode_int(value),
            rlp_encode_bytes(data),
            rlp_encode_int(v),
            rlp_encode_bytes(r.lstrip(b"\x00") or b""),
            rlp_encode_bytes(s.lstrip(b"\x00") or b""),
        ]
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
        for continuation in lines[index + 1:]:
            stripped = continuation.strip()
            if not stripped:
                break
            if re.match(r"^[0-9a-fA-F]{2}(?:\s+[0-9a-fA-F]{2})*$", stripped):
                hex_text += " " + stripped
                continue
            break

        return _clean_hex(hex_text)

    raise ValueError(f"missing {label} in Demo10 output")


def extract_inline_value(log_text: str, label: str) -> str:
    match = re.search(rf"^{re.escape(label)}=(.+)$", log_text, flags=re.MULTILINE)
    if match is None:
        raise ValueError(f"missing {label} in Demo10 output")
    return match.group(1).strip()


def extract_inline_hex(log_text: str, label: str) -> bytes:
    value = extract_inline_value(log_text, label)
    if value.lower().startswith("0x"):
        value = value[2:]
    if value == "":
        return b""
    return bytes.fromhex(value)


def parse_demo10_material(log_text: str) -> dict[str, bytes | int]:
    material: dict[str, bytes | int] = {}
    for label in HEX_BLOCK_LABELS:
        material[label] = extract_hex_block(log_text, label)
    for label in HEX_INLINE_LABELS:
        material[label] = extract_inline_hex(log_text, label)
    for label in INT_LABELS:
        material[label] = int(extract_inline_value(log_text, label), 10)
    material["ETH_V_CANDIDATE_0"] = int(extract_inline_value(log_text, "ETH_V_CANDIDATE_0"))
    material["ETH_V_CANDIDATE_1"] = int(extract_inline_value(log_text, "ETH_V_CANDIDATE_1"))
    return material


def verify_material(material: dict[str, bytes | int]) -> list[str]:
    public_key = material["ETH_PUBLIC_KEY_UNCOMPRESSED_04XY"]
    signing_rlp = material["ETH_SIGNING_RLP"]
    signing_hash = material["ETH_SIGNING_HASH_KECCAK256"]
    from_address = material["ETH_FROM_ADDRESS"]
    to_address = material["ETH_TO"]
    tx_data = material["ETH_DATA"]
    signature_der = material["ETH_SIGNATURE_DER"]
    r = material["ETH_SIGNATURE_R"]
    s_low = material["ETH_SIGNATURE_S_LOW"]
    raw0 = material["ETH_RAW_TX_CANDIDATE_V0"]
    raw1 = material["ETH_RAW_TX_CANDIDATE_V1"]
    v0 = material["ETH_V_CANDIDATE_0"]
    v1 = material["ETH_V_CANDIDATE_1"]
    nonce = material["ETH_NONCE"]
    gas_price = material["ETH_GAS_PRICE_WEI"]
    gas_limit = material["ETH_GAS_LIMIT"]
    value = material["ETH_VALUE_WEI"]
    chain_id = material["ETH_CHAIN_ID"]

    assert isinstance(public_key, bytes)
    assert isinstance(signing_rlp, bytes)
    assert isinstance(signing_hash, bytes)
    assert isinstance(from_address, bytes)
    assert isinstance(to_address, bytes)
    assert isinstance(tx_data, bytes)
    assert isinstance(signature_der, bytes)
    assert isinstance(r, bytes)
    assert isinstance(s_low, bytes)
    assert isinstance(raw0, bytes)
    assert isinstance(raw1, bytes)
    assert isinstance(v0, int)
    assert isinstance(v1, int)
    assert isinstance(nonce, int)
    assert isinstance(gas_price, int)
    assert isinstance(gas_limit, int)
    assert isinstance(value, int)
    assert isinstance(chain_id, int)

    checks: list[str] = []

    if len(public_key) != 65 or public_key[0] != 0x04:
        raise ValueError("ETH_PUBLIC_KEY_UNCOMPRESSED_04XY must be 65 bytes and start with 0x04")
    if len(signing_hash) != 32:
        raise ValueError("ETH_SIGNING_HASH_KECCAK256 must be 32 bytes")
    if len(from_address) != 20 or len(to_address) != 20:
        raise ValueError("ETH addresses must be 20 bytes")
    if len(r) != 32 or len(s_low) != 32:
        raise ValueError("ETH_SIGNATURE_R and ETH_SIGNATURE_S_LOW must be 32 bytes")
    checks.append("parsed public key/address/signature lengths")

    expected_signing_rlp = build_signing_rlp(
        nonce, gas_price, gas_limit, to_address, value, tx_data, chain_id
    )
    if signing_rlp != expected_signing_rlp:
        raise ValueError("ETH_SIGNING_RLP does not match the printed transaction fields")
    checks.append("recomputed RLP from printed transaction fields")

    expected_hash = keccak256(signing_rlp)
    if signing_hash != expected_hash:
        raise ValueError("ETH_SIGNING_HASH_KECCAK256 does not equal Keccak256(ETH_SIGNING_RLP)")
    checks.append("recomputed Ethereum Keccak-256 signing hash")

    expected_address = keccak256(public_key[1:])[-20:]
    if from_address != expected_address:
        raise ValueError("ETH_FROM_ADDRESS does not match Keccak256(public key X||Y)[12:]")
    checks.append("recomputed ETH address from public key")

    pub = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256K1(), public_key)
    pub.verify(signature_der, signing_hash, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    checks.append("verified SE05x DER ECDSA signature over the printed Keccak digest")

    low_s_der = utils.encode_dss_signature(int.from_bytes(r, "big"), int.from_bytes(s_low, "big"))
    pub.verify(low_s_der, signing_hash, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    checks.append("verified r/s_low form can represent the same ECDSA signature")

    expected_v0 = chain_id * 2 + 35
    expected_v1 = chain_id * 2 + 36
    if (v0, v1) != (expected_v0, expected_v1):
        raise ValueError(
            f"ETH v candidates must be {expected_v0}/{expected_v1} for chainId={chain_id}"
        )
    checks.append("checked EIP-155 v candidates for printed chainId")

    if raw0 != build_signed_rlp(
        nonce, gas_price, gas_limit, to_address, value, tx_data, v0, r, s_low
    ):
        raise ValueError("ETH_RAW_TX_CANDIDATE_V0 does not match r/s_low/v0")
    if raw1 != build_signed_rlp(
        nonce, gas_price, gas_limit, to_address, value, tx_data, v1, r, s_low
    ):
        raise ValueError("ETH_RAW_TX_CANDIDATE_V1 does not match r/s_low/v1")
    checks.append("rebuilt both raw transaction candidates from printed fields and r/s_low")

    return checks


def read_log_file(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _serial_command_bytes(command: str) -> bytes:
    if not command.endswith("\r") and not command.endswith("\n"):
        command += "\r"
    return command.encode("ascii")


def capture_from_serial(
    port: str,
    baudrate: int,
    command: str,
    timeout: float,
    setup_commands: list[str],
) -> str:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required for --port mode. Install it with: pip install pyserial"
        ) from exc

    with serial.Serial(port, baudrate=baudrate, timeout=0.2) as ser:
        ser.reset_input_buffer()
        chunks: list[bytes] = []

        for setup_command in setup_commands:
            ser.write(_serial_command_bytes(setup_command))
            ser.flush()
            time.sleep(0.2)
            pending = ser.read(4096)
            if pending:
                chunks.append(pending)

        ser.write(_serial_command_bytes(command))
        ser.flush()

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = ser.read(4096)
            if data:
                chunks.append(data)
                text = b"".join(chunks).decode("utf-8", errors="replace")
                if "se05x-eth-demo>" in text:
                    return text

        return b"".join(chunks).decode("utf-8", errors="replace")


def print_summary(material: dict[str, bytes | int]) -> None:
    print("Parsed Demo10 ETH material:")
    print(f"  chain id     : {material['ETH_CHAIN_ID']}")
    print(f"  nonce        : {material['ETH_NONCE']}")
    print(f"  from address : 0x{material['ETH_FROM_ADDRESS'].hex()}")
    print(f"  to address   : 0x{material['ETH_TO'].hex()}")
    print(f"  value wei    : {material['ETH_VALUE_WEI']}")
    print(f"  signing hash : 0x{material['ETH_SIGNING_HASH_KECCAK256'].hex()}")
    print(f"  raw tx v0    : {len(material['ETH_RAW_TX_CANDIDATE_V0'])} bytes")
    print(f"  raw tx v1    : {len(material['ETH_RAW_TX_CANDIDATE_V1'])} bytes")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Verify SE05x Demo10 ETH legacy transaction signing output."
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--log", type=Path, help="Text file containing Demo10 serial output")
    source.add_argument("--port", help="Serial port, for example COM9")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--command", default="AT+S", help="Final Demo10 command for --port mode")
    parser.add_argument(
        "--set-command",
        action="append",
        default=[],
        help=(
            "Setup command sent before --command. Repeat it to set real tx fields, "
            "for example --set-command AT+N=5 --set-command AT+V=1000000000000000"
        ),
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--save-log", type=Path, help="Optional path to save serial output")
    args = parser.parse_args(argv)

    try:
        if args.log is not None:
            log_text = read_log_file(args.log)
        else:
            log_text = capture_from_serial(
                args.port, args.baudrate, args.command, args.timeout, args.set_command
            )
            if args.save_log is not None:
                args.save_log.write_text(log_text, encoding="utf-8")

        material = parse_demo10_material(log_text)
        print_summary(material)
        checks = verify_material(material)
        print("Checks:")
        for check in checks:
            print(f"  OK - {check}")
        print("VERIFY OK: Demo10 ETH RLP, Keccak, address, signature, and raw tx candidates match.")
        print("NOTE: this script does not broadcast the transaction and does not select the final recovery id.")
        return 0
    except InvalidSignature:
        print("VERIFY FAIL: ECDSA signature does not match public key and digest.", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
