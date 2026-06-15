#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "fsl_sss_se05x_types.h"
#include "se05x_04_xx_APDU_apis.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_const.h"
#include "se05x_demo.h"
#include "se05x_enums.h"

LOG_MODULE_REGISTER(se05x_demo_uart_safe_api, LOG_LEVEL_INF);

/*
 * 00 号 demo：UART_SAFE_API，串口交互式安全 API 测试台。
 *
 * 适用场景：
 *   当你不想每次为了测一个 APDU 都重新改 main.c、重新编译、重新下载时，
 *   可以切到这个 demo。它启动后会停在串口菜单，输入一个 AT+X 字符串命令就调用
 *   一个 SE05x API，并立即打印返回状态和数据 preview。
 *
 * 为什么它默认是安全的：
 *   这个菜单只放“读/查/随机数/状态/容量”类接口，不放创建对象、写 key、
 *   写证书、删除对象、改策略、改生命周期、个性化这类会改变 SE05x NVM 或
 *   安全状态的接口。因此可以反复测试，不会消耗写入型 object ID，也不会覆盖
 *   Demo 06/07 准备的 key/cert。
 *
 * “全部 API”的边界：
 *   这里的“全部”指本工程 01-05 里已经使用并验证过的安全 APDU 接口类别，
 *   不是 NXP Plug & Trust hostlib 里的全部函数。hostlib 里还有大量写入、
 *   加密、策略、删除、生命周期管理 API，它们需要单独的业务 demo 和明确的
 *   object ID/权限设计，不能放进随手输入的 UART 菜单。
 *
 * 执行流程：
 *   1. main.c 先打开 I2C、T=1 over I2C 和 Platform SCP03 安全会话。
 *   2. 本 demo 打印菜单。
 *   3. 用户在串口输入 AT+X 命令。
 *   4. demo 调用对应 SE05x API。
 *   5. 串口打印 OK/FAIL/SKIP、返回长度、状态字和数据 preview。
 *   6. 输入 AT+Q 后退出 demo，main.c 关闭 SE05x session。
 *
 * 期望串口输出：
 *   - 输入 AT+1 应返回 applet version/config。
 *   - 输入 AT+3 应返回 16 字节随机数，每次一般不同。
 *   - 输入 AT+4 应返回 UniqueID preview。
 *   - 输入 AT+E 的 ReadIDList 在部分 OEF 上可能返回非 OK，这里按 SKIP 解释。
 *
 * 用到的 SE05x 功能：
 *   Platform SCP03 安全会话内的版本读取、扩展版本、随机数、固定对象读取、
 *   保留对象存在检查、剩余空间读取、曲线列表、crypto object 列表、对象 ID
 *   列表和 applet 状态读取。
 *
 * 串口输出策略：
 *   本文件顶部注释用中文解释完整背景；实际 printk()/LOG_xxx() 输出使用
 *   英文 ASCII，避免串口工具编码不是 UTF-8 时出现中文乱码。
 *
 * 串口输入策略：
 *   使用固定的 AT+X 字符串命令，例如 AT+1、AT+3、AT+A、AT+Q。这样固件按字符串
 *   缓冲区解析命令，不再依赖“收到的第一个字节是什么”，可以避开串口工具 text/hex
 *   模式切换带来的误判。命令后面可以带回车或换行，也可以不带；收到完整 AT+X 后
 *   本 demo 就会立即执行。串口界面只打印“执行了哪个 SE05x API、API 返回了什么”。
 */

#define UART_CMD_MAX_LEN 16U

static int normalize_command_char(int ch)
{
	if (ch >= 'A' && ch <= 'Z') {
		return ch + ('a' - 'A');
	}

	return ch;
}

static bool is_line_end(int ch)
{
	return ch == '\r' || ch == '\n';
}

static bool is_known_command(int cmd);

static const char *command_api_name(int cmd)
{
	switch (cmd) {
	case '0':
	case 'h':
		return "Show menu";
	case '1':
		return "Se05x_API_GetVersion";
	case '2':
		return "Se05x_API_GetExtVersion";
	case '3':
		return "Se05x_API_GetRandom(16)";
	case '4':
		return "Se05x_API_ReadObject(UNIQUE_ID)";
	case '5':
		return "Se05x_API_CheckObjectExists(UNIQUE_ID)";
	case '6':
		return "Se05x_API_CheckObjectExists(FEATURE)";
	case '7':
		return "Se05x_API_CheckObjectExists(PLATFORM_SCP)";
	case '8':
		return "Se05x_API_GetFreeMemory(PERSISTENT)";
	case '9':
		return "Se05x_API_GetFreeMemory(TRANSIENT_RESET)";
	case 'a':
		return "Se05x_API_GetFreeMemory(TRANSIENT_DESELECT)";
	case 'b':
		return "Se05x_API_ReadECCurveList";
	case 'c':
		return "Se05x_API_ReadCryptoObjectList";
	case 'd':
		return "Se05x_API_ReadState";
	case 'e':
		return "Se05x_API_ReadIDList";
	case 'q':
		return "Quit";
	default:
		return "Unknown command";
	}
}

