#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/console/console.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "fsl_sss_se05x_types.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_demo.h"
#include "se05x_ecc_curves.h"
#include "se05x_enums.h"

LOG_MODULE_REGISTER(se05x_demo_wallet_curve, LOG_LEVEL_INF);

#define WALLET_UART_CMD_MAX_LEN 16U

/*
 * Demo 09: wallet_curve_check.
 *
 * 中文说明：
 * 这个 demo 用来研究当前 SE05x/SE052_B501 是否可以支持 BTC/ETH 需要的
 * secp256k1 曲线。它不是完整硬件钱包 demo，也不会解析交易、派生地址或广播交易。
 *
 * NVM 边界：
 * - 会在 secp256k1 当前 NOT_SET 时调用 Se05x_API_CreateCurve_secp256k1()。
 * - CreateCurve + SetECCurveParam 会写 SE05x persistent NVM。
 * - 如果 secp256k1 已经 SET，本 demo 不会重复创建曲线。
 * - 测试 key 使用 transient object，不写 persistent 私钥内容。
 * - SE05x transient object 的对象属性/ID 可能仍会保留，所以本 demo 只会自动清理
 *   自己保留的测试 object_id=0xEF090001，不会删除任何业务对象。
 * - 不删除曲线，不 DeleteAll，不循环重试。
 *
 * 为什么要单独做这个 demo：
 * Demo 00 只能安全只读查询，已经证明当前 Secp256k1 是 NOT_SET。Demo 09 则是
 * 明确写 NVM 的研究型 demo，用来回答“这颗 SE 当前配置是否允许启用 secp256k1，
 * 并是否能进一步生成 transient secp256k1 key 做 ECDSA sign/verify”。
 *
 * 判断含义：
 * - 如果曲线创建失败，说明当前 OEF/权限/配置不允许应用侧启用 secp256k1。
 * - 如果曲线 SET 但 key/sign 失败，说明钱包还不能走 SE 原生 BTC/ETH 私钥签名。
 * - 如果曲线 SET、transient key generate、sign/verify 都成功，说明 SE 原生
 *   secp256k1 ECDSA 基础链路成立。BTC/ETH 仍需补交易解析、hash、地址、公钥导出、
 *   DER/r|s 转换、low-S、Ethereum recovery id 等钱包协议层。
 *
 * 串口交互：
 *   为了现场反复验证，本 demo 启动后会自动跑一次完整测试，然后停在 AT 文本菜单。
 *   输入 AT+R 可以再次完整运行；AT+C 只检查曲线；AT+S 只生成 transient key 并输出
 *   公钥、digest、signature。串口输出使用英文 ASCII，避免串口工具中文编码问题。
 */

static uint8_t k_wallet_digest[32] = {
	0x57, 0x41, 0x4c, 0x4c, 0x45, 0x54, 0x2d, 0x53,
	0x45, 0x43, 0x50, 0x32, 0x35, 0x36, 0x4b, 0x31,
	0x2d, 0x44, 0x45, 0x4d, 0x4f, 0x2d, 0x30, 0x39,
	0x2d, 0x44, 0x49, 0x47, 0x45, 0x53, 0x54, 0x21,
};

static int wallet_normalize_command_char(int ch)
{
	if (ch >= 'A' && ch <= 'Z') {
		return ch + ('a' - 'A');
	}

	return ch;
}

static bool wallet_is_line_end(int ch)
{
	return ch == '\r' || ch == '\n';
}

static bool wallet_is_known_command(int cmd)
{
	return cmd == 'h' || cmd == 'r' || cmd == 'c' || cmd == 's' || cmd == 'q';
}

static int wallet_parse_at_command(const char *line, size_t len)
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

	const int cmd = wallet_normalize_command_char(line[start + 3U]);
	if (!wallet_is_known_command(cmd)) {
		return -1;
	}

	return cmd;
}

