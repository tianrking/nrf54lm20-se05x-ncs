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
