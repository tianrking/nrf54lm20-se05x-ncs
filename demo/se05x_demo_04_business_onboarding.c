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

LOG_MODULE_REGISTER(se05x_demo_onboarding, LOG_LEVEL_INF);

/*
 * 04 号业务 demo：BUSINESS_ONBOARDING，设备注册/产测上报前置流程。
 *
 * 真实业务含义：
 *   产品第一次上电、产线测试、设备绑定到云平台之前，通常需要采集一份
 *   “这颗设备是谁、SE 是否存在、SE 的安全通道是否打开、随机挑战是否可用”
 *   的注册材料。真实云端注册还会包含设备证书、签名、批次号、生产工站信息等。
 *
 * 当前阶段边界：
 *   现在仍使用官方/default Platform SCP03 key/profile，不改配置，不写 SE05x NVM。
 *   因此本 demo 不生成应用私钥、不导入证书、不做 TLS，只完成真实注册流程的
 *   “安全读取和注册 payload 准备”部分。
 *
 * 真实产品后续会在此流程后继续做：
 *   1. 产线或安全工站创建/导入应用私钥和证书。
 *   2. 云端保存 unique ID、证书公钥、批次和设备型号。
 *   3. 设备上线时用 SE 内私钥签名云端 challenge，证明私钥在 SE 内。
 *
 * 当前执行流程：
 *   1. GetVersion：确认 SE05x applet 和能力位。
 *   2. ReadObject(UNIQUE_ID)：读取芯片唯一 ID，作为注册主身份之一。
 *   3. CheckObjectExists(PLATFORM_SCP)：确认当前安全通道依赖对象存在。
 *   4. GetRandom(32)：生成注册 nonce，用于真实业务里的防重放挑战。
 *   5. ReadState：读取状态摘要，作为注册诊断字段。
 *
 * 数据安全说明：
 *   只读和随机数生成，不创建对象，不写 NVM，不改变 SE05x 配置。
 */

static void onboarding_get_version(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t version[7] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetVersion(session, version, &version_len);

	if (sw != SM_OK || version_len < sizeof(version)) {
		se05x_demo_mark_fail_sw(stats, "GetVersion", sw);
		return;
	}

	const uint16_t applet_config = ((uint16_t)version[3] << 8) | version[4];

	LOG_INF("Business field: se_profile=%s", se05x_demo_active_scp03_profile());
	LOG_INF("Business field: applet_version=%u.%u.%u", version[0], version[1],
		version[2]);
	LOG_INF("Business field: applet_config=0x%04" PRIX16, applet_config);
	se05x_demo_log_applet_features(applet_config);
	se05x_demo_mark_pass(stats, "GetVersion");
}

static void onboarding_read_unique_id(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t unique_id[SE050_MODULE_UNIQUE_ID_LEN] = { 0 };
	size_t unique_id_len = sizeof(unique_id);
	smStatus_t sw = Se05x_API_ReadObject(session, kSE05x_AppletResID_UNIQUE_ID,
					     0, 0, unique_id, &unique_id_len);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "ReadObject(UNIQUE_ID)", sw);
		return;
	}

	LOG_INF("Business field: device_unique_id");
	se05x_demo_log_hex_preview("UniqueID", unique_id, unique_id_len);
	se05x_demo_mark_pass(stats, "ReadObject(UNIQUE_ID)");
}

static void onboarding_check_platform_scp(se05x_demo_stats_t *stats,
					  pSe05xSession_t session)
{
	SE05x_Result_t result = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(session,
						    kSE05x_AppletResID_PLATFORM_SCP,
						    &result);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "CheckObjectExists(PLATFORM_SCP)", sw);
		return;
	}

	LOG_INF("Business field: platform_scp_object=%s",
		result == kSE05x_Result_SUCCESS ? "present" : "missing");
	if (result == kSE05x_Result_SUCCESS) {
		se05x_demo_mark_pass(stats, "CheckObjectExists(PLATFORM_SCP)");
	} else {
		se05x_demo_mark_fail_sw(stats, "CheckObjectExists(PLATFORM_SCP)", sw);
	}
}

static void onboarding_get_registration_nonce(se05x_demo_stats_t *stats,
					      pSe05xSession_t session)
{
	uint8_t nonce[32] = { 0 };
	size_t nonce_len = sizeof(nonce);
	smStatus_t sw = Se05x_API_GetRandom(session, sizeof(nonce), nonce, &nonce_len);

	if (sw != SM_OK || nonce_len != sizeof(nonce)) {
		se05x_demo_mark_fail_sw(stats, "GetRandom(REGISTER_NONCE)", sw);
		return;
	}

	LOG_INF("Business field: registration_nonce");
	se05x_demo_log_hex_preview("RegisterNonce", nonce, nonce_len);
	se05x_demo_mark_pass(stats, "GetRandom(REGISTER_NONCE)");
}

static void onboarding_read_state(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t state[128] = { 0 };
	size_t state_len = sizeof(state);
	smStatus_t sw = Se05x_API_ReadState(session, state, &state_len);

	if (sw == SM_OK) {
		LOG_INF("Business field: se_state");
		se05x_demo_log_hex_preview("State", state, state_len);
		se05x_demo_mark_pass(stats, "ReadState");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadState", sw);
	}
}

static sss_status_t run_business_onboarding(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "BUSINESS_ONBOARDING");
	LOG_INF("BUSINESS_ONBOARDING started: prepare SE fields for registration/factory report");
	LOG_INF("Business payload model: unique_id + applet_version + scp_profile + nonce + state");
	LOG_INF("No NVM writes, no app key creation, no certificate import; SCP03 profile unchanged");

	onboarding_get_version(&stats, se_session);
	onboarding_read_unique_id(&stats, se_session);
	onboarding_check_platform_scp(&stats, se_session);
	onboarding_get_registration_nonce(&stats, se_session);
	onboarding_read_state(&stats, se_session);

	LOG_INF("Business next step: upload these fields with factory record or registration request");
	LOG_INF("Production hardening: add SE private-key challenge signing to prove non-exportability");
	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_business_onboarding = {
	.id = SE05X_DEMO_BUSINESS_ONBOARDING,
	.name = "business_onboarding",
	.when_to_use = "做设备注册、产测上报、云端绑定前，确认 SE 身份和注册 nonce 可用时使用。",
	.flow = "读取 applet 版本、unique ID、Platform SCP 对象状态、注册 nonce 和 SE state。",
	.expected_output = "看到 unique_id、scp_profile、registration_nonce、state，最终 fail=0。",
	.se_features = "SCP03、UNIQUE_ID 固定对象、SE05x 随机数、对象存在检查、状态读取。",
	.run = run_business_onboarding,
};