static int wallet_read_uart_command(void)
{
	char line[WALLET_UART_CMD_MAX_LEN] = { 0 };
	size_t len = 0;

	while (1) {
		int ch = console_getchar();

		if (wallet_is_line_end(ch)) {
			if (len == 0U) {
				continue;
			}

			return wallet_parse_at_command(line, len);
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

		const int cmd = wallet_parse_at_command(line, len);
		if (cmd >= 0) {
			return cmd;
		}
	}
}

static void wallet_print_menu(void)
{
	printk("\n========== SE05x Demo 09: Wallet curve check ==========\n");
	printk("AT+H : Show this menu\n");
	printk("AT+R : Run full wallet probe: curve + transient key + public key + sign/verify\n");
	printk("AT+C : Check secp256k1 curve status only\n");
	printk("AT+S : Generate transient secp256k1 key, print public key, sign and verify\n");
	printk("AT+Q : Quit Demo 09 and close SE05x session\n");
	printk("Private key policy: never exported, never printed. Only public key/signature are printed.\n");
	printk("NVM policy: AT+R may create secp256k1 curve once if it is NOT_SET; AT+S cleans only 0xEF090001.\n");
	printk("=======================================================\n\n");
}

static void wallet_print_hex_block(const char *label, const uint8_t *data, size_t data_len)
{
	printk("%s len=%u hex=", label, (unsigned int)data_len);
	for (size_t i = 0; i < data_len; i++) {
		if (i > 0U && (i % 16U) == 0U) {
			printk("\n  ");
		}
		printk("%02X", data[i]);
		if ((i + 1U) < data_len) {
			printk(" ");
		}
	}
	printk("\n");
}

static const uint8_t *wallet_find_uncompressed_ec_point(const uint8_t *key,
							size_t key_len,
							size_t *point_len)
{
	const uint8_t *point = NULL;

	/*
	 * SSS get_key() returns the EC public key with a DER header for SE05x.
	 * The wallet-usable secp256k1 public key is the uncompressed point:
	 * 0x04 || X(32 bytes) || Y(32 bytes).
	 */
	for (size_t i = 0; (i + 65U) <= key_len; i++) {
		if (key[i] == 0x04U) {
			point = &key[i];
		}
	}

	if (point != NULL) {
		*point_len = 65U;
	}

	return point;
}

static bool dump_wallet_public_key(se05x_demo_stats_t *stats,
				   ex_sss_boot_ctx_t *boot_ctx,
				   sss_object_t *wallet_key)
{
	uint8_t public_key[160] = { 0 };
	size_t public_key_len = sizeof(public_key);
	size_t public_key_bits = 0U;
	size_t point_len = 0U;
	const uint8_t *point;
	sss_status_t status;

	/*
	 * 读取 key pair object 时，SE05x/APDU 只返回公钥部分。
	 * 私钥不会导出；如果尝试读纯 private key/symmetric key，SE05x 会拒绝。
	 */
	status = sss_key_store_get_key(&boot_ctx->ks, wallet_key, public_key,
				       &public_key_len, &public_key_bits);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "GetPublicKey(SECP256K1)", status);
		return false;
	}

	printk("\nWallet verification material exported from Demo09:\n");
	wallet_print_hex_block("PUBLIC_KEY_DER", public_key, public_key_len);

	point = wallet_find_uncompressed_ec_point(public_key, public_key_len, &point_len);
	if (point == NULL) {
		se05x_demo_mark_fail_sw(stats, "ExtractPublicKey04XY", SM_NOT_OK);
		return false;
	}

	wallet_print_hex_block("PUBLIC_KEY_UNCOMPRESSED_04XY", point, point_len);
	wallet_print_hex_block("PUBLIC_KEY_X", &point[1], 32U);
	wallet_print_hex_block("PUBLIC_KEY_Y", &point[33], 32U);
	se05x_demo_mark_pass(stats, "GetPublicKey(SECP256K1)");

	return true;
}

static bool curve_status_is_set(const uint8_t *curve_list, size_t curve_list_len,
				SE05x_ECCurve_t curve_id)
{
	if (curve_id == kSE05x_ECCurve_NA || (size_t)(curve_id - 1U) >= curve_list_len) {
		return false;
	}

	return curve_list[curve_id - 1U] == kSE05x_SetIndicator_SET;
}

static smStatus_t read_secp256k1_status(pSe05xSession_t session, bool *is_set)
{
	uint8_t curve_list[32] = { 0 };
	size_t curve_list_len = sizeof(curve_list);
	smStatus_t sw = Se05x_API_ReadECCurveList(session, curve_list, &curve_list_len);

	if (sw == SM_OK) {
		*is_set = curve_status_is_set(curve_list, curve_list_len,
					      kSE05x_ECCurve_Secp256k1);
		LOG_INF("ReadECCurveList len=%u Secp256k1=%s", (unsigned int)curve_list_len,
			*is_set ? "SET" : "NOT_SET");
		se05x_demo_log_hex_preview("CurveList", curve_list, curve_list_len);
	}

	return sw;
}

