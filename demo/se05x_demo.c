#include <inttypes.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fsl_sss_ftr.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_const.h"
#include "se05x_demo.h"
#include "se05x_enums.h"

LOG_MODULE_REGISTER(se05x_demo, LOG_LEVEL_INF);

/*
 * SE05x demo 公共注册表。
 *
 * 这个文件不放具体业务 demo，只负责：
 *   1. 收集 demo/se05x_demo_xx_*.c 暴露出来的 demo 描述。
 *   2. 根据 main.c 里选中的 enum 找到对应 demo。
 *   3. 在串口上打印当前编译进固件的 demo catalog 和当前选择。
 *   4. 提供统一的 PASS/SKIP/FAIL 统计和十六进制 preview 打印。
 *
 * 串口编码约定：
 *   运行时 LOG_xxx()/printk() 只输出英文 ASCII，避免串口终端不是 UTF-8 时
 *   把中文显示成乱码。完整中文解释保留在 README 和各 demo 文件顶部注释中。
 *
 * 统计规则：
 *   PASS：这个步骤是必须项，且已经成功。
 *   SKIP：这个步骤是可选项，或者当前 applet/OEF 不支持，不影响总体通过。
 *   FAIL：这个步骤是必须项但失败，只要 fail 非 0，demo 总体就是 FAILED。
 */

extern const se05x_demo_t g_se05x_demo_uart_safe_api;
extern const se05x_demo_t g_se05x_demo_safe_read_only;
extern const se05x_demo_t g_se05x_demo_identity_random;
extern const se05x_demo_t g_se05x_demo_inventory;
extern const se05x_demo_t g_se05x_demo_business_onboarding;
extern const se05x_demo_t g_se05x_demo_provisioning_check;
extern const se05x_demo_t g_se05x_demo_ecc_sign_verify;
extern const se05x_demo_t g_se05x_demo_certificate_store;
extern const se05x_demo_t g_se05x_demo_tls_client_identity;
extern const se05x_demo_t g_se05x_demo_wallet_curve_check;

static const se05x_demo_t *const g_demos[] = {
	&g_se05x_demo_uart_safe_api,
	&g_se05x_demo_safe_read_only,
	&g_se05x_demo_identity_random,
	&g_se05x_demo_inventory,
	&g_se05x_demo_business_onboarding,
	&g_se05x_demo_provisioning_check,
	&g_se05x_demo_ecc_sign_verify,
	&g_se05x_demo_certificate_store,
	&g_se05x_demo_tls_client_identity,
	&g_se05x_demo_wallet_curve_check,
};

#define DEMO_COUNT (sizeof(g_demos) / sizeof(g_demos[0]))

const se05x_demo_t *se05x_demo_find(se05x_demo_id_t id)
{
	for (size_t i = 0; i < DEMO_COUNT; ++i) {
		if (g_demos[i]->id == id) {
			return g_demos[i];
		}
	}

	return NULL;
}

void se05x_demo_log_catalog(void)
{
	LOG_INF("Available SE05x demos:");
	for (size_t i = 0; i < DEMO_COUNT; ++i) {
		LOG_INF("  id=%d name=%s", g_demos[i]->id, g_demos[i]->name);
	}
}

void se05x_demo_log_selection(const se05x_demo_t *demo)
{
	if (demo == NULL) {
		LOG_ERR("No demo selected");
		return;
	}

	LOG_INF("Selected demo: id=%d name=%s", demo->id, demo->name);
	LOG_INF("Demo details are documented in demo/README.md and api/README.md");
}

