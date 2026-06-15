# API 参考说明

本文档说明本工程**实际使用和封装的 API**，包括：

- 应用启动和 SE05x session API。
- `se05x_bus` 平台无关 I2C transport API。
- Zephyr I2C backend API。
- demo framework API。
- 当前 demo 调用到的 SE05x APDU API。
- Demo 06/07/08 调用到的 SSS key object、key store、asymmetric sign/verify API。

NXP Plug & Trust hostlib 内部还有更多未使用的加密、TLS、证书链和策略 API。本文只说明本工程已经在代码里调用并构建验证过的 API，避免把未验证接口写成项目能力。

## 总体调用链

```mermaid
flowchart TD
    A["main()"] --> B["se05x_zephyr_i2c_bus_create()"]
    B --> C["se05x_bus_register_default()"]
    C --> D["ex_sss_boot_open()"]
    D --> E["Platform SCP03 secure session"]
    E --> F["ex_sss_key_store_and_object_init()"]
    F --> G["se05x_demo_find(APP_SELECTED_DEMO)"]
    G --> H["demo->run(&boot_ctx)"]
    H --> I["Se05x_API_GetVersion / GetRandom / ReadObject / ..."]
    I --> J["T=1 over I2C"]
    J --> K["SE05x / SE052"]
    H --> L["ex_sss_session_close()"]
```

## 公共类型和返回值约定

### 串口日志编码约定

本工程 API 文档和源码注释使用中文解释接口细节；固件运行时输出到串口的菜单、状态和错误日志使用英文 ASCII。原因是现场常用串口工具不一定按 UTF-8 解码中文，中文日志容易显示成 `����`。因此：

- API 的作用、入参、输出参数、返回值、风险说明写在本文档中。
- 串口只打印命令名、状态字、长度、object ID、`OK/FAIL/SKIP` 和少量英文说明。
- 如果新增 API demo，应优先在本文补齐中文说明，而不是把长中文解释放进固件日志。

| 类型 | 来源 | 含义 |
| --- | --- | --- |
| `ex_sss_boot_ctx_t` | NXP SSS example boot layer | 保存 host session、SE session、key store、key object 等上下文。`main.c` 中的 `s_boot_ctx` 就是这个类型。 |
| `sss_status_t` | NXP SSS layer | SSS 层函数返回值。常见成功值是 `kStatus_SSS_Success`。 |
| `pSe05xSession_t` | NXP SE05x APDU layer | SE05x APDU 层 session 指针，demo 中从 `boot_ctx->session` 转换得到。 |
| `smStatus_t` | NXP secure messaging/APDU layer | APDU 函数返回的状态字封装。常见成功值是 `SM_OK`。 |
| `size_t *xxxLen` | NXP APDU API 约定 | 通常是输入输出参数：调用前填 buffer 容量，返回后改成实际写入长度。 |
| `SE05x_Result_t` | NXP SE05x enum | 对象存在等查询的结果。常用值：`kSE05x_Result_SUCCESS`、`kSE05x_Result_FAILURE`。 |
| `SE05x_MemoryType_t` | NXP SE05x enum | 空间类型。常用值：`PERSISTENT`、`TRANSIENT_RESET`、`TRANSIENT_DESELECT`。 |

### 长度参数约定

很多 SE05x APDU API 使用这种形式：

```c
uint8_t buffer[128];
size_t buffer_len = sizeof(buffer);
smStatus_t sw = Some_API(..., buffer, &buffer_len);
```

含义是：

1. 调用前，`buffer_len` 表示 `buffer` 最大容量。
2. 调用后，如果成功，`buffer_len` 表示 SE05x 实际返回了多少字节。
3. 如果 `buffer` 太小，API 可能失败或返回不完整数据，具体取决于 NXP hostlib 和 applet 响应。

## 启动和 session API

这些 API 在 `src/main.c` 中使用，负责打开和关闭 SE05x 会话。

### `ex_sss_boot_open`

```c
sss_status_t ex_sss_boot_open(ex_sss_boot_ctx_t *pCtx, const char *portName);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 打开 NXP SSS example session。本工程配置为 Platform SCP03，因此这一步会连接 SE05x、读取 ATR、完成 SCP03 认证并建立安全会话。 |
| 调用位置 | `src/main.c` 的 `app_open_se_session()`。 |
| `pCtx` | 输入输出参数。调用前由应用清零；调用成功后填入 SE session、host session、连接状态等上下文。 |
| `portName` | 连接字符串。本工程传 `NULL`，底层通过 `se05x_bus` 默认 bus 访问 Zephyr I2C。 |
| 返回值 | `kStatus_SSS_Success` 表示成功；其他值表示 session 或 SCP03 打开失败。 |
| 失败重点 | I2C/T=1 通路、SCP03 profile/key、PSA host crypto、host RNG 配置。 |

### `ex_sss_key_store_and_object_init`

```c
sss_status_t ex_sss_key_store_and_object_init(ex_sss_boot_ctx_t *pCtx);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 初始化 SSS key store 和 key object 上下文。只读 demo 对它依赖不强，但后续创建 key、签名、加密、证书类 demo 会需要。 |
| 调用位置 | `src/main.c` 的 `app_open_se_session()`，在 `ex_sss_boot_open()` 成功之后。 |
| `pCtx` | 输入输出参数。必须是已经成功打开 session 的 boot context。 |
| 返回值 | `kStatus_SSS_Success` 表示初始化成功；失败时当前工程只打印 warning，不阻止只读 demo 运行。 |
| 时序要求 | 必须在 `ex_sss_boot_open()` 之后调用。 |

### `ex_sss_session_close`