static void ensure_secp256k1_curve(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	bool is_set = false;
	smStatus_t sw = read_secp256k1_status(session, &is_set);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "ReadECCurveList(before)", sw);
		return;
	}

	if (is_set) {
		se05x_demo_mark_pass(stats, "Secp256k1AlreadySET");
		return;
	}

	LOG_WRN("Secp256k1 is NOT_SET; creating curve will write SE05x persistent NVM once");
	sw = Se05x_API_CreateCurve_secp256k1(session, kSE05x_ECCurve_Secp256k1);
	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "CreateCurve_secp256k1", sw);
		return;
	}
	se05x_demo_mark_pass(stats, "CreateCurve_secp256k1");

	is_set = false;
	sw = read_secp256k1_status(session, &is_set);
	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "ReadECCurveList(after)", sw);
		return;
	}

	if (is_set) {
		se05x_demo_mark_pass(stats, "Secp256k1SETAfterCreate");
	} else {
		se05x_demo_mark_fail_sw(stats, "Secp256k1StillNOT_SET", SM_NOT_OK);
	}
}

static bool curve_is_ready(se05x_demo_stats_t *stats, pSe05xSession_t se_session,
			   const char *step_name)
{
	bool is_set = false;
	smStatus_t sw = read_secp256k1_status(se_session, &is_set);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, step_name, sw);
		return false;
	}

	if (!is_set) {
		se05x_demo_mark_fail_sw(stats, "Secp256k1NOT_SET", SM_NOT_OK);
		return false;
	}

	se05x_demo_mark_pass(stats, "Secp256k1SET");
	return true;
}

static bool delete_wallet_test_object_if_exists(se05x_demo_stats_t *stats,
						pSe05xSession_t se_session,
						const char *phase)
{
	SE05x_Result_t exists = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(se_session,
						    SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1,
						    &exists);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "CheckObjectExists(WALLET_TEST_KEY)", sw);
		return false;
	}

	if (exists != kSE05x_Result_SUCCESS) {
		if (phase != NULL) {
			LOG_INF("%s: wallet test object_id=0x%08" PRIX32 " is free", phase,
				(uint32_t)SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1);
		}
		return true;
	}

	LOG_WRN("%s: deleting reserved Demo09 test object_id=0x%08" PRIX32
		"; DeleteSecureObject writes SE05x NVM metadata",
		phase != NULL ? phase : "cleanup",
		(uint32_t)SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1);

	sw = Se05x_API_DeleteSecureObject(se_session, SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1);
	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "DeleteSecureObject(WALLET_TEST_KEY)", sw);
		return false;
	}

	se05x_demo_mark_pass(stats, "DeleteStaleWalletTestObject");
	return true;
}

static void generate_transient_key_and_sign(se05x_demo_stats_t *stats,
					    ex_sss_boot_ctx_t *boot_ctx,
					    pSe05xSession_t se_session)
{
	sss_object_t wallet_key = { 0 };
	sss_asymmetric_t sign_ctx = { 0 };
	sss_asymmetric_t verify_ctx = { 0 };
	uint8_t signature[128] = { 0 };
	size_t signature_len = sizeof(signature);
	sss_status_t status;

	if (!delete_wallet_test_object_if_exists(stats, se_session, "before generate")) {
		return;
	}
	se05x_demo_mark_pass(stats, "WalletTestObjectIdReady");

	status = sss_key_object_init(&wallet_key, &boot_ctx->ks);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "sss_key_object_init(SECP256K1)", status);
		return;
	}

	status = sss_key_object_allocate_handle(&wallet_key, SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1,
					       kSSS_KeyPart_Pair,
					       kSSS_CipherType_EC_NIST_K,
					       32, kKeyObject_Mode_Transient);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AllocateHandle(SECP256K1_TRANSIENT)", status);
		goto cleanup;
	}
	se05x_demo_mark_pass(stats, "AllocateHandle(SECP256K1_TRANSIENT)");

	status = sss_key_store_generate_key(&boot_ctx->ks, &wallet_key, 256, NULL);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "GenerateKey(SECP256K1_TRANSIENT)", status);
		goto cleanup;
	}
	se05x_demo_mark_pass(stats, "GenerateKey(SECP256K1_TRANSIENT)");

	if (!dump_wallet_public_key(stats, boot_ctx, &wallet_key)) {
		goto cleanup;
	}

	status = sss_asymmetric_context_init(&sign_ctx, &boot_ctx->session, &wallet_key,
					     kAlgorithm_SSS_SHA256, kMode_SSS_Sign);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(SignSECP256K1)", status);
		goto cleanup;
	}

	status = sss_asymmetric_sign_digest(&sign_ctx, k_wallet_digest, sizeof(k_wallet_digest),
					    signature, &signature_len);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "SignDigest(SECP256K1)", status);
		goto cleanup;
	}
	wallet_print_hex_block("DIGEST_SHA256_INPUT", k_wallet_digest, sizeof(k_wallet_digest));
	wallet_print_hex_block("SIGNATURE_DER", signature, signature_len);
	se05x_demo_log_hex_preview("Secp256k1SignaturePreview", signature, signature_len);
	se05x_demo_mark_pass(stats, "SignDigest(SECP256K1)");

	status = sss_asymmetric_context_init(&verify_ctx, &boot_ctx->session, &wallet_key,
					     kAlgorithm_SSS_SHA256, kMode_SSS_Verify);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(VerifySECP256K1)", status);
		goto cleanup;
	}

	status = sss_asymmetric_verify_digest(&verify_ctx, k_wallet_digest, sizeof(k_wallet_digest),
					      signature, signature_len);
	if (status == kStatus_SSS_Success) {
		se05x_demo_mark_pass(stats, "VerifyDigest(SECP256K1)");
	} else {
		se05x_demo_mark_fail_status(stats, "VerifyDigest(SECP256K1)", status);
	}

