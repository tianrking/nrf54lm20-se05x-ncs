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

LOG_MODULE_REGISTER(se05x_demo_safe, LOG_LEVEL_INF);

/*
 * 01 号 demo：SAFE_READ_ONLY，完整只读冒烟测试。
 *
 * 适用场景：
 *   这个 demo 适合在板子 bring-up 阶段运行。也就是说，硬件连线、I2C、
 *   J-Link、串口、PSA crypto、Platform SCP03 都应该已经基本可用。它会
 *   尽量覆盖多种“只读 APDU”，用来确认 nRF54LM20 和 SE05x 的基础链路
 *   是否稳定。
 *
 * 为什么叫 safe/read-only：
 *   这个 demo 不创建对象、不写 key、不删除对象、不做个性化，不会改变
 *   SE05x NVM 里的用户数据。它只读取 applet、能力、随机数、固定对象和
 *   一些状态信息，所以适合反复跑。
 *
 * 执行流程：
 *   1. GetVersion：读取 applet 版本号和 applet_config 能力位。
 *   2. GetExtVersion：读取扩展版本信息，用来区分更细的 applet/OEF 状态。
 *   3. GetRandom：让 SE05x 生成 16 字节随机数，验证安全通道后 APDU 可用。
 *   4. ReadObject(UNIQUE_ID)：读取芯片固定唯一 ID。
 *   5. CheckObjectExists：检查 UNIQUE_ID、FEATURE、PLATFORM_SCP 等保留对象。
 *   6. GetFreeMemory：读取持久区和瞬态区剩余空间。
 *   7. ReadIDList/ReadECCurveList/ReadCryptoObjectList/ReadState：读取对象、
 *      曲线和状态清单。某些 applet/OEF 可能不允许 ReadIDList，这种情况
 *      会记录为 SKIP，而不是 FAIL。
 *
 * 期望串口输出：
 *   - 能看到 Applet version、Applet config、UniqueID、Random preview。
 *   - 最后应看到 “SAFE_READ_ONLY 汇总：... fail=0”。
 *   - main.c 最后应打印 “Demo safe_read_only 总体结果：OK”。
 *
 * 用到的 SE05x 功能：
 *   Platform SCP03 安全会话、版本读取、随机数、固定对象读取、保留对象检查、
 *   空间查询、曲线清单、对象清单和 applet 状态读取。
 */

static smStatus_t test_get_version(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t version[7] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetVersion(session, version, &version_len);

	if (sw != SM_OK || version_len < sizeof(version)) {
		se05x_demo_mark_fail_sw(stats, "GetVersion", sw);
		return sw;
	}

	const uint16_t applet_config = ((uint16_t)version[3] << 8) | version[4];

	LOG_INF("Applet version: %u.%u.%u", version[0], version[1], version[2]);
	LOG_INF("Applet config : 0x%04" PRIX16, applet_config);
	LOG_INF("SecureBox     : %u.%u", version[5], version[6]);
	se05x_demo_log_applet_features(applet_config);
	se05x_demo_mark_pass(stats, "GetVersion");
	return sw;
}

static void test_get_ext_version(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t version[48] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetExtVersion(session, version, &version_len);

	if (sw == SM_OK) {
		se05x_demo_log_hex_preview("ExtVersion", version, version_len);
		se05x_demo_mark_pass(stats, "GetExtVersion");
	} else {
		se05x_demo_mark_skip_sw(stats, "GetExtVersion", sw);
	}
}

static void test_get_random(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t random[16] = { 0 };
	size_t random_len = sizeof(random);
	smStatus_t sw = Se05x_API_GetRandom(session, sizeof(random), random, &random_len);

	if (sw == SM_OK && random_len == sizeof(random)) {
		se05x_demo_log_hex_preview("Random", random, random_len);
		se05x_demo_mark_pass(stats, "GetRandom");
	} else {
		se05x_demo_mark_fail_sw(stats, "GetRandom", sw);
	}
}

static void test_read_unique_id(se05x_demo_stats_t *stats, pSe05xSession_t session)
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

static void test_check_object_exists(se05x_demo_stats_t *stats, pSe05xSession_t session,
				     uint32_t object_id, const char *name)
{
	SE05x_Result_t result = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(session, object_id, &result);

	if (sw == SM_OK) {
		LOG_INF("%s exists=%s", name,
			result == kSE05x_Result_SUCCESS ? "yes" : "no");
		se05x_demo_mark_pass(stats, name);
	} else {
		se05x_demo_mark_skip_sw(stats, name, sw);
	}
}

static void test_free_memory(se05x_demo_stats_t *stats, pSe05xSession_t session,
			     SE05x_MemoryType_t type, const char *name)
{
	uint32_t free_mem = 0;
	smStatus_t sw = Se05x_API_GetFreeMemory(session, type, &free_mem);

	if (sw == SM_OK) {
		LOG_INF("%s free=%" PRIu32, name, free_mem);
		se05x_demo_mark_pass(stats, name);
	} else {
		se05x_demo_mark_skip_sw(stats, name, sw);
	}
}