static int parse_at_command(const char *line, size_t len)
{
	size_t start = 0;
	size_t end = len;

	while (start < end && line[start] == ' ') {
		++start;
	}
	while (end > start && line[end - 1U] == ' ') {
		--end;
	}

	if (end - start != 4U) {
		return -1;
	}

	const char a = line[start];
	const char t = line[start + 1U];
	if (!((a == 'A' || a == 'a') && (t == 'T' || t == 't') &&
	      line[start + 2U] == '+')) {
		return -1;
	}

	const int cmd = normalize_command_char(line[start + 3U]);
	if (!is_known_command(cmd)) {
		return -1;
	}

	return cmd;
}

static int read_uart_command(void)
{
	char line[UART_CMD_MAX_LEN] = { 0 };
	size_t len = 0;

	while (1) {
		int ch = console_getchar();

		if (is_line_end(ch)) {
			if (len == 0U) {
				continue;
			}

			return parse_at_command(line, len);
		}

		if ((ch == '\b' || ch == 0x7F) && len > 0U) {
			line[--len] = '\0';
			continue;
		}

		if (len >= sizeof(line) - 1U) {
			len = 0;
			return -1;
		}

		line[len++] = (char)ch;
		line[len] = '\0';

		const int cmd = parse_at_command(line, len);
		if (cmd >= 0) {
			return cmd;
		}
	}
}

static bool is_known_command(int cmd)
{
	switch (cmd) {
	case '0':
	case 'h':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'q':
		return true;
	default:
		return false;
	}
}

static void print_api_call(int cmd)
{
	if (is_known_command(cmd)) {
		const int shown = (cmd >= 'a' && cmd <= 'z') ? cmd - ('a' - 'A') : cmd;

		printk("CMD AT+%c -> CALL %s\n", shown, command_api_name(cmd));
		return;
	}

	printk("Unknown command. Use AT+0 or AT+H for help.\n");
}

static void print_hex_bytes(const char *label, const uint8_t *data, size_t data_len)
{
	const size_t shown = data_len < 16U ? data_len : 16U;

	printk("     %s len=%u hex=", label, (unsigned int)data_len);
	for (size_t i = 0; i < shown; ++i) {
		printk("%02X%s", data[i], i + 1U == shown ? "" : " ");
	}
	if (data_len > shown) {
		printk(" ...");
	}
	printk("\n");
}

static void print_menu(void)
{
	printk("\n");
	printk("========== SE05x Demo 00: UART safe API menu ==========\n");
	printk("AT+0 / AT+H : Show this menu\n");
	printk("AT+1        : Se05x_API_GetVersion\n");
	printk("AT+2        : Se05x_API_GetExtVersion\n");
	printk("AT+3        : Se05x_API_GetRandom(16)\n");
	printk("AT+4        : Se05x_API_ReadObject(UNIQUE_ID)\n");
	printk("AT+5        : Se05x_API_CheckObjectExists(UNIQUE_ID)\n");
	printk("AT+6        : Se05x_API_CheckObjectExists(FEATURE)\n");
	printk("AT+7        : Se05x_API_CheckObjectExists(PLATFORM_SCP)\n");
	printk("AT+8        : Se05x_API_GetFreeMemory(PERSISTENT)\n");
	printk("AT+9        : Se05x_API_GetFreeMemory(TRANSIENT_RESET)\n");
	printk("AT+A        : Se05x_API_GetFreeMemory(TRANSIENT_DESELECT)\n");
	printk("AT+B        : Se05x_API_ReadECCurveList\n");
	printk("AT+C        : Se05x_API_ReadCryptoObjectList\n");
	printk("AT+D        : Se05x_API_ReadState\n");
	printk("AT+E        : Se05x_API_ReadIDList (may return SKIP on some OEFs)\n");
	printk("AT+Q        : Quit Demo 00 and close the SE05x session\n");
	printk("Safety: this menu does not write NVM, create/delete objects, or change SE05x config.\n");
	printk("Input: send one AT command, for example AT+3. Line ending is optional.\n");
	printk("=======================================================\n");
}

static void print_result_sw(const char *name, smStatus_t sw)
{
	if (sw == SM_OK) {
		printk("OK   %s sw=0x%04" PRIX16 "\n", name, (uint16_t)sw);
	} else {
		printk("FAIL %s sw=0x%04" PRIX16 "\n", name, (uint16_t)sw);
	}
}