const char *se05x_demo_active_scp03_profile(void)
{
#if defined(SSS_PFSCP_ENABLE_SE051A_0001A920) && SSS_PFSCP_ENABLE_SE051A_0001A920
	return "SE051A_0001A920";
#elif defined(SSS_PFSCP_ENABLE_SE051C_0005A8FA) && SSS_PFSCP_ENABLE_SE051C_0005A8FA
	return "SE051C_0005A8FA";
#elif defined(SSS_PFSCP_ENABLE_SE051A2) && SSS_PFSCP_ENABLE_SE051A2
	return "SE051A2";
#elif defined(SSS_PFSCP_ENABLE_SE051C2) && SSS_PFSCP_ENABLE_SE051C2
	return "SE051C2";
#elif defined(SSS_PFSCP_ENABLE_SE050E_0001A921) && SSS_PFSCP_ENABLE_SE050E_0001A921
	return "SE050E_0001A921";
#elif defined(SSS_PFSCP_ENABLE_SE051W_0005A739) && SSS_PFSCP_ENABLE_SE051W_0005A739
	return "SE051W_0005A739";
#elif defined(SSS_PFSCP_ENABLE_SE052_B501) && SSS_PFSCP_ENABLE_SE052_B501
	return "SE052_B501";
#elif defined(SSS_PFSCP_ENABLE_SE050F2_0001A92A) && SSS_PFSCP_ENABLE_SE050F2_0001A92A
	return "SE050F2_0001A92A";
#elif defined(SSS_PFSCP_ENABLE_A5000_0004A736) && SSS_PFSCP_ENABLE_A5000_0004A736
	return "A5000_0004A736";
#elif defined(SSS_PFSCP_ENABLE_SE050F2) && SSS_PFSCP_ENABLE_SE050F2
	return "SE050F2";
#elif defined(SSS_PFSCP_ENABLE_SE050_DEVKIT) && SSS_PFSCP_ENABLE_SE050_DEVKIT
	return "SE050_DEVKIT";
#elif defined(SSS_PFSCP_ENABLE_SE050A1) && SSS_PFSCP_ENABLE_SE050A1
	return "SE050A1";
#elif defined(SSS_PFSCP_ENABLE_SE050A2) && SSS_PFSCP_ENABLE_SE050A2
	return "SE050A2";
#elif defined(SSS_PFSCP_ENABLE_SE050B1) && SSS_PFSCP_ENABLE_SE050B1
	return "SE050B1";
#elif defined(SSS_PFSCP_ENABLE_SE050B2) && SSS_PFSCP_ENABLE_SE050B2
	return "SE050B2";
#elif defined(SSS_PFSCP_ENABLE_SE050C1) && SSS_PFSCP_ENABLE_SE050C1
	return "SE050C1";
#elif defined(SSS_PFSCP_ENABLE_SE050C2) && SSS_PFSCP_ENABLE_SE050C2
	return "SE050C2";
#else
	return "unknown";
#endif
}

void se05x_demo_stats_init(se05x_demo_stats_t *stats, const char *tag)
{
	memset(stats, 0, sizeof(*stats));
	stats->tag = tag;
}

sss_status_t se05x_demo_stats_result(const se05x_demo_stats_t *stats)
{
	return stats->fail == 0U ? kStatus_SSS_Success : kStatus_SSS_Fail;
}

void se05x_demo_log_summary(const se05x_demo_stats_t *stats)
{
	LOG_INF("%s summary: pass=%u skip=%u fail=%u", stats->tag, stats->pass,
		stats->skip, stats->fail);
}

void se05x_demo_mark_pass(se05x_demo_stats_t *stats, const char *name)
{
	++stats->pass;
	LOG_INF("%s PASS %s", stats->tag, name);
}

void se05x_demo_mark_fail_sw(se05x_demo_stats_t *stats, const char *name, smStatus_t sw)
{
	++stats->fail;
	LOG_ERR("%s FAIL %s sw=0x%04" PRIX16, stats->tag, name, (uint16_t)sw);
}

void se05x_demo_mark_skip_sw(se05x_demo_stats_t *stats, const char *name, smStatus_t sw)
{
	++stats->skip;
	LOG_WRN("%s SKIP %s sw=0x%04" PRIX16, stats->tag, name, (uint16_t)sw);
}

void se05x_demo_mark_fail_status(se05x_demo_stats_t *stats, const char *name,
				 sss_status_t status)
{
	++stats->fail;
	LOG_ERR("%s FAIL %s status=0x%04X", stats->tag, name, (unsigned int)status);
}

void se05x_demo_log_hex_preview(const char *label, const uint8_t *data, size_t data_len)
{
	char line[(3 * 16) + 1] = { 0 };
	const size_t shown = data_len < 16U ? data_len : 16U;

	for (size_t i = 0; i < shown; ++i) {
		(void)snprintk(&line[i * 3U], sizeof(line) - (i * 3U), "%02X ", data[i]);
	}

	LOG_INF("%s len=%u preview=%s%s", label, (unsigned int)data_len, line,
		data_len > shown ? "..." : "");
}

void se05x_demo_log_applet_features(uint16_t applet_config)
{
#define LOG_FEATURE(name)                                                                      \
	LOG_INF("%-18s: %s", #name,                                                             \
		((applet_config & kSE05x_AppletConfig_##name) ==                                  \
		 kSE05x_AppletConfig_##name) ? "yes" : "no")

	LOG_FEATURE(ECDSA_ECDH_ECDHE);
	LOG_FEATURE(EDDSA);
	LOG_FEATURE(DH_MONT);
	LOG_FEATURE(HMAC);
	LOG_FEATURE(RSA_PLAIN);
	LOG_FEATURE(RSA_CRT);
	LOG_FEATURE(AES);
	LOG_FEATURE(DES);
	LOG_FEATURE(PBKDF);
	LOG_FEATURE(TLS);
	LOG_FEATURE(MIFARE);
	LOG_FEATURE(I2CM);

#undef LOG_FEATURE
}