static void test_read_id_list(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t more = 0;
	uint8_t id_list[128] = { 0 };
	size_t id_list_len = sizeof(id_list);
	smStatus_t sw = Se05x_API_ReadIDList(session, 0, 0xFF, &more, id_list,
					     &id_list_len);

	if (sw == SM_OK) {
		LOG_INF("ReadIDList len=%u more=0x%02X", (unsigned int)id_list_len, more);
		se05x_demo_log_hex_preview("IDList", id_list, id_list_len);
		se05x_demo_mark_pass(stats, "ReadIDList");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadIDList", sw);
	}
}

static void test_read_curve_list(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t curve_list[128] = { 0 };
	size_t curve_list_len = sizeof(curve_list);
	smStatus_t sw = Se05x_API_ReadECCurveList(session, curve_list, &curve_list_len);

	if (sw == SM_OK) {
		LOG_INF("ReadECCurveList len=%u", (unsigned int)curve_list_len);
		se05x_demo_log_hex_preview("CurveList", curve_list, curve_list_len);
		se05x_demo_mark_pass(stats, "ReadECCurveList");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadECCurveList", sw);
	}
}

static void test_read_crypto_object_list(se05x_demo_stats_t *stats,
					 pSe05xSession_t session)
{
	uint8_t list[128] = { 0 };
	size_t list_len = sizeof(list);
	smStatus_t sw = Se05x_API_ReadCryptoObjectList(session, list, &list_len);

	if (sw == SM_OK) {
		LOG_INF("ReadCryptoObjectList len=%u", (unsigned int)list_len);
		se05x_demo_log_hex_preview("CryptoObjectList", list, list_len);
		se05x_demo_mark_pass(stats, "ReadCryptoObjectList");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadCryptoObjectList", sw);
	}
}

static void test_read_state(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	uint8_t state[128] = { 0 };
	size_t state_len = sizeof(state);
	smStatus_t sw = Se05x_API_ReadState(session, state, &state_len);

	if (sw == SM_OK) {
		LOG_INF("ReadState len=%u", (unsigned int)state_len);
		se05x_demo_log_hex_preview("State", state, state_len);
		se05x_demo_mark_pass(stats, "ReadState");
	} else {
		se05x_demo_mark_skip_sw(stats, "ReadState", sw);
	}
}

static sss_status_t run_safe_read_only(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "SAFE_READ_ONLY");
	LOG_INF("SAFE_READ_ONLY started: read-only test, no NVM writes, no object creation");

	if (test_get_version(&stats, se_session) != SM_OK) {
		se05x_demo_log_summary(&stats);
		return se05x_demo_stats_result(&stats);
	}

	test_get_ext_version(&stats, se_session);
	test_get_random(&stats, se_session);
	test_read_unique_id(&stats, se_session);
	test_check_object_exists(&stats, se_session, kSE05x_AppletResID_UNIQUE_ID,
				 "CheckObjectExists(UNIQUE_ID)");
	test_check_object_exists(&stats, se_session, kSE05x_AppletResID_FEATURE,
				 "CheckObjectExists(FEATURE)");
	test_check_object_exists(&stats, se_session, kSE05x_AppletResID_PLATFORM_SCP,
				 "CheckObjectExists(PLATFORM_SCP)");
	test_free_memory(&stats, se_session, kSE05x_MemoryType_PERSISTENT,
			 "GetFreeMemory(PERSISTENT)");
	test_free_memory(&stats, se_session, kSE05x_MemoryType_TRANSIENT_RESET,
			 "GetFreeMemory(TRANSIENT_RESET)");
	test_free_memory(&stats, se_session, kSE05x_MemoryType_TRANSIENT_DESELECT,
			 "GetFreeMemory(TRANSIENT_DESELECT)");
	test_read_id_list(&stats, se_session);
	test_read_curve_list(&stats, se_session);
	test_read_crypto_object_list(&stats, se_session);
	test_read_state(&stats, se_session);

	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_safe_read_only = {
	.id = SE05X_DEMO_SAFE_READ_ONLY,
	.name = "safe_read_only",
	.when_to_use = "I2C、串口、PSA RNG、Platform SCP03 都准备好后，用它做完整只读冒烟测试。",
	.flow = "打开 Platform SCP03 后，读取版本、随机数、唯一 ID、能力、内存和状态。",
	.expected_output = "必须项 PASS，可选项允许 SKIP，最终 fail=0 且总体结果 OK。",
	.se_features = "SCP03 安全会话和多组只读 APDU；不会写入或创建 SE05x 对象。",
	.run = run_safe_read_only,
};
