# tools

这个目录放 PC 侧辅助脚本，不会编译进 nRF54LM20 固件。

## `verify_demo09_signature.py`

用途：验证 Demo09 串口输出的 secp256k1 签名材料是否真实有效。

它验证的是：

- `PUBLIC_KEY_UNCOMPRESSED_04XY`
- `DIGEST_SHA256_INPUT`
- `SIGNATURE_DER`

验证逻辑是：PC 用公钥验证 `SIGNATURE_DER` 是否确实签过 `DIGEST_SHA256_INPUT`。  
私钥不会导出，也不会被 PC 读取。

### 先安装依赖

离线日志验签只需要 `cryptography`。当前开发机一般已经安装。

直接串口模式还需要 `pyserial`：

```bat
python -m pip install pyserial
```

### 方式一：保存串口日志后离线验签

把 Demo09 输出保存为 `demo09.log`，然后运行：

```bat
python tools\verify_demo09_signature.py --log demo09.log
```

成功时会输出：

```text
VERIFY OK: signature matches public key and digest.
```

### 方式二：PC 直接通过串口触发 Demo09

先确保板子已经烧录 Demo09 固件，并且串口已经进入：

```text
se05x-wallet-demo>
```

然后运行：

```bat
python tools\verify_demo09_signature.py --port COM9 --command AT+S
```

脚本会发送 `AT+S`，等待 Demo09 输出新的公钥、digest、签名，然后在 PC 上验签。

也可以保存本次串口输出：

```bat
python tools\verify_demo09_signature.py --port COM9 --command AT+S --save-log demo09_at_s.log
```

### 这不是“公钥解密”

ECDSA 是签名算法，不是加密算法。  
正确理解是：

- SE 内私钥：对 32 字节 digest 签名。
- PC/手机端公钥：验证签名确实来自对应私钥。
- 公钥不能还原私钥，也不能从签名里还原原文。

Ethereum 里常说的“根据签名恢复公钥”是另一层协议规则，需要 `r/s/v`、recovery id 和 Keccak digest。Demo09 当前输出 DER 签名，目的是先证明 SE 的 secp256k1 ECDSA 基础链路成立。

## `verify_demo10_eth_tx.py`

用途：验证 Demo10 串口输出的 ETH legacy transaction 签名材料是否自洽、可验签、可组装成 raw transaction 候选。

它验证的是：

- `ETH_NONCE`
- `ETH_GAS_PRICE_WEI`
- `ETH_GAS_LIMIT`
- `ETH_TO`
- `ETH_VALUE_WEI`
- `ETH_DATA`
- `ETH_CHAIN_ID`
- `ETH_SIGNING_RLP`
- `ETH_SIGNING_HASH_KECCAK256`
- `ETH_PUBLIC_KEY_UNCOMPRESSED_04XY`
- `ETH_FROM_ADDRESS`
- `ETH_SIGNATURE_DER`
- `ETH_SIGNATURE_R`
- `ETH_SIGNATURE_S_LOW`
- `ETH_RAW_TX_CANDIDATE_V0`
- `ETH_RAW_TX_CANDIDATE_V1`

验证逻辑是：

1. PC 从串口日志解析板子实际打印的交易字段，不写死样例值。
2. PC 按这些字段重新构造 legacy/EIP-155 signing RLP。
3. PC 重新计算 Ethereum Keccak-256，确认等于 `ETH_SIGNING_HASH_KECCAK256`。
4. PC 用 `Keccak256(public key X||Y)` 推导地址，确认等于 `ETH_FROM_ADDRESS`。
5. PC 用公钥验证 `ETH_SIGNATURE_DER` 确实签过该 Keccak digest。
6. PC 用 `ETH_SIGNATURE_R` 和 `ETH_SIGNATURE_S_LOW` 重组签名并再次验签。
7. PC 根据 `ETH_CHAIN_ID` 检查 `v` 候选是否符合 EIP-155：`chainId * 2 + 35/36`。
8. PC 重新拼两个 raw transaction 候选，确认和固件输出一致。

离线日志验证：

```bat
python tools\verify_demo10_eth_tx.py --log demo10.log
```

串口自动触发默认字段签名并验证：

```bat
python tools\verify_demo10_eth_tx.py --port COM9 --command AT+S
```

串口设置真实交易字段后再触发验证：

```bat
python tools\verify_demo10_eth_tx.py --port COM9 ^
  --set-command AT+N=5 ^
  --set-command AT+G=1000000000 ^
  --set-command AT+L=21000 ^
  --set-command AT+T=0x3535353535353535353535353535353535353535 ^
  --set-command AT+V=1000000000000000 ^
  --set-command AT+C=1 ^
  --set-command AT+D= ^
  --command AT+S
```

成功时输出：

```text
VERIFY OK: Demo10 ETH RLP, Keccak, address, signature, and raw tx candidates match.
```

注意：这个脚本不是钱包、不是节点、不发交易。它只证明 Demo10 输出的材料在本地数学上成立。`--set-command` 只是帮你通过串口把真实交易字段送进板子，签名动作仍然发生在 SE05x 侧，不是 PC 模拟签名。最终广播前仍然要由手机/PC 根据 recovery id 选择能恢复出 `ETH_FROM_ADDRESS` 的那个 `v`。