```c
void ex_sss_session_close(ex_sss_boot_ctx_t *pCtx);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 关闭 SSS/SE05x session，释放 NXP hostlib 维护的会话资源。 |
| 调用位置 | `src/main.c` 中 demo 运行完成之后。 |
| `pCtx` | 输入参数。传入已经打开过 session 的 boot context。 |
| 返回值 | 无。 |
| 时序要求 | demo 完成后调用。当前主线程之后进入 sleep，用于保留串口日志和 debug 状态。 |

## `se05x_bus` 平台无关 API

这些 API 定义在 `se05x_bus/include/se05x_bus.h`。它们把 NXP hostlib 和具体平台 I2C 实现解耦。

### `se05x_bus_ops_t`

```c
typedef struct {
    int (*open)(se05x_bus_t *bus);
    void (*close)(se05x_bus_t *bus);
    int (*write)(se05x_bus_t *bus, uint8_t address, const uint8_t *data, size_t data_len);
    int (*read)(se05x_bus_t *bus, uint8_t address, uint8_t *data, size_t data_len);
    void (*delay_ms)(uint32_t delay_ms);
    void *ctx;
} se05x_bus_ops_t;
```

| 成员 | 方向 | 作用 |
| --- | --- | --- |
| `open` | backend 实现，上层调用 | 打开底层 bus。Zephyr backend 当前主要做上下文准备，真正 ready 状态在 create 阶段确认。 |
| `close` | backend 实现，上层调用 | 关闭或释放 bus 相关资源。 |
| `write` | backend 实现，上层调用 | 向指定 I2C 地址写入 `data_len` 字节。 |
| `read` | backend 实现，上层调用 | 从指定 I2C 地址读取 `data_len` 字节。 |
| `delay_ms` | backend 实现，上层调用 | 给 NXP T=1 over I2C 层提供毫秒延时。 |
| `ctx` | backend 私有数据 | 保存 Zephyr `i2c_dt_spec`、timeout 等平台上下文。 |

### `se05x_bus_register_default`

```c
int se05x_bus_register_default(const se05x_bus_ops_t *ops);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 注册默认 bus。NXP porting 层通过这个默认 bus 访问 SE05x。 |
| 调用位置 | `src/main.c` 的 `app_register_transport()`。 |
| `ops` | 输入参数。指向已经初始化好的平台 bus 操作表。 |
| 返回值 | `0` 表示成功；非 0 表示参数无效或注册失败。 |
| 时序要求 | 必须在 `ex_sss_boot_open()` 前调用。 |

### `se05x_bus_get_default`

```c
const se05x_bus_ops_t *se05x_bus_get_default(void);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 获取当前默认 bus 操作表。 |
| 调用位置 | NXP porting 层，例如 `i2c_a7_zephyr_bus.c`。 |
| 入参 | 无。 |
| 返回值 | 成功时返回默认 bus ops 指针；未注册时返回 `NULL`。 |

### `se05x_bus_clear_default`

```c
void se05x_bus_clear_default(void);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 清除默认 bus 注册。 |
| 当前使用 | 当前主流程没有主动调用，预留给后续反初始化或多平台测试使用。 |
| 入参 | 无。 |
| 返回值 | 无。 |

## Zephyr I2C backend API

这些 API 定义在 `se05x_bus/include/se05x_bus_zephyr.h`。

### `se05x_zephyr_i2c_default_config`

```c
se05x_zephyr_i2c_config_t se05x_zephyr_i2c_default_config(void);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 从 devicetree alias `se05x` 生成默认 Zephyr I2C 配置。 |
| 入参 | 无。 |
| 返回值 | `se05x_zephyr_i2c_config_t`，包含 `struct i2c_dt_spec i2c` 和 `timeout_ms`。 |
| 使用场景 | `se05x_zephyr_i2c_bus_create()` 在 `config == NULL` 时使用它。 |

### `se05x_zephyr_i2c_bus_create`

```c
int se05x_zephyr_i2c_bus_create(se05x_bus_t *bus,
                                const se05x_zephyr_i2c_config_t *config);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 创建 Zephyr I2C backend，把 Zephyr `i2c_dt_spec` 封装成 `se05x_bus_t`。 |
| 调用位置 | `src/main.c` 的 `app_register_transport()`。 |
| `bus` | 输出参数。调用成功后填入可注册到 `se05x_bus_register_default()` 的 ops。 |
| `config` | 输入参数。为 `NULL` 时使用 devicetree alias `se05x` 的默认配置；非 `NULL` 时使用调用者提供的 I2C 配置。 |
| 返回值 | `0` 表示成功；非 0 表示分配失败、I2C device not ready 或配置错误。 |
| 失败重点 | overlay、I2C 实例、SCL/SDA 管脚、SE05x 地址、Zephyr device ready 状态。 |

### `se05x_zephyr_i2c_default_address`

```c
uint8_t se05x_zephyr_i2c_default_address(void);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 返回 devicetree alias `se05x` 对应节点的 I2C 地址。当前默认是 `0x48`。 |
| 入参 | 无。 |
| 返回值 | 8 位 I2C 地址。 |
| 使用场景 | NXP porting 层需要知道 SE05x slave address 时使用。 |

## Demo framework API

这些 API 定义在 `demo/se05x_demo.h`，实现位于 `demo/se05x_demo.c`。

### demo 描述结构

```c
typedef struct {
    se05x_demo_id_t id;
    const char *name;
    const char *when_to_use;
    const char *flow;
    const char *expected_output;
    const char *se_features;
    sss_status_t (*run)(ex_sss_boot_ctx_t *boot_ctx);
} se05x_demo_t;
```

| 成员 | 作用 |
| --- | --- |
| `id` | demo 编号，和 `APP_SELECTED_DEMO` 对应。 |
| `name` | 短名称，串口日志中显示。 |
| `when_to_use` | 说明什么时候运行这个 demo。 |
| `flow` | 说明 demo 的主要执行流程。 |
| `expected_output` | 说明期望串口输出和通过条件。 |
| `se_features` | 说明用到的 SE05x 功能或 APDU 类型。 |
| `run` | demo 入口函数，参数是已经打开 SCP03 session 的 `boot_ctx`。 |

### `se05x_demo_find`

```c
const se05x_demo_t *se05x_demo_find(se05x_demo_id_t id);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 根据 demo ID 查找 demo 描述结构。 |
| 调用位置 | `src/main.c`。 |
| `id` | 输入参数，例如 `SE05X_DEMO_UART_SAFE_API`、`SE05X_DEMO_SAFE_READ_ONLY`。 |
| 返回值 | 找到时返回 `se05x_demo_t *`；找不到返回 `NULL`。 |

