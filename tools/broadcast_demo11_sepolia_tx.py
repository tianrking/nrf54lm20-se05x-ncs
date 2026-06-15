#!/usr/bin/env python3
"""Build, verify, select, and optionally broadcast a Demo11 Sepolia transaction.

The signing key stays inside SE05x. This script only:
  1. asks the board for its stable ETH address,
  2. queries Sepolia RPC for nonce/gas/chainId,
  3. sends transaction fields to the board over UART,
  4. verifies the board output locally,
  5. selects the raw transaction candidate whose recovered address matches,
  6. broadcasts only when --broadcast is explicitly passed.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.request
from pathlib import Path

from verify_demo10_eth_tx import extract_inline_hex, parse_demo10_material, verify_material


SECP256K1_P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
SECP256K1_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
SECP256K1_GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
SECP256K1_GY = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8


Point = tuple[int, int] | None


def mod_inv(value: int, modulus: int) -> int:
    return pow(value, -1, modulus)


def point_add(p1: Point, p2: Point) -> Point:
    if p1 is None:
        return p2
    if p2 is None:
        return p1

    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % SECP256K1_P == 0:
        return None

    if p1 == p2:
        slope = (3 * x1 * x1) * mod_inv(2 * y1 % SECP256K1_P, SECP256K1_P)
    else:
        slope = (y2 - y1) * mod_inv((x2 - x1) % SECP256K1_P, SECP256K1_P)
    slope %= SECP256K1_P

    x3 = (slope * slope - x1 - x2) % SECP256K1_P
    y3 = (slope * (x1 - x3) - y1) % SECP256K1_P
    return (x3, y3)


def point_mul(k: int, point: Point) -> Point:
    result: Point = None
    addend = point

    while k:
        if k & 1:
            result = point_add(result, addend)
        addend = point_add(addend, addend)
        k >>= 1

    return result


def recover_public_key(digest: bytes, r_bytes: bytes, s_bytes: bytes, recovery_id: int) -> bytes:
    if recovery_id not in (0, 1):
        raise ValueError(f"unsupported recovery id {recovery_id}; expected 0 or 1")

    r = int.from_bytes(r_bytes, "big")
    s = int.from_bytes(s_bytes, "big")
    e = int.from_bytes(digest, "big")
    if not (1 <= r < SECP256K1_N and 1 <= s < SECP256K1_N):
        raise ValueError("invalid r/s range")

    x = r
    alpha = (pow(x, 3, SECP256K1_P) + 7) % SECP256K1_P
    beta = pow(alpha, (SECP256K1_P + 1) // 4, SECP256K1_P)
    y = beta if (beta & 1) == recovery_id else SECP256K1_P - beta
    r_point: Point = (x, y)

    if point_mul(SECP256K1_N, r_point) is not None:
        raise ValueError("invalid recovery point")

    g = (SECP256K1_GX, SECP256K1_GY)
    s_r = point_mul(s, r_point)
    e_g = point_mul(e % SECP256K1_N, g)
    neg_e_g = None if e_g is None else (e_g[0], (-e_g[1]) % SECP256K1_P)
    q = point_mul(mod_inv(r, SECP256K1_N), point_add(s_r, neg_e_g))
    if q is None:
        raise ValueError("recovered point at infinity")

    return b"\x04" + q[0].to_bytes(32, "big") + q[1].to_bytes(32, "big")


def keccak256(data: bytes) -> bytes:
    from verify_demo10_eth_tx import keccak256 as _keccak256

    return _keccak256(data)


def select_raw_transaction(material: dict[str, bytes | int]) -> tuple[int, bytes]:
    from_address = material["ETH_FROM_ADDRESS"]
    digest = material["ETH_SIGNING_HASH_KECCAK256"]
    r = material["ETH_SIGNATURE_R"]
    s = material["ETH_SIGNATURE_S_LOW"]
    chain_id = material["ETH_CHAIN_ID"]

    assert isinstance(from_address, bytes)
    assert isinstance(digest, bytes)
    assert isinstance(r, bytes)
    assert isinstance(s, bytes)
    assert isinstance(chain_id, int)

    candidates = [
        (material["ETH_V_CANDIDATE_0"], material["ETH_RAW_TX_CANDIDATE_V0"]),
        (material["ETH_V_CANDIDATE_1"], material["ETH_RAW_TX_CANDIDATE_V1"]),
    ]
    for v, raw_tx in candidates:
        assert isinstance(v, int)
        assert isinstance(raw_tx, bytes)
        recovery_id = v - (chain_id * 2 + 35)
        pub = recover_public_key(digest, r, s, recovery_id)
        recovered_address = keccak256(pub[1:])[-20:]
        if recovered_address == from_address:
            return v, raw_tx

    raise ValueError("neither raw tx candidate recovers to ETH_FROM_ADDRESS")


def rpc_call(rpc_url: str, method: str, params: list[object]) -> object:
    payload = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode()
    request = urllib.request.Request(
        rpc_url,
        data=payload,
        headers={"content-type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        body = json.loads(response.read().decode())
    if "error" in body:
        raise RuntimeError(f"{method} RPC error: {body['error']}")
    return body["result"]


def rpc_quantity(value: int) -> str:
    return hex(value)


def parse_rpc_quantity(value: object) -> int:
    if not isinstance(value, str) or not value.startswith("0x"):
        raise ValueError(f"invalid RPC quantity: {value!r}")
    return int(value, 16)


def serial_send_and_wait(ser, command: str, prompt: str, timeout: float) -> str:
    if not command.endswith("\r") and not command.endswith("\n"):
        command += "\r"
    ser.write(command.encode("ascii"))
    ser.flush()

    deadline = time.monotonic() + timeout
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        data = ser.read(4096)
        if data:
            chunks.append(data)
            text = b"".join(chunks).decode("utf-8", errors="replace")
            if prompt in text:
                return text
    return b"".join(chunks).decode("utf-8", errors="replace")


def serial_sign_with_board(
    port: str,
    baudrate: int,
    timeout: float,
    rpc_url: str,
    to_address: str,
    value_wei: int,
    gas_limit: int,
    data_hex: str,
) -> str:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required: python -m pip install pyserial") from exc

    prompt = "se05x-eth-testnet>"
    with serial.Serial(port, baudrate=baudrate, timeout=0.2) as ser:
        ser.reset_input_buffer()
        address_log = serial_send_and_wait(ser, "AT+A", prompt, timeout)
        from_address = extract_inline_hex(address_log, "ETH_FROM_ADDRESS")
        from_hex = "0x" + from_address.hex()

        chain_id = parse_rpc_quantity(rpc_call(rpc_url, "eth_chainId", []))
        nonce = parse_rpc_quantity(rpc_call(rpc_url, "eth_getTransactionCount", [from_hex, "pending"]))
        gas_price = parse_rpc_quantity(rpc_call(rpc_url, "eth_gasPrice", []))

        setup = [
            f"AT+N={nonce}",
            f"AT+G={gas_price}",
            f"AT+L={gas_limit}",
            f"AT+T={to_address}",
            f"AT+V={value_wei}",
            f"AT+C={chain_id}",
            f"AT+D={data_hex}",
        ]

        logs = [address_log]
        for command in setup:
            logs.append(serial_send_and_wait(ser, command, prompt, timeout))
        logs.append(serial_send_and_wait(ser, "AT+S", prompt, timeout))
        return "\n".join(logs)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Demo11 Sepolia transfer helper.")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--log", type=Path, help="Existing Demo11 serial log")
    source.add_argument("--port", help="Serial port, for example COM9")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--rpc",
        default="https://ethereum-sepolia-rpc.publicnode.com",
        help="Sepolia JSON-RPC endpoint",
    )
    parser.add_argument("--to", help="Recipient address for --port mode")
    parser.add_argument("--value-wei", type=int, default=100000000000000)
    parser.add_argument("--gas-limit", type=int, default=21000)
    parser.add_argument("--data", default="", help="Hex calldata without or with 0x")
    parser.add_argument("--save-log", type=Path)
    parser.add_argument("--broadcast", action="store_true", help="Actually call eth_sendRawTransaction")
    args = parser.parse_args(argv)

    try:
        if args.log is not None:
            log_text = read_text(args.log)
        else:
            if not args.to:
                raise ValueError("--to is required in --port mode")
            data_hex = args.data[2:] if args.data.lower().startswith("0x") else args.data
            log_text = serial_sign_with_board(
                args.port,
                args.baudrate,
                args.timeout,
                args.rpc,
                args.to,
                args.value_wei,
                args.gas_limit,
                data_hex,
            )
            if args.save_log:
                args.save_log.write_text(log_text, encoding="utf-8")

        material = parse_demo10_material(log_text)
        checks = verify_material(material)
        selected_v, raw_tx = select_raw_transaction(material)

        print("Checks:")
        for check in checks:
            print(f"  OK - {check}")
        print("VERIFY OK: Demo11 output is internally consistent.")
        print(f"SELECTED_V={selected_v}")
        print(f"SELECTED_RAW_TX=0x{raw_tx.hex()}")

        if args.broadcast:
            tx_hash = rpc_call(args.rpc, "eth_sendRawTransaction", ["0x" + raw_tx.hex()])
            print(f"BROADCAST_TX_HASH={tx_hash}")
        else:
            print("DRY RUN: add --broadcast to send the selected raw transaction to Sepolia.")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
