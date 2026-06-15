# tools

`tools/` 目录放 PC 侧辅助脚本，不会编译进 nRF54LM20 固件。脚本的职责是验证固件串口输出、补齐链上查询和广播动作；私钥签名动作仍然发生在 SE05x 内部。

## 依赖

离线验签需要 `cryptography`。串口模式还需要 `pyserial`。

```bat
python -m pip install cryptography pyserial
```

## `verify_demo09_signature.py`

用途：验证 Demo09 输出的 secp256k1 公钥、digest、DER 签名是否真实匹配。

验证材料：

- `PUBLIC_KEY_UNCOMPRESSED_04XY`
- `DIGEST_SHA256_INPUT`
- `SIGNATURE_DER`

离线日志验签：

```bat
python tools\verify_demo09_signature.py --log demo09.log
```

串口触发 Demo09：

```bat
python tools\verify_demo09_signature.py --port COM9 --command AT+S --save-log demo09_at_s.log
```

成功输出：

```text
VERIFY OK: signature matches public key and digest.
```

## `verify_demo10_eth_tx.py`

用途：验证 Demo10 或 Demo11 输出的 ETH legacy 交易签名材料是否自洽。

它会验证：

1. PC 重新组装 signing RLP。
2. PC 重新计算 Ethereum Keccak-256。
3. PC 从 public key 推导 `ETH_FROM_ADDRESS`。
4. PC 用公钥验 `ETH_SIGNATURE_DER`。
5. PC 检查 `r/s` 和 low-S 归一化。
6. PC 重新生成 raw transaction candidates。

离线日志验证：

```bat
python tools\verify_demo10_eth_tx.py --log demo10.log
```

串口触发签名：

```bat
python tools\verify_demo10_eth_tx.py --port COM9 --command AT+S --save-log demo10_at_s.log
```

给板子设置真实字段后再触发：

```bat
python tools\verify_demo10_eth_tx.py --port COM9 ^
  --set-command AT+N=5 ^
  --set-command AT+G=1000000000 ^
  --set-command AT+L=21000 ^
  --set-command AT+T=0x3535353535353535353535353535353535353535 ^
  --set-command AT+V=1000000000000000 ^
  --set-command AT+C=11155111 ^
  --set-command AT+D= ^
  --command AT+S ^
  --save-log demo10_real_fields.log
```

注意：这个脚本不广播交易，只做本地数学验证。

## `broadcast_demo11_sepolia_tx.py`

用途：配合 Demo11 完成真实 Sepolia 测试网交易 dry-run 或广播。

它不是模拟签名。流程是：

1. 脚本通过串口发送 `AT+A`。
2. Demo11 在 SE05x 中创建或复用持久化钱包私钥对象 `0xEF110001`，并输出稳定 `ETH_FROM_ADDRESS`。
3. 脚本通过 Sepolia RPC 查询 `chainId`、`nonce`、`gasPrice`。
4. 脚本通过串口发送 `AT+N/G/L/T/V/C/D` 设置交易字段。
5. 脚本发送 `AT+S`，SE05x 对交易 signing hash 签名。
6. 脚本在 PC 侧重新验证 RLP、Keccak、地址和签名。
7. 脚本通过公钥恢复选择正确 `v` 和唯一 raw transaction。
8. 默认只 dry-run；只有显式传入 `--broadcast` 才调用 `eth_sendRawTransaction`。

### 第一步：获取地址并充值

烧录当前默认 Demo11 固件后，串口输入：

```text
AT+A
```

记录输出里的：

```text
ETH_FROM_ADDRESS=0x...
```

用 Sepolia faucet 给这个地址充值测试币。

### 第二步：dry-run 签名

```bat
python tools\broadcast_demo11_sepolia_tx.py ^
  --port COM9 ^
  --to 0x接收地址 ^
  --value-wei 100000000000000 ^
  --save-log demo11_sepolia_dry_run.log
```

dry-run 成功时会打印：

```text
VERIFY OK: Demo11 output is internally consistent.
SELECTED_V=...
SELECTED_RAW_TX=0x...
DRY RUN: not broadcasting. Add --broadcast to send this transaction.
```

### 第三步：确认后广播

确认以下内容都正确后再广播：

- `ETH_FROM_ADDRESS` 是你已经充值的 Demo11 地址。
- `ETH_TO` 是你的接收地址。
- `ETH_VALUE_WEI` 是你想转出的测试币金额。
- `ETH_CHAIN_ID` 是 Sepolia 的 `11155111`。
- `nonce` 和 `gasPrice` 来自可信 RPC。

广播命令：

```bat
python tools\broadcast_demo11_sepolia_tx.py ^
  --port COM9 ^
  --to 0x接收地址 ^
  --value-wei 100000000000000 ^
  --broadcast ^
  --save-log demo11_sepolia_broadcast.log
```

成功时会输出交易哈希：

```text
BROADCAST_TX_HASH=0x...
```

## 关于 recovery id 和 `v`

SE05x 的 ECDSA 接口返回 DER 签名，不直接返回 Ethereum 需要的 recovery id。固件会输出两个 raw transaction candidate；PC 脚本用 `r/s/digest` 尝试恢复公钥，选择恢复出的地址等于 `ETH_FROM_ADDRESS` 的那个 candidate。这个过程是 Ethereum 钱包里正常需要做的步骤，不是绕过 SE05x。

## 安全提醒

- Demo11 的 key 是测试网 key，object ID 是 `0xEF110001`。
- 不要随便发送 `AT+X=DELETE_TESTNET_KEY`，删除后地址会变化。
- 生产钱包不能只靠串口命令，需要加入用户确认、屏幕显示、PIN、固件防回滚、交易解析、通信认证、备份恢复策略。