### `se05x_demo_log_catalog`

```c
void se05x_demo_log_catalog(void);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 在串口打印当前编译进固件的 demo 列表。 |
| 入参 | 无。 |
| 返回值 | 无。 |

### `se05x_demo_log_selection`

```c
void se05x_demo_log_selection(const se05x_demo_t *demo);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 打印当前选中的 demo 名称、适用场景、流程、预期输出和 SE05x 功能。业务 demo 会在这里说明当前是否写 NVM、是否只是业务前置流程。 |
| `demo` | 输入参数。通常来自 `se05x_demo_find()`。 |
| 返回值 | 无。`demo == NULL` 时打印错误日志。 |

### `se05x_demo_active_scp03_profile`

```c
const char *se05x_demo_active_scp03_profile(void);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 根据编译期 `SSS_PFSCP_ENABLE_xxx` 宏返回当前 SCP03 profile 名称。 |
| 入参 | 无。 |
| 返回值 | 字符串，例如当前工程常见为 `SE052_B501`；未知配置返回 `unknown`。 |

### 统计和日志 API

| API | 作用 | 入参 | 返回值 |
| --- | --- | --- | --- |
| `se05x_demo_stats_init(se05x_demo_stats_t *stats, const char *tag)` | 初始化 pass/skip/fail 统计。 | `stats` 输出统计结构；`tag` 日志前缀。 | 无 |
| `se05x_demo_stats_result(const se05x_demo_stats_t *stats)` | 把统计结果转换为 SSS 返回值。 | `stats` 输入统计结构。 | `fail == 0` 返回 `kStatus_SSS_Success`，否则返回 `kStatus_SSS_Fail`。 |
| `se05x_demo_log_summary(const se05x_demo_stats_t *stats)` | 打印 pass/skip/fail 汇总。 | `stats` 输入统计结构。 | 无 |
| `se05x_demo_mark_pass(se05x_demo_stats_t *stats, const char *name)` | 某检查项通过。 | `stats` 统计结构；`name` 检查项名称。 | 无 |
| `se05x_demo_mark_fail_sw(se05x_demo_stats_t *stats, const char *name, smStatus_t sw)` | 某必需 APDU 检查失败。 | `stats`、检查项名称、APDU 状态。 | 无 |
| `se05x_demo_mark_skip_sw(se05x_demo_stats_t *stats, const char *name, smStatus_t sw)` | 某可选能力跳过。 | `stats`、检查项名称、APDU 状态。 | 无 |
| `se05x_demo_mark_fail_status(se05x_demo_stats_t *stats, const char *name, sss_status_t status)` | 某 SSS 层检查失败。 | `stats`、检查项名称、SSS 状态。 | 无 |
| `se05x_demo_log_hex_preview(const char *label, const uint8_t *data, size_t data_len)` | 打印最多 16 字节十六进制预览。 | `label` 标签；`data` 数据指针；`data_len` 数据长度。 | 无 |
| `se05x_demo_log_applet_features(uint16_t applet_config)` | 解析并打印 applet config 能力位。 | `applet_config` 来自 `GetVersion` 返回的 version[3:4]。 | 无 |

## Demo 00 UART 交互命令和 API 对应

Demo 00 的源码文件是 `demo/se05x_demo_00_uart_safe_api.c`。它不是新的 SE05x 协议层，而是一个串口交互式外壳：用户输入命令，固件调用一个已经验证过的安全 APDU API，并立即打印结果。

串口输入推荐使用文本模式，并采用固定 `AT+X` 字符串命令。发送 `AT+3` 后，固件会直接打印本次命令对应的 API 调用和返回值，例如 `CMD AT+3 -> CALL Se05x_API_GetRandom(16)`、`OK GetRandom sw=0x9000 len=16` 和 16 字节随机数。命令后面的回车或换行是可选的；固件收到完整 `AT+X` 后即可解析执行。固定前缀让固件按字符串处理命令，避免单字节输入被串口工具的 text/hex 模式影响。

| 串口命令 | 调用函数 | API 类别 | 传入参数重点 | 返回内容 | NVM 风险 |
| --- | --- | --- | --- | --- | --- |
| `AT+0` / `AT+H` | `print_menu()` | 无 | 无。 | 重新打印菜单。 | 无 |
| `AT+1` | `cmd_get_version()` | `Se05x_API_GetVersion()` | 当前 `pSe05xSession_t`、7 字节 version buffer。 | applet version、applet config、SecureBox。 | 无 |
| `AT+2` | `cmd_get_ext_version()` | `Se05x_API_GetExtVersion()` | 当前 session、48 字节 version buffer。 | 扩展版本字节，日志打印 preview。 | 无 |
| `AT+3` | `cmd_get_random()` | `Se05x_API_GetRandom()` | 请求 16 字节随机数。 | 16 字节随机数 preview。 | 无 |
| `AT+4` | `cmd_read_unique_id()` | `Se05x_API_ReadObject()` | object ID 为 `kSE05x_AppletResID_UNIQUE_ID`，offset/length 为 `0`。 | UniqueID preview。 | 无 |
| `AT+5` | `cmd_check_object()` | `Se05x_API_CheckObjectExists()` | object ID 为 `kSE05x_AppletResID_UNIQUE_ID`。 | `exists=yes/no`。 | 无 |
| `AT+6` | `cmd_check_object()` | `Se05x_API_CheckObjectExists()` | object ID 为 `kSE05x_AppletResID_FEATURE`。 | `exists=yes/no`。 | 无 |
| `AT+7` | `cmd_check_object()` | `Se05x_API_CheckObjectExists()` | object ID 为 `kSE05x_AppletResID_PLATFORM_SCP`。 | `exists=yes/no`。 | 无 |
| `AT+8` | `cmd_free_memory()` | `Se05x_API_GetFreeMemory()` | memory type 为 `kSE05x_MemoryType_PERSISTENT`。 | persistent free bytes。 | 无 |
| `AT+9` | `cmd_free_memory()` | `Se05x_API_GetFreeMemory()` | memory type 为 `kSE05x_MemoryType_TRANSIENT_RESET`。 | transient reset free bytes。 | 无 |
| `AT+A` | `cmd_free_memory()` | `Se05x_API_GetFreeMemory()` | memory type 为 `kSE05x_MemoryType_TRANSIENT_DESELECT`。 | transient deselect free bytes。 | 无 |
| `AT+B` | `cmd_read_curve_list()` | `Se05x_API_ReadECCurveList()` | 当前 session、128 字节 curve buffer。 | ECC curve list preview，并解码 Weierstrass 曲线 `SET/NOT_SET` 状态；重点打印 P-256、BTC/ETH `secp256k1` 钱包视角。 | 无 |
| `AT+C` | `cmd_read_crypto_object_list()` | `Se05x_API_ReadCryptoObjectList()` | 当前 session、128 字节 list buffer。 | crypto object list preview。 | 无 |
| `AT+D` | `cmd_read_state()` | `Se05x_API_ReadState()` | 当前 session、128 字节 state buffer。 | applet state preview。 | 无 |
| `AT+E` | `cmd_read_id_list()` | `Se05x_API_ReadIDList()` | `outputOffset=0`、`filter=0xFF`。 | ID list preview；失败时按 SKIP。 | 无 |
| `AT+F` | `cmd_run_all_safe_queries()` | Demo 00 内部顺序调用安全查询 API | 当前 session。 | 一次性执行 `GetVersion`、`GetExtVersion`、`GetRandom`、`ReadObject(UNIQUE_ID)`、对象存在检查、三类 free memory、curve list、crypto object list、state 和可选 ID list。 | 无 |
| `AT+Q` | 返回 `main.c` | 无 | 无。 | 关闭 Demo 00，随后由 main 关闭 session。 | 无 |

Demo 00 不调用 `sss_key_store_set_key()`、对象创建、对象删除、policy 修改或生命周期修改 API，因此默认菜单可以安全反复运行。写入型能力仍然放在 Demo 06/07 中，并通过固定 demo object ID 和“已有对象不覆盖”的策略控制风险。

## SE05x APDU API

下面这些是当前 demo 实际调用的 SE05x APDU API。它们都要求：

1. `ex_sss_boot_open()` 已经成功。
2. `pSe05xSession_t session_ctx` 指向有效 SE05x session。
3. 如果当前 profile 是 Platform SCP03，则调用会在已建立的安全通道内进行。

### `Se05x_API_GetVersion`

```c
smStatus_t Se05x_API_GetVersion(pSe05xSession_t session_ctx,
                                uint8_t *pappletVersion,
                                size_t *appletVersionLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x applet 基础版本信息和 applet config 能力位。 |
| 使用 demo | Demo 00、Demo 01、Demo 02、Demo 03、Demo 04、Demo 05。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `pappletVersion` | 输出 buffer。本工程使用 7 字节 buffer。 |
| `appletVersionLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际版本数据长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示 APDU 失败。 |
| 本工程解析 | `version[0..2]` 作为 major/minor/patch；`version[3..4]` 合成 `applet_config` 并打印能力位。 |

### `Se05x_API_GetExtVersion`

```c
smStatus_t Se05x_API_GetExtVersion(pSe05xSession_t session_ctx,
                                   uint8_t *pappletVersion,
                                   size_t *appletVersionLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x applet 扩展版本信息。NXP 注释说明该接口可返回 37 字节 VersionInfo，包括 applet 版本、feature 和 secure box version 等。 |
| 使用 demo | Demo 00、Demo 01。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `pappletVersion` | 输出 buffer。本工程使用 64 字节 buffer，串口只打印前 16 字节 preview。 |
| `appletVersionLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际返回长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示 APDU 失败。 |

### `Se05x_API_GetRandom`

```c
smStatus_t Se05x_API_GetRandom(pSe05xSession_t session_ctx,
                               uint16_t size,
                               uint8_t *randomData,
                               size_t *prandomDataLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 从 SE05x 获取硬件/安全随机数。 |
| 使用 demo | Demo 00、Demo 01、Demo 02、Demo 04、Demo 05。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `size` | 输入参数。请求随机数字节数。本工程请求 16 字节。 |
| `randomData` | 输出 buffer，保存随机数。 |
| `prandomDataLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际随机数长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示 APDU 失败。 |
| 本工程判断 | 要求返回 `SM_OK` 且实际长度等于请求长度。 |

### `Se05x_API_ReadObject`

```c
smStatus_t Se05x_API_ReadObject(pSe05xSession_t session_ctx,
                                uint32_t objectID,
                                uint16_t offset,
                                uint16_t length,
                                uint8_t *data,
                                size_t *pdataLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x 中指定 object ID 的对象数据。 |
| 使用 demo | Demo 00、Demo 01、Demo 02、Demo 04。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `objectID` | 输入参数。对象 ID。本工程读取 `kSE05x_AppletResID_UNIQUE_ID`。 |
| `offset` | 输入参数。读取偏移。本工程传 `0`。 |
| `length` | 输入参数。请求读取长度。本工程传 `0`，实际返回长度以 `pdataLen` 为准。 |
| `data` | 输出 buffer，保存对象数据。 |
| `pdataLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际读取长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示读取失败、对象不可读或对象不存在。 |

### `Se05x_API_CheckObjectExists`

```c
smStatus_t Se05x_API_CheckObjectExists(pSe05xSession_t session_ctx,
                                       uint32_t objectID,
                                       SE05x_Result_t *presult);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 检查指定 secure object 是否存在。 |
| 使用 demo | Demo 00、Demo 01、Demo 03、Demo 04、Demo 05、Demo 06、Demo 07、Demo 08、Demo 09。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `objectID` | 输入参数。对象 ID。当前检查 `UNIQUE_ID`、`FEATURE`、`PLATFORM_SCP`。 |
| `presult` | 输出参数。`kSE05x_Result_SUCCESS` 表示存在；`kSE05x_Result_FAILURE` 表示不存在。 |
| 返回值 | `SM_OK` 表示查询命令成功；查询成功不等于对象一定存在，需要继续看 `presult`。 |
| 本工程处理 | Demo 00 中作为 UART 单项查询；Demo 01 中作为 pass/skip 检查；Demo 03 用于 inventory；Demo 04/05 用于业务流程前置校验；Demo 06/07/08 用于写入前防覆盖和依赖对象检查。 |

### `Se05x_API_GetFreeMemory`

```c
smStatus_t Se05x_API_GetFreeMemory(pSe05xSession_t session_ctx,
                                   SE05x_MemoryType_t memoryType,
                                   uint32_t *pfreeMem);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 查询 SE05x 指定内存类型剩余空间。 |
| 使用 demo | Demo 00、Demo 01、Demo 03、Demo 05。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `memoryType` | 输入参数。内存类型：`kSE05x_MemoryType_PERSISTENT`、`kSE05x_MemoryType_TRANSIENT_RESET`、`kSE05x_MemoryType_TRANSIENT_DESELECT`。 |
| `pfreeMem` | 输出参数。剩余空间字节数。 |
| 返回值 | `SM_OK` 表示查询成功；其他值表示 APDU 失败或该类型不可查询。 |
| 场景意义 | 写入 key、证书、对象前，先看 persistent 空间是否足够。 |

### `Se05x_API_ReadIDList`

```c
smStatus_t Se05x_API_ReadIDList(pSe05xSession_t session_ctx,
                                uint16_t outputOffset,
                                uint8_t filter,
                                uint8_t *pmore,
                                uint8_t *idlist,
                                size_t *pidlistLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x 对象 ID 列表。NXP 注释说明 `idlist` 是包含 4 字节 object identifier 的字节数组。 |
| 使用 demo | Demo 00、Demo 01、Demo 03。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `outputOffset` | 输入参数。列表读取偏移。本工程传 `0`。 |
| `filter` | 输入参数。过滤条件。本工程传 `0xFF`。 |
| `pmore` | 输出参数。指示是否还有更多 ID。 |
| `idlist` | 输出 buffer，保存对象 ID 字节列表。 |
| `pidlistLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际返回长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示当前 applet/OEF/权限下不可用或 APDU 失败。 |
| 本工程处理 | 当前已知可能返回 `sw=0xFFFF`，因此作为 `SKIP`，不影响整体 bring-up 通过。 |

### `Se05x_API_ReadECCurveList`

```c
smStatus_t Se05x_API_ReadECCurveList(pSe05xSession_t session_ctx,
                                     uint8_t *curveList,
                                     size_t *pcurveListLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x ECC curve 列表或曲线启用状态。返回的是 Weierstrass 曲线的 set indicator 数组，数组下标和 `SE05x_ECCurve_t` 曲线 ID 对应：`curveList[curve_id - 1]`。当前 Demo 00 会把原始字节解码为曲线名和 `SET/NOT_SET`，并额外给出钱包视角：P-256 是否 ready、BTC/ETH 需要的 `secp256k1` 是否 ready。 |
| 使用 demo | Demo 00、Demo 01、Demo 03、Demo 05、Demo 09。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `curveList` | 输出 buffer，保存曲线列表数据。 |
| `pcurveListLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际返回长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示 APDU 失败或当前配置不支持。 |
| 场景意义 | 后续做 ECC key、ECDSA、ECDH 前，用它查看曲线能力状态。Demo 09 用它判断 secp256k1 是否已经 `SET`，避免重复写曲线参数。 |

### `Se05x_API_CreateCurve_secp256k1`

```c
smStatus_t Se05x_API_CreateCurve_secp256k1(pSe05xSession_t session_ctx,
                                           SE05x_ECCurve_t curve_id);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 把 secp256k1 曲线参数写入 SE05x，包括 A、B、G、N、Prime 等曲线参数。写入后 `ReadECCurveList()` 中 `kSE05x_ECCurve_Secp256k1` 应该从 `NOT_SET` 变为 `SET`。 |
| 使用 demo | Demo 09。 |
| `session_ctx` | 输入参数。SE05x session，必须已经通过 Platform SCP03 打开。 |
| `curve_id` | 输入参数。Demo 09 固定传入 `kSE05x_ECCurve_Secp256k1`。 |
| 返回值 | `SM_OK` 表示曲线参数写入成功；其他值表示当前 applet/OEF/权限不允许创建曲线，或者 APDU 写入失败。 |
| NVM 风险 | 这是写 SE05x persistent NVM 的 API。Demo 09 只有在 `ReadECCurveList()` 确认 secp256k1 为 `NOT_SET` 时才调用，并且调用后再次读取曲线列表确认状态。 |
| 业务含义 | 如果这个 API 成功，只说明 secp256k1 曲线可以被当前 SE 配置启用；还需要继续验证 key generation、sign、verify，才能判断是否具备钱包签名基础能力。 |

### `Se05x_API_ReadCryptoObjectList`

```c
smStatus_t Se05x_API_ReadCryptoObjectList(pSe05xSession_t session_ctx,
                                          uint8_t *idlist,
                                          size_t *pidlistLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x crypto object 列表。crypto object 通常用于临时密码运算上下文。 |
| 使用 demo | Demo 00、Demo 01、Demo 03、Demo 05。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `idlist` | 输出 buffer，保存 crypto object 列表数据。 |
| `pidlistLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际返回长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示 APDU 失败。 |
| 本工程现象 | 返回长度为 0 也可能是正常状态，表示当前没有临时 crypto object。 |

### `Se05x_API_ReadState`

```c
smStatus_t Se05x_API_ReadState(pSe05xSession_t session_ctx,
                               uint8_t *pstateValues,
                               size_t *pstateValuesLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 读取 SE05x applet 状态摘要。 |
| 使用 demo | Demo 00、Demo 01、Demo 02、Demo 04。 |
| `session_ctx` | 输入参数。SE05x session。 |
| `pstateValues` | 输出 buffer，保存状态字节。 |
| `pstateValuesLen` | 输出长度参数。调用前是 buffer 容量；调用后是实际状态数据长度。 |
| 返回值 | `SM_OK` 表示成功；其他值表示 APDU 失败或当前配置不支持。 |
| 本工程处理 | Demo 00 中作为 UART 单项查询；Demo 01 中作为只读检查；Demo 02/04 中作为快速检查最后的状态闭环，失败时按 skip 处理。 |

## SSS key/certificate/sign API

下面这些 API 从 Demo 06 开始使用。它们运行在 `ex_sss_boot_open()` 和 `ex_sss_key_store_and_object_init()` 之后，依赖 `boot_ctx->session` 和 `boot_ctx->ks` 已经有效。

### `sss_key_object_init`

```c
sss_status_t sss_key_object_init(sss_object_t *keyObject,
                                 sss_key_store_t *keyStore);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 初始化一个 SSS key object 句柄，并把它绑定到当前 key store。 |
| 使用 demo | Demo 06、Demo 07、Demo 08、Demo 09。 |
| `keyObject` | 输出参数。调用前通常清零；调用成功后可用于 allocate/get/set/sign/read。 |
| `keyStore` | 输入参数。本工程使用 `&boot_ctx->ks`。 |
| 返回值 | `kStatus_SSS_Success` 表示成功；其他值表示 key store 或对象类型不匹配。 |

### `sss_key_object_allocate_handle`

```c
sss_status_t sss_key_object_allocate_handle(sss_object_t *keyObject,
                                            uint32_t keyId,
                                            sss_key_part_t keyPart,
                                            sss_cipher_type_t cipherType,
                                            size_t keyByteLenMax,
                                            uint32_t options);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 为一个新对象分配 SSS handle。对 SE05x persistent object 来说，这是写 key/cert 前的准备动作。 |
| 使用 demo | Demo 06、Demo 07、Demo 09。 |
| `keyObject` | 输入输出参数，必须已经 `sss_key_object_init()`。 |
| `keyId` | 输入参数。Demo 06 使用 `0xEF060001`；Demo 07 使用 `0xEF070001`；Demo 09 使用 transient 测试 ID `0xEF090001`。 |
| `keyPart` | 输入参数。ECC 私钥对使用 `kSSS_KeyPart_Pair`；证书 binary object 使用 `kSSS_KeyPart_Default`。 |
| `cipherType` | 输入参数。P-256 ECC 使用 `kSSS_CipherType_EC_NIST_P`；Demo 09 的 secp256k1 使用 `kSSS_CipherType_EC_NIST_K`；证书存储使用 `kSSS_CipherType_Binary`。 |
| `keyByteLenMax` | 输入参数。对象最大长度，ECC demo key 使用 DER 私钥长度，证书使用 DER 证书长度。 |
| `options` | 输入参数。`kKeyObject_Mode_Persistent` 会写 persistent NVM；`kKeyObject_Mode_Transient` 只在当前 session 保留。 |
| 返回值 | `kStatus_SSS_Success` 表示 handle 分配成功；失败时不应继续写入。 |
| NVM 风险 | 当 `options` 是 persistent 时，后续 `sss_key_store_set_key()` 会创建或更新 SE05x NVM 对象。本工程先 `CheckObjectExists()`，已有对象不覆盖。 |

### `sss_key_object_get_handle`

```c
sss_status_t sss_key_object_get_handle(sss_object_t *keyObject,
                                       uint32_t keyId);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 获取已经存在的 SE05x object handle。 |
| 使用 demo | Demo 06、Demo 07、Demo 08。 |
| `keyObject` | 输入输出参数，必须已经 `sss_key_object_init()`。 |
| `keyId` | 输入参数。要获取的 object ID。 |
| 返回值 | `kStatus_SSS_Success` 表示对象存在且 handle 可用；失败表示对象不存在、类型不可识别或权限/通信失败。 |
| 本工程处理 | Demo 06/07 已有对象时使用它复用对象；Demo 08 用它加载 TLS key/cert 依赖。 |

### `sss_key_store_set_key`

```c
sss_status_t sss_key_store_set_key(sss_key_store_t *keyStore,
                                   sss_object_t *keyObject,
                                   const uint8_t *data,
                                   size_t dataLen,
                                   size_t keyBitLen,
                                   void *options,
                                   size_t optionsLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 把 key/cert/binary 数据写入 key store 对应对象。 |
| 使用 demo | Demo 06、Demo 07。 |
| `keyStore` | 输入参数。本工程使用 `&boot_ctx->ks`。 |
| `keyObject` | 输入参数。必须已经 allocate handle。 |
| `data` | 输入参数。Demo 06 是 P-256 demo 私钥 DER；Demo 07 是 DER demo certificate。 |
| `dataLen` | 输入参数。数据字节长度。 |
| `keyBitLen` | 输入参数。ECC 使用 `256`；证书 binary object 使用 `dataLen * 8`。 |
| `options/optionsLen` | 当前工程传 `NULL/0`。 |
| 返回值 | `kStatus_SSS_Success` 表示写入成功。 |
| NVM 风险 | 对 persistent object 调用会写 SE05x NVM。本工程只在对象不存在时写入，避免覆盖已有对象。 |

### `sss_key_store_generate_key`

```c
sss_status_t sss_key_store_generate_key(sss_key_store_t *keyStore,
                                        sss_object_t *keyObject,
                                        size_t keyBitLen,
                                        void *options);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 让 SE05x 在指定 key object 中生成一把新 key。对于 Demo 09，它用于在 SE 内生成 secp256k1 transient key pair，私钥不导出。 |
| 使用 demo | Demo 09。 |
| `keyStore` | 输入参数。本工程使用 `&boot_ctx->ks`。 |
| `keyObject` | 输入参数。必须已经通过 `sss_key_object_init()` 和 `sss_key_object_allocate_handle()` 分配好；Demo 09 使用 `kSSS_CipherType_EC_NIST_K`、`kSSS_KeyPart_Pair`、`kKeyObject_Mode_Transient`。 |
| `keyBitLen` | 输入参数。Demo 09 传入 `256`，对应 secp256k1 256-bit 私钥长度。 |
| `options` | 输入参数。Demo 09 当前传 `NULL`。 |
| 返回值 | `kStatus_SSS_Success` 表示 SE 内 key 生成成功；其他值表示曲线、对象模式、权限或 hostlib 映射不匹配。 |
| NVM 风险 | 风险取决于 `keyObject` 的对象模式。Demo 09 使用 transient 模式，因此测试 key 不写 persistent NVM；如果生产代码改为 persistent，就会创建长期私钥对象，必须有 object ID、策略、备份登记和删除方案。 |
| 业务含义 | 这是判断“SE 能否原生持有 secp256k1 私钥”的关键步骤。它成功后仍需继续通过 `sss_asymmetric_sign_digest()` 验证签名能力。 |

### `sss_key_store_get_key`

```c
sss_status_t sss_key_store_get_key(sss_key_store_t *keyStore,
                                   sss_object_t *keyObject,
                                   uint8_t *data,
                                   size_t *dataLen,
                                   size_t *pKeyBitLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 从 key store 读取对象数据。证书/binary object 可读；私钥通常不应可读。 |
| 使用 demo | Demo 07、Demo 08。 |
| `keyStore` | 输入参数。本工程使用 `&boot_ctx->ks`。 |
| `keyObject` | 输入参数。要读取的对象 handle。 |
| `data` | 输出 buffer。 |
| `dataLen` | 输入输出参数。调用前是 buffer 容量；调用后是实际读取长度。 |
| `pKeyBitLen` | 输出参数。返回对象 bit length。 |
| 返回值 | `kStatus_SSS_Success` 表示读取成功。 |
| 本工程处理 | Demo 07 读取后逐字节比较；Demo 08 读取证书用于模拟 TLS Certificate 消息。 |

### `sss_key_object_free`

```c
void sss_key_object_free(sss_object_t *keyObject);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 释放本地 SSS object handle。 |
| 使用 demo | Demo 06、Demo 07、Demo 08、Demo 09。 |
| `keyObject` | 输入参数。已经 init/get/allocate 过的对象句柄。 |
| 返回值 | 无。 |
| 注意 | 对 persistent object 来说，这只是释放本地 handle，不等于删除 SE05x NVM 对象。 |

### `sss_asymmetric_context_init`

```c
sss_status_t sss_asymmetric_context_init(sss_asymmetric_t *context,
                                         sss_session_t *session,
                                         sss_object_t *keyObject,
                                         sss_algorithm_t algorithm,
                                         sss_mode_t mode);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 初始化非对称签名/验签上下文。 |
| 使用 demo | Demo 06、Demo 08、Demo 09。 |
| `context` | 输出参数。签名或验签上下文。 |
| `session` | 输入参数。本工程使用 `&boot_ctx->session`。 |
| `keyObject` | 输入参数。签名时是 ECC private key object；验签时是 public key object。 |
| `algorithm` | 输入参数。本工程使用 `kAlgorithm_SSS_SHA256`，表示传入的是 SHA-256 digest。 |
| `mode` | 输入参数。签名用 `kMode_SSS_Sign`；验签用 `kMode_SSS_Verify`。 |
| 返回值 | `kStatus_SSS_Success` 表示 context 初始化成功。 |

### `sss_asymmetric_sign_digest`

```c
sss_status_t sss_asymmetric_sign_digest(sss_asymmetric_t *context,
                                        uint8_t *digest,
                                        size_t digestLen,
                                        uint8_t *signature,
                                        size_t *signatureLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 用 SE05x 内私钥对 digest 做 ECDSA 签名。 |
| 使用 demo | Demo 06、Demo 08、Demo 09。 |
| `context` | 输入参数。必须是 sign mode。 |
| `digest` | 输入参数。待签名 digest。本工程使用 32 字节示例 digest。 |
| `digestLen` | 输入参数。digest 长度，当前为 32。 |
| `signature` | 输出 buffer。保存 DER/编码后的签名数据。 |
| `signatureLen` | 输入输出参数。调用前是 signature buffer 容量；调用后是实际签名长度。 |
| 返回值 | `kStatus_SSS_Success` 表示签名成功。 |
| 业务意义 | 对应云端 challenge 签名或 TLS CertificateVerify 签名。 |

### `sss_asymmetric_verify_digest`

```c
sss_status_t sss_asymmetric_verify_digest(sss_asymmetric_t *context,
                                          uint8_t *digest,
                                          size_t digestLen,
                                          uint8_t *signature,
                                          size_t signatureLen);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 用公钥验证 digest/signature 是否匹配。 |
| 使用 demo | Demo 06、Demo 09。 |
| `context` | 输入参数。必须是 verify mode。 |
| `digest` | 输入参数。原始 digest。 |
| `digestLen` | 输入参数。digest 长度。 |
| `signature` | 输入参数。待验证签名。 |
| `signatureLen` | 输入参数。签名长度。 |
| 返回值 | `kStatus_SSS_Success` 表示验签成功；失败表示签名不匹配或对象/算法不匹配。 |
| 本工程处理 | Demo 06 用 transient public key 验证 SE 内私钥签名；Demo 09 用 transient secp256k1 key 验证刚生成的签名，证明 key pair 和签名链路成立。 |

### `sss_asymmetric_context_free`

```c
void sss_asymmetric_context_free(sss_asymmetric_t *context);
```

| 项目 | 说明 |
| --- | --- |
| 作用 | 释放非对称签名/验签上下文。 |
| 使用 demo | Demo 06、Demo 08、Demo 09。 |
| `context` | 输入参数。已经 init 的 asymmetric context。 |
| 返回值 | 无。 |

## Demo 与 API 对应表

| API | Demo 00 | Demo 01 | Demo 02 | Demo 03 | Demo 04 | Demo 05 | Demo 06 | Demo 07 | Demo 08 | Demo 09 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ex_sss_boot_open()` | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 |
| `ex_sss_key_store_and_object_init()` | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 | 前置 |
| `Se05x_API_GetVersion()` | 是，命令 `AT+1` | 是 | 是 | 是 | 是 | 是 | 否 | 否 | 否 | 否 |
| `Se05x_API_GetExtVersion()` | 是，命令 `AT+2` | 是 | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 否 |
| `Se05x_API_GetRandom()` | 是，命令 `AT+3` | 是 | 是 | 否 | 是，注册 nonce | 是，工站 nonce | 否 | 否 | 否 | 否 |
| `Se05x_API_ReadObject(UNIQUE_ID)` | 是，命令 `AT+4` | 是 | 是 | 否 | 是 | 否 | 否 | 否 | 否 | 否 |
| `Se05x_API_CheckObjectExists()` | 是，命令 `AT+5/6/7` | 是 | 否 | 是 | 是，Platform SCP | 是，Platform SCP/Feature | 是，ECC key | 是，cert | 是，key/cert | 是，测试 key ID |
| `Se05x_API_GetFreeMemory()` | 是，命令 `AT+8/9/A` | 是 | 否 | 是 | 否 | 是 | 否 | 否 | 否 | 否 |
| `Se05x_API_ReadIDList()` | 是，命令 `AT+E` | 是 | 否 | 是 | 否 | 否 | 否 | 否 | 否 | 否 |
| `Se05x_API_ReadECCurveList()` | 是，命令 `AT+B` | 是 | 否 | 是 | 否 | 是 | 否 | 否 | 否 | 是，检查 secp256k1 |
| `Se05x_API_CreateCurve_secp256k1()` | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 是，可能写曲线 NVM |
| `Se05x_API_ReadCryptoObjectList()` | 是，命令 `AT+C` | 是 | 否 | 是 | 否 | 是 | 否 | 否 | 否 | 否 |
| `Se05x_API_ReadState()` | 是，命令 `AT+D` | 是 | 是 | 否 | 是 | 否 | 否 | 否 | 否 | 否 |
| `sss_key_object_init()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 是 | 是 | 是 |
| `sss_key_object_allocate_handle()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 是 | 否 | 是，transient key |
| `sss_key_object_get_handle()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 是 | 是 | 否 |
| `sss_key_store_set_key()` | 否 | 否 | 否 | 否 | 否 | 否 | 是，写 ECC key | 是，写 cert | 否 | 否 |
| `sss_key_store_generate_key()` | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 是，SE 内生成 transient key |
| `sss_key_store_get_key()` | 否 | 否 | 否 | 否 | 否 | 否 | 否 | 是，读 cert | 是，读 cert | 否 |
| `sss_key_object_free()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 是 | 是 | 是 |
| `sss_asymmetric_context_init()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 否 | 是 | 是 |
| `sss_asymmetric_sign_digest()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 否 | 是 | 是 |
| `sss_asymmetric_verify_digest()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 否 | 否 | 是 |
| `sss_asymmetric_context_free()` | 否 | 否 | 否 | 否 | 否 | 否 | 是 | 否 | 是 | 是 |

## 新增 API 文档规则

后续新增 demo 时，每使用一个新的 SE05x/SSS API，都应该在本文档补充：

1. 函数签名。
2. 作用。
3. 使用 demo。
4. 每个输入参数说明。
5. 每个输出参数说明。
6. 返回值判断。
7. 写 NVM 风险说明。

如果 API 会创建、修改或删除 SE05x persistent object，必须在 demo README 和源码文件头部额外写清 object ID、覆盖策略、清理方式和失败恢复方式。
