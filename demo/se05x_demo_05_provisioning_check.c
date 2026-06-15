#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include "fsl_sss_se05x_types.h"
#include "se05x_04_xx_APDU_apis.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_const.h"
#include "se05x_demo.h"
#include "se05x_enums.h"

LOG_MODULE_REGISTER(se05x_demo_provision, LOG_LEVEL_INF);

/*
 * 05 号业务 demo：PROVISIONING_CHECK，应用密钥/证书写入前预检流程。
 *
 * 真实业务含义：
 *   真正产品会把应用私钥、证书、TLS 身份或业务 HMAC/AES key 放进 SE05x。
 *   写入之前，产线程序需要确认：
 *     - 当前 SE applet 是否支持目标算法；
 *     - Platform SCP03 通道是否能打开；
 *     - persistent 空间是否足够；
 *     - transient 空间和 crypto object 状态是否正常；
 *     - ECC curve 能力是否可见。
 *
 * 当前阶段边界：
 *   现在先不改官方/default SCP03 配置，也不创建 key，不导证书，不删除对象。
 *   本 demo 是真实 provisioning 工站的“写入前检查”部分，可以放心反复运行。
 *
 * 后续真正写入型 demo 应该新增独立编号，例如：
 *   06_create_ecc_key、07_import_certificate、08_tls_client_identity。
 *   那些 demo 必须明确 object ID、覆盖策略、清理方式和失败恢复方法。
 */

static void provisioning_get_version(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t version[7] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetVersion(session, version, &version_len);

	if (sw != SM_OK || version_len < sizeof(version)) {
		se05x_demo_mark_fail_sw(stats, "GetVersion", sw);
		return;
	}

	const uint16_t applet_config = ((uint16_t)version[3] << 8) | version[4];

	LOG_INF("Provisioning field: scp_profile=%s", se05x_demo_active_scp03_profile());
	LOG_INF("Provisioning field: applet_version=%u.%u.%u", version[0], version[1],
		version[2]);
	LOG_INF("Provisioning field: applet_config=0x%04" PRIX16, applet_config);
	se05x_demo_log_applet_features(applet_config);
	se05x_demo_mark_pass(stats, "GetVersion");
}

static void provisioning_check_object(se05x_demo_stats_t *stats, pSe05xSession_t session,
				      uint32_t object_id, const char *name)
{
	SE05x_Result_t result = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(session, object_id, &result);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, name, sw);
		return;
	}

	LOG_INF("%s exists=%s", name, result == kSE05x_Result_SUCCESS ? "yes" : "no");
	if (result == kSE05x_Result_SUCCESS) {
		se05x_demo_mark_pass(stats, name);
	} else {
		se05x_demo_mark_fail_sw(stats, name, sw);
	}
}

static void provisioning_free_memory(se05x_demo_stats_t *stats, pSe05xSession_t session,
				     SE05x_MemoryType_t type, const char *name)
{
	uint32_t free_mem = 0;
	smStatus_t sw = Se05x_API_GetFreeMemory(session, type, &free_mem);

	if (sw == SM_OK) {
		LOG_INF("%s free=%" PRIu32, name, free_mem);
		se05x_demo_mark_pass(stats, name);
	} else {
		se05x_demo_mark_fail_sw(stats, name, sw);
	}
}

static void provisioning_curve_list(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t curve_list[128] = { 0 };
	size_t curve_list_len = sizeof(curve_list);
	smStatus_t sw = Se05x_API_ReadECCurveList(session, curve_list, &curve_list_len);

	if (sw == SM_OK) {
		LOG_INF("Provisioning field: ECC curve list");
		se05x_demo_log_hex_preview("CurveList", curve_list, curve_list_len);
		se05x_demo_mark_pass(stats, "ReadECCurveList");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadECCurveList", sw);
	}
}

static void provisioning_crypto_object_list(se05x_demo_stats_t *stats,
					    pSe05xSession_t session)
{
	uint8_t list[128] = { 0 };
	size_t list_len = sizeof(list);
	smStatus_t sw = Se05x_API_ReadCryptoObjectList(session, list, &list_len);

	if (sw == SM_OK) {
		LOG_INF("Provisioning field: crypto object list len=%u",
			(unsigned int)list_len);
		se05x_demo_log_hex_preview("CryptoObjectList", list, list_len);
		se05x_demo_mark_pass(stats, "ReadCryptoObjectList");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadCryptoObjectList", sw);
	}
}

static void provisioning_generate_csr_nonce(se05x_demo_stats_t *stats,
					    pSe05xSession_t session)
{
	uint8_t nonce[16] = { 0 };
	size_t nonce_len = sizeof(nonce);
	smStatus_t sw = Se05x_API_GetRandom(session, sizeof(nonce), nonce, &nonce_len);

	if (sw != SM_OK || nonce_len != sizeof(nonce)) {
		se05x_demo_mark_fail_sw(stats, "GetRandom(CSR_NONCE)", sw);
		return;
	}

	LOG_INF("Provisioning field: csr_or_work_order_nonce");
	se05x_demo_log_hex_preview("ProvisioningNonce", nonce, nonce_len);
	se05x_demo_mark_pass(stats, "GetRandom(CSR_NONCE)");
}

static sss_status_t run_provisioning_check(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "PROVISIONING_CHECK");
	LOG_INF("PROVISIONING_CHECK started: precheck before app key/certificate provisioning");
	LOG_INF("No key creation, no certificate import, no SE05x NVM writes; precheck only");

	provisioning_get_version(&stats, se_session);
	provisioning_check_object(&stats, se_session, kSE05x_AppletResID_PLATFORM_SCP,
				  "CheckObjectExists(PLATFORM_SCP)");
	provisioning_check_object(&stats, se_session, kSE05x_AppletResID_FEATURE,
				  "CheckObjectExists(FEATURE)");
	provisioning_free_memory(&stats, se_session, kSE05x_MemoryType_PERSISTENT,
				 "GetFreeMemory(PERSISTENT)");
	provisioning_free_memory(&stats, se_session, kSE05x_MemoryType_TRANSIENT_RESET,
				 "GetFreeMemory(TRANSIENT_RESET)");
	provisioning_free_memory(&stats, se_session, kSE05x_MemoryType_TRANSIENT_DESELECT,
				 "GetFreeMemory(TRANSIENT_DESELECT)");
	provisioning_curve_list(&stats, se_session);
	provisioning_crypto_object_list(&stats, se_session);
	provisioning_generate_csr_nonce(&stats, se_session);

	LOG_INF("Business next step: decide whether to enter key/cert provisioning station");
	LOG_INF("Future write demos must declare object IDs explicitly to avoid production overwrite");
	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_provisioning_check = {
	.id = SE05X_DEMO_PROVISIONING_CHECK,
	.name = "provisioning_check",
	.when_to_use = "Precheck SE capabilities and memory before provisioning app keys or certificates.",
	.flow = "Read capabilities, reserved objects, memory, curves, crypto objects, and station nonce.",
	.expected_output = "Capabilities, memory, curve list, crypto object status, nonce, and final fail=0.",
	.se_features = "SCP03, feature bitmap, reserved objects, memory, ECC curves, crypto object, random.",
	.run = run_provisioning_check,
};