static void print_skip_sw(const char *name, smStatus_t sw)
{
	printk("SKIP %s sw=0x%04" PRIX16 "; this API may be disabled by the current applet/OEF.\n",
	       name, (uint16_t)sw);
}

static void cmd_get_version(pSe05xSession_t session)
{
	uint8_t version[7] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetVersion(session, version, &version_len);

	if (sw != SM_OK || version_len < sizeof(version)) {
		print_result_sw("GetVersion", sw);
		return;
	}

	const uint16_t applet_config = ((uint16_t)version[3] << 8) | version[4];

	printk("OK   GetVersion sw=0x%04" PRIX16 " len=%u\n",
	       (uint16_t)sw, (unsigned int)version_len);
	printk("     Applet version : %u.%u.%u\n", version[0], version[1], version[2]);
	printk("     Applet config  : 0x%04" PRIX16 "\n", applet_config);
	printk("     SecureBox      : %u.%u\n", version[5], version[6]);
	LOG_INF("UART_API GetVersion applet_config=0x%04" PRIX16, applet_config);
	se05x_demo_log_applet_features(applet_config);
}

static void cmd_get_ext_version(pSe05xSession_t session)
{
	uint8_t version[48] = { 0 };
	size_t version_len = sizeof(version);
	smStatus_t sw = Se05x_API_GetExtVersion(session, version, &version_len);

	print_result_sw("GetExtVersion", sw);
	if (sw == SM_OK) {
		print_hex_bytes("ExtVersion", version, version_len);
	}
}

static void cmd_get_random(pSe05xSession_t session)
{
	uint8_t random[16] = { 0 };
	size_t random_len = sizeof(random);
	smStatus_t sw = Se05x_API_GetRandom(session, sizeof(random), random, &random_len);

	if (sw == SM_OK && random_len == sizeof(random)) {
		printk("OK   GetRandom sw=0x%04" PRIX16 " len=%u\n",
		       (uint16_t)sw, (unsigned int)random_len);
		print_hex_bytes("Random", random, random_len);
	} else {
		print_result_sw("GetRandom", sw);
	}
}

static void cmd_read_unique_id(pSe05xSession_t session)
{
	uint8_t unique_id[SE050_MODULE_UNIQUE_ID_LEN] = { 0 };
	size_t unique_id_len = sizeof(unique_id);
	smStatus_t sw = Se05x_API_ReadObject(session, kSE05x_AppletResID_UNIQUE_ID,
					     0, 0, unique_id, &unique_id_len);

	print_result_sw("ReadObject(UNIQUE_ID)", sw);
	if (sw == SM_OK) {
		print_hex_bytes("UniqueID", unique_id, unique_id_len);
	}
}

static void cmd_check_object(pSe05xSession_t session, uint32_t object_id,
			     const char *name)
{
	SE05x_Result_t result = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(session, object_id, &result);

	if (sw == SM_OK) {
		printk("OK   %s sw=0x%04" PRIX16 " object_id=0x%08" PRIX32 " exists=%s\n",
		       name, (uint16_t)sw, object_id,
		       result == kSE05x_Result_SUCCESS ? "yes" : "no");
	} else {
		print_result_sw(name, sw);
	}
}

static void cmd_free_memory(pSe05xSession_t session, SE05x_MemoryType_t type,
			    const char *name)
{
	uint32_t free_mem = 0;
	smStatus_t sw = Se05x_API_GetFreeMemory(session, type, &free_mem);

	if (sw == SM_OK) {
		printk("OK   %s sw=0x%04" PRIX16 " free=%" PRIu32 " bytes\n",
		       name, (uint16_t)sw, free_mem);
	} else {
		print_result_sw(name, sw);
	}
}

static void cmd_read_curve_list(pSe05xSession_t session)
{
	uint8_t curve_list[128] = { 0 };
	size_t curve_list_len = sizeof(curve_list);
	smStatus_t sw = Se05x_API_ReadECCurveList(session, curve_list, &curve_list_len);

	if (sw == SM_OK) {
		printk("OK   ReadECCurveList sw=0x%04" PRIX16 " len=%u\n",
		       (uint16_t)sw, (unsigned int)curve_list_len);
		print_hex_bytes("CurveList", curve_list, curve_list_len);
	} else {
		print_skip_sw("ReadECCurveList", sw);
	}
}