cleanup:
	if (sign_ctx.session != NULL) {
		sss_asymmetric_context_free(&sign_ctx);
	}
	if (verify_ctx.session != NULL) {
		sss_asymmetric_context_free(&verify_ctx);
	}
	sss_key_object_free(&wallet_key);
	(void)delete_wallet_test_object_if_exists(stats, se_session, "after test");
}

static sss_status_t run_wallet_probe_once(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "WALLET_CURVE_CHECK");
	LOG_INF("WALLET_CURVE_CHECK started: secp256k1 curve enable + transient sign/verify");
	LOG_WRN("This demo may write SE05x NVM once if secp256k1 is NOT_SET");
	LOG_INF("Transient test key object_id=0x%08" PRIX32 " is reserved for Demo09 cleanup",
		(uint32_t)SE05X_DEMO_OBJECT_ID_WALLET_SECP256K1);

	ensure_secp256k1_curve(&stats, se_session);
	if (stats.fail == 0U) {
		generate_transient_key_and_sign(&stats, boot_ctx, se_session);
	}

	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

static sss_status_t run_wallet_curve_only(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "WALLET_CURVE_ONLY");
	(void)curve_is_ready(&stats, se_session, "ReadECCurveList");
	se05x_demo_log_summary(&stats);

	return se05x_demo_stats_result(&stats);
}

static sss_status_t run_wallet_sign_only(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "WALLET_SIGN_ONLY");
	if (curve_is_ready(&stats, se_session, "ReadECCurveList")) {
		generate_transient_key_and_sign(&stats, boot_ctx, se_session);
	}

	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

static sss_status_t run_wallet_curve_check(ex_sss_boot_ctx_t *boot_ctx)
{
	sss_status_t last_status;

	last_status = run_wallet_probe_once(boot_ctx);
	wallet_print_menu();

	while (1) {
		printk("se05x-wallet-demo> ");
		const int cmd = wallet_read_uart_command();

		switch (cmd) {
		case 'h':
			printk("\nCMD AT+H -> Show menu\n");
			wallet_print_menu();
			break;
		case 'r':
			printk("\nCMD AT+R -> Run full wallet probe\n");
			last_status = run_wallet_probe_once(boot_ctx);
			break;
		case 'c':
			printk("\nCMD AT+C -> Check secp256k1 curve status\n");
			last_status = run_wallet_curve_only(boot_ctx);
			break;
		case 's':
			printk("\nCMD AT+S -> Generate transient key, print public key, sign and verify\n");
			last_status = run_wallet_sign_only(boot_ctx);
			break;
		case 'q':
			printk("\nCMD AT+Q -> Quit Demo 09\n");
			return last_status;
		default:
			printk("\nUnknown command. Type AT+H for help.\n");
			break;
		}
	}
}

const se05x_demo_t g_se05x_demo_wallet_curve_check = {
	.id = SE05X_DEMO_WALLET_CURVE_CHECK,
	.name = "wallet_curve_check",
	.when_to_use = "Research whether this SE05x can enable secp256k1 for BTC/ETH-style wallets.",
	.flow = "Auto-run once, then accept AT commands for curve check and transient sign/verify.",
	.expected_output = "Secp256k1 SET, public key, digest, DER signature, verify PASS, final fail=0.",
	.se_features = "NVM curve creation, EC_NIST_K/secp256k1 transient key, ECDSA sign/verify.",
	.run = run_wallet_curve_check,
};
