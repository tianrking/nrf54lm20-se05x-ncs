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

LOG_MODULE_REGISTER(se05x_demo_identity, LOG_LEVEL_INF);

/*
 * 02 号 demo：IDENTITY_RANDOM，身份信息和随机数快速检查。
 *
 * 适用场景：
 *   当你只想快速确认“SE05x 真的连上了、SCP03 能打开、APDU 能返回数据”
 *   时，用这个 demo。它比 01 号完整测试短，更适合频繁 flash/debug。
 *
 * 执行流程：
 *   1. GetVersion：读取 applet 版本和能力位，确认当前 SE05x applet 类型。
 *   2. ReadObject(UNIQUE_ID)：读取芯片唯一 ID，确认读固定对象没问题。
 *   3. GetRandom 连续执行 3 次：观察三组随机数 preview 是否不同。
 *   4. ReadState：读取 applet 简短状态，确认会话仍然可用。
 *
 * 期望串口输出：
 *   - 能看到版本号、UniqueID、三组 Random preview 和 State preview。
 *   - 三组 Random preview 理论上每次都不同。
 *   - 最后应看到 “IDENTITY_RANDOM 汇总：... fail=0”。
 *
 * 用到的 SE05x 功能：
 *   Platform SCP03、安全通道内读取固定对象、SE05x 随机数生成器、状态读取。
 *
 * 数据安全说明：
 *   这个 demo 只读，不创建 key，不写对象，不改变 SE05x NVM。
 */

static void demo_get_version(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t version[7] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetVersion(session, version, &version_len);

	if (sw != SM_OK || version_len < sizeof(version)) {
		se05x_demo_mark_fail_sw(stats, "GetVersion", sw);
		return;
	}

	const uint16_t applet_config = ((uint16_t)version[3] << 8) | version[4];

	LOG_INF("Applet version: %u.%u.%u", version[0], version[1], version[2]);
	LOG_INF("Applet config : 0x%04" PRIX16, applet_config);
	se05x_demo_log_applet_features(applet_config);
	se05x_demo_mark_pass(stats, "GetVersion");
}

static void demo_read_unique_id(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t unique_id[SE050_MODULE_UNIQUE_ID_LEN] = { 0 };
	size_t unique_id_len = sizeof(unique_id);
	smStatus_t sw = Se05x_API_ReadObject(session, kSE05x_AppletResID_UNIQUE_ID,
					     0, 0, unique_id, &unique_id_len);

	if (sw == SM_OK) {
		se05x_demo_log_hex_preview("UniqueID", unique_id, unique_id_len);
		se05x_demo_mark_pass(stats, "ReadObject(UNIQUE_ID)");
	} else {
		se05x_demo_mark_fail_sw(stats, "ReadObject(UNIQUE_ID)", sw);
	}
}

static void demo_get_random(se05x_demo_stats_t *stats, pSe05xSession_t session,
			    unsigned int index)
{
	uint8_t random[16] = { 0 };
	size_t random_len = sizeof(random);
	smStatus_t sw = Se05x_API_GetRandom(session, sizeof(random), random, &random_len);

	if (sw == SM_OK && random_len == sizeof(random)) {
		LOG_INF("Random sample %u", index);
		se05x_demo_log_hex_preview("Random", random, random_len);
		se05x_demo_mark_pass(stats, "GetRandom");
	} else {
		se05x_demo_mark_fail_sw(stats, "GetRandom", sw);
	}
}

static void demo_read_state(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t state[128] = { 0 };
	size_t state_len = sizeof(state);
	smStatus_t sw = Se05x_API_ReadState(session, state, &state_len);

	if (sw == SM_OK) {
		se05x_demo_log_hex_preview("State", state, state_len);
		se05x_demo_mark_pass(stats, "ReadState");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadState", sw);
	}
}

static sss_status_t run_identity_random(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "IDENTITY_RANDOM");
	LOG_INF("IDENTITY_RANDOM 开始：读取 SE05x 身份信息并采样随机数");

	demo_get_version(&stats, se_session);
	demo_read_unique_id(&stats, se_session);

	for (unsigned int i = 0; i < 3U; ++i) {
		demo_get_random(&stats, se_session, i + 1U);
	}

	demo_read_state(&stats, se_session);
	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_identity_random = {
	.id = SE05X_DEMO_IDENTITY_RANDOM,
	.name = "identity_random",
	.when_to_use = "想快速确认 SCP03 已打开，并且 SE05x 能读取身份和随机数时使用。",
	.flow = "读取 applet 版本、UniqueID、三组随机数和 applet 状态。",
	.expected_output = "能看到版本、UniqueID、不同随机数 preview、状态字节，最终 fail=0。",
	.se_features = "SCP03、固定身份对象、SE05x 随机数生成器、applet 状态。",
	.run = run_identity_random,
};
