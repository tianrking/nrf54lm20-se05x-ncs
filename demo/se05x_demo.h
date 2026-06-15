#ifndef SE05X_DEMO_H
#define SE05X_DEMO_H

#include <stddef.h>
#include <stdint.h>

#include "ex_sss_boot.h"
#include "fsl_sss_api.h"
#include "sm_types.h"

typedef enum {
	SE05X_DEMO_UART_SAFE_API = 0,       /* 00：UART 交互式安全 API 菜单 */
	SE05X_DEMO_SAFE_READ_ONLY = 1,      /* 01：完整只读冒烟测试 */
	SE05X_DEMO_IDENTITY_RANDOM = 2,     /* 02：身份信息和随机数快速检查 */
	SE05X_DEMO_INVENTORY = 3,           /* 03：能力、对象和空间清单 */
	SE05X_DEMO_BUSINESS_ONBOARDING = 4, /* 04：真实业务设备注册前置流程 */
	SE05X_DEMO_PROVISIONING_CHECK = 5,  /* 05：密钥和证书写入前预检流程 */
	SE05X_DEMO_ECC_SIGN_VERIFY = 6,     /* 06：SE 内 ECC 私钥签名和公钥验签 */
	SE05X_DEMO_CERTIFICATE_STORE = 7,   /* 07：设备证书对象写入和回读校验 */
	SE05X_DEMO_TLS_CLIENT_IDENTITY = 8, /* 08：TLS 客户端身份材料检查和挑战签名 */
	SE05X_DEMO_WALLET_CURVE_CHECK = 9,  /* 09：钱包 secp256k1 曲线启用和签名验证研究 */
} se05x_demo_id_t;

#define SE05X_DEMO_OBJECT_ID_ECC_KEY 0xEF060001u
#define SE05X_DEMO_OBJECT_ID_CERT    0xEF070001u
#define SE05X_DEMO_OBJECT_ID_ECC_PUB 0xEF060002u
#define SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1 0xEF090001u
#define SE05X_DEMO_ECC_KEY_BITS      256u

typedef struct {
	unsigned int pass; /* 必须成功且已经成功的检查数量 */
	unsigned int fail; /* 必须成功但失败的检查数量，非 0 表示 demo 总体失败 */
	unsigned int skip; /* 可选能力或当前 OEF 不支持时跳过的检查数量 */
	const char *tag;   /* 串口日志前缀，方便区分不同 demo */
} se05x_demo_stats_t;

typedef struct {
	se05x_demo_id_t id;          /* main.c 里用于选择 demo 的编号 */
	const char *name;            /* 短名称，串口日志里显示 */
	const char *when_to_use;     /* 适合什么情况运行 */
	const char *flow;            /* demo 的主要执行流程 */
	const char *expected_output; /* 期望串口输出和通过条件 */
	const char *se_features;     /* 用到的 SE05x 功能或 APDU 类别 */
	sss_status_t (*run)(ex_sss_boot_ctx_t *boot_ctx);
} se05x_demo_t;

const se05x_demo_t *se05x_demo_find(se05x_demo_id_t id);
void se05x_demo_log_catalog(void);
void se05x_demo_log_selection(const se05x_demo_t *demo);

const char *se05x_demo_active_scp03_profile(void);

void se05x_demo_stats_init(se05x_demo_stats_t *stats, const char *tag);
sss_status_t se05x_demo_stats_result(const se05x_demo_stats_t *stats);
void se05x_demo_log_summary(const se05x_demo_stats_t *stats);

void se05x_demo_mark_pass(se05x_demo_stats_t *stats, const char *name);
void se05x_demo_mark_fail_sw(se05x_demo_stats_t *stats, const char *name, smStatus_t sw);
void se05x_demo_mark_skip_sw(se05x_demo_stats_t *stats, const char *name, smStatus_t sw);
void se05x_demo_mark_fail_status(se05x_demo_stats_t *stats, const char *name,
				 sss_status_t status);

void se05x_demo_log_hex_preview(const char *label, const uint8_t *data, size_t data_len);
void se05x_demo_log_applet_features(uint16_t applet_config);

#endif
