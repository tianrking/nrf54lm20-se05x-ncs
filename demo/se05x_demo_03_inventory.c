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

LOG_MODULE_REGISTER(se05x_demo_inventory, LOG_LEVEL_INF);

/*
 * 03 号 demo：INVENTORY，能力、对象和空间清单。
 *
 * 适用场景：
 *   当你在比较不同 SE05x 模块、不同 OEF、不同出厂配置，或者想知道当前
 *   这颗 SE05x 暴露了哪些保留对象/曲线/空间时，用这个 demo。它回答的
 *   问题是：“这颗 SE 当前有哪些能力和资源？”
 *
 * 执行流程：
 *   1. CheckObjectExists：检查 UNIQUE_ID、FEATURE、PLATFORM_SCP 等保留对象。
 *   2. GetFreeMemory：读取持久存储、reset 瞬态存储、deselect 瞬态存储。
 *   3. ReadECCurveList：读取 applet 支持的 ECC 曲线能力位。
 *   4. ReadCryptoObjectList：读取 crypto object 清单。
 *   5. ReadIDList：尝试读取对象 ID 清单。有些 applet/OEF 会拒绝这个命令，
 *      所以这里把失败记为 SKIP，不作为总体失败。
 *
 * 期望串口输出：
 *   - 能看到 reserved object 是否存在。
 *   - 能看到 PERSISTENT / TRANSIENT_RESET / TRANSIENT_DESELECT 剩余空间。
 *   - 能看到曲线列表和 crypto object list。
 *   - 最后应看到 “INVENTORY 汇总：... fail=0”。ReadIDList 可以 SKIP。
 *
 * 用到的 SE05x 功能：
 *   Platform SCP03、保留对象检查、内存容量查询、ECC 曲线清单、crypto object
 *   清单、对象 ID 清单读取。
 *
 * 数据安全说明：
 *   这个 demo 只读 metadata，不读取私钥材料，不创建、不更新、不删除对象。
 */

static void inventory_get_version(se05x_demo_stats_t *stats, pSe05xSession_t session)
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

static void inventory_check_object(se05x_demo_stats_t *stats, pSe05xSession_t session,
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

static void inventory_free_memory(se05x_demo_stats_t *stats, pSe05xSession_t session,
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

static void inventory_curve_list(se05x_demo_stats_t *stats, pSe05xSession_t session)
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

static void inventory_crypto_object_list(se05x_demo_stats_t *stats,
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

static void inventory_id_list(se05x_demo_stats_t *stats, pSe05xSession_t session)
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

static sss_status_t run_inventory(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "INVENTORY");
	LOG_INF("INVENTORY 开始：检查 SE05x applet 资源、能力和存储空间");

	inventory_get_version(&stats, se_session);
	inventory_check_object(&stats, se_session, kSE05x_AppletResID_UNIQUE_ID,
			       "CheckObjectExists(UNIQUE_ID)");
	inventory_check_object(&stats, se_session, kSE05x_AppletResID_FEATURE,
			       "CheckObjectExists(FEATURE)");
	inventory_check_object(&stats, se_session, kSE05x_AppletResID_PLATFORM_SCP,
			       "CheckObjectExists(PLATFORM_SCP)");
	inventory_free_memory(&stats, se_session, kSE05x_MemoryType_PERSISTENT,
			      "GetFreeMemory(PERSISTENT)");
	inventory_free_memory(&stats, se_session, kSE05x_MemoryType_TRANSIENT_RESET,
			      "GetFreeMemory(TRANSIENT_RESET)");
	inventory_free_memory(&stats, se_session, kSE05x_MemoryType_TRANSIENT_DESELECT,
			      "GetFreeMemory(TRANSIENT_DESELECT)");
	inventory_curve_list(&stats, se_session);
	inventory_crypto_object_list(&stats, se_session);
	inventory_id_list(&stats, se_session);

	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_inventory = {
	.id = SE05X_DEMO_INVENTORY,
	.name = "inventory",
	.when_to_use = "比较 SE05x/OEF 差异，或在做功能开发前查看当前芯片能力和资源。",
	.flow = "检查保留对象、内存计数、曲线支持、crypto object list 和 ID list。",
	.expected_output = "metadata 日志正常，最终 fail=0；ReadIDList 在部分 OEF 上允许 SKIP。",
	.se_features = "SCP03、保留资源、存储空间计数、曲线和对象清单 APDU。",
	.run = run_inventory,
};