static void cmd_read_crypto_object_list(pSe05xSession_t session)
{
	uint8_t list[128] = { 0 };
	size_t list_len = sizeof(list);
	smStatus_t sw = Se05x_API_ReadCryptoObjectList(session, list, &list_len);

	if (sw == SM_OK) {
		printk("OK   ReadCryptoObjectList sw=0x%04" PRIX16 " len=%u\n",
		       (uint16_t)sw, (unsigned int)list_len);
		print_hex_bytes("CryptoObjectList", list, list_len);
	} else {
		print_skip_sw("ReadCryptoObjectList", sw);
	}
}

static void cmd_read_state(pSe05xSession_t session)
{
	uint8_t state[128] = { 0 };
	size_t state_len = sizeof(state);
	smStatus_t sw = Se05x_API_ReadState(session, state, &state_len);

	if (sw == SM_OK) {
		printk("OK   ReadState sw=0x%04" PRIX16 " len=%u\n",
		       (uint16_t)sw, (unsigned int)state_len);
		print_hex_bytes("State", state, state_len);
	} else {
		print_skip_sw("ReadState", sw);
	}
}

static void cmd_read_id_list(pSe05xSession_t session)
{
	uint8_t more = 0;
	uint8_t id_list[128] = { 0 };
	size_t id_list_len = sizeof(id_list);
	smStatus_t sw = Se05x_API_ReadIDList(session, 0, 0xFF, &more, id_list,
					     &id_list_len);

	if (sw == SM_OK) {
		printk("OK   ReadIDList sw=0x%04" PRIX16 " len=%u more=0x%02X\n",
		       (uint16_t)sw, (unsigned int)id_list_len, more);
		print_hex_bytes("IDList", id_list, id_list_len);
	} else {
		print_skip_sw("ReadIDList", sw);
	}
}

static sss_status_t run_uart_safe_api(ex_sss_boot_ctx_t *boot_ctx)
{
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;
	int err = console_init();

	if (err != 0) {
		LOG_ERR("console_init failed: %d", err);
		return kStatus_SSS_Fail;
	}

	LOG_INF("UART_SAFE_API started: interactive safe API test, no NVM writes, no object creation");
	printk("\nDemo 00 started. Type an AT command on UART. Type AT+0 or AT+H for help, AT+Q to quit.\n");
	print_menu();

	while (1) {
		printk("\nse05x-safe-api> ");
		int cmd = read_uart_command();
		printk("\n");
		print_api_call(cmd);

		switch (cmd) {
		case '0':
		case 'h':
			print_menu();
			break;
		case '1':
			cmd_get_version(se_session);
			break;
		case '2':
			cmd_get_ext_version(se_session);
			break;
		case '3':
			cmd_get_random(se_session);
			break;
		case '4':
			cmd_read_unique_id(se_session);
			break;
		case '5':
			cmd_check_object(se_session, kSE05x_AppletResID_UNIQUE_ID,
					 "CheckObjectExists(UNIQUE_ID)");
			break;
		case '6':
			cmd_check_object(se_session, kSE05x_AppletResID_FEATURE,
					 "CheckObjectExists(FEATURE)");
			break;
		case '7':
			cmd_check_object(se_session, kSE05x_AppletResID_PLATFORM_SCP,
					 "CheckObjectExists(PLATFORM_SCP)");
			break;
		case '8':
			cmd_free_memory(se_session, kSE05x_MemoryType_PERSISTENT,
					"GetFreeMemory(PERSISTENT)");
			break;
		case '9':
			cmd_free_memory(se_session, kSE05x_MemoryType_TRANSIENT_RESET,
					"GetFreeMemory(TRANSIENT_RESET)");
			break;
		case 'a':
			cmd_free_memory(se_session, kSE05x_MemoryType_TRANSIENT_DESELECT,
					"GetFreeMemory(TRANSIENT_DESELECT)");
			break;
		case 'b':
			cmd_read_curve_list(se_session);
			break;
		case 'c':
			cmd_read_crypto_object_list(se_session);
			break;
		case 'd':
			cmd_read_state(se_session);
			break;
		case 'e':
			cmd_read_id_list(se_session);
			break;
		case 'q':
			printk("Quit Demo 00. main.c will close the SE05x session.\n");
			LOG_INF("UART_SAFE_API quit");
			return kStatus_SSS_Success;
		default:
			break;
		}
	}
}

const se05x_demo_t g_se05x_demo_uart_safe_api = {
	.id = SE05X_DEMO_UART_SAFE_API,
	.name = "uart_safe_api",
	.when_to_use = "Interactive UART menu for safe APDU testing during bring-up and debugging.",
	.flow = "Open SCP03, wait for a UART command, then call one safe read/query/random API.",
	.expected_output = "Each command prints OK/FAIL/SKIP, status word, length, and preview; q quits.",
	.se_features = "Safe APDUs inside SCP03: version, random, UniqueID, object checks, memory, lists, state.",
	.run = run_uart_safe_api,
};
