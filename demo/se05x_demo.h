#ifndef SE05X_DEMO_H
#define SE05X_DEMO_H

#include <stddef.h>
#include <stdint.h>

#include "ex_sss_boot.h"
#include "fsl_sss_api.h"
#include "sm_types.h"

typedef enum {
	SE05X_DEMO_SAFE_READ_ONLY = 0,  /* 01：完整只读冒烟测试 */
	SE05X_DEMO_IDENTITY_RANDOM,     /* 02：身份信息和随机数快速检查 */
	SE05X_DEMO_INVENTORY,           /* 03：能力、对象和空间清单 */
} se05x_demo_id_t;

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
