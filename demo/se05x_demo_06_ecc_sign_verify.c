#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "fsl_sss_se05x_types.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_demo.h"

LOG_MODULE_REGISTER(se05x_demo_ecc, LOG_LEVEL_INF);

/*
 * 06 号业务 demo：ECC_SIGN_VERIFY，SE 内 ECC 私钥签名和公钥验签。
 *
 * 真实业务含义：
 *   产品注册、云端绑定、TLS 客户端认证、设备挑战应答等场景，最终都会落到
 *   一个核心动作：私钥留在 SE05x 内部，外部只把 challenge/hash 交给 SE05x
 *   签名，云端或主机用证书里的公钥验签。
 *
 * 当前 demo 的边界：
 *   - 会写 SE05x persistent NVM。
 *   - 写入对象：SE05X_DEMO_OBJECT_ID_ECC_KEY，也就是 0xEF060001。
 *   - 写入内容：NXP 示例 P-256 demo 私钥，只用于开发验证，不能用于生产。
 *   - 如果对象已经存在，本 demo 不覆盖，只复用已有对象并继续签名验签。
 *   - 公钥对象使用 transient object，关闭 session 后消失，不占 persistent NVM。
 *
 * 执行流程：
 *   1. CheckObjectExists(0xEF060001)：确认 demo 私钥对象是否已存在。
 *   2. 不存在时：allocate persistent handle，并用 sss_key_store_set_key() 写入 demo 私钥。
 *   3. 存在时：sss_key_object_get_handle() 获取已有对象，绝不覆盖。
 *   4. 导入匹配的 demo 公钥到 transient object。
 *   5. 用 SE 内私钥对 32 字节 challenge digest 做 ECDSA 签名。
 *   6. 用 demo 公钥验签，证明 key/cert 业务链路可以成立。
 *
 * 期望串口输出：
 *   - 能看到 object_id=0xEF060001。
 *   - 首次运行看到 created=yes；再次运行看到 created=no。
 *   - 能看到 signature preview。
 *   - 最后应看到 “ECC_SIGN_VERIFY 汇总：... fail=0”。
 */

/* NXP 示例 P-256 DER key pair：包含 demo 私钥和对应公钥，只能用于开发验证。 */
static const uint8_t k_demo_ec_key_pair_der[] = {
	0x30, 0x81, 0x87, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86,
	0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D,
	0x03, 0x01, 0x07, 0x04, 0x6D, 0x30, 0x6B, 0x02, 0x01, 0x01, 0x04, 0x20,
	0x78, 0xE5, 0x20, 0x6A, 0x08, 0xED, 0xD2, 0x52, 0x36, 0x33, 0x8A, 0x24,
	0x84, 0xE4, 0x2F, 0x1F, 0x7D, 0x1F, 0x6D, 0x94, 0x37, 0xA9, 0x95, 0x86,
	0xDA, 0xFC, 0xD2, 0x23, 0x6F, 0xA2, 0x87, 0x35, 0xA1, 0x44, 0x03, 0x42,
	0x00, 0x04, 0xED, 0xA7, 0xE9, 0x0B, 0xF9, 0x20, 0xCF, 0xFB, 0x9D, 0xF6,
	0xDB, 0xCE, 0xF7, 0x20, 0xE1, 0x23, 0x8B, 0x3C, 0xEE, 0x84, 0x86, 0xD2,
	0x50, 0xE4, 0xDF, 0x30, 0x11, 0x50, 0x1A, 0x15, 0x08, 0xA6, 0x2E, 0xD7,
	0x49, 0x52, 0x78, 0x63, 0x6E, 0x61, 0xE8, 0x5F, 0xED, 0xB0, 0x6D, 0x87,
	0x92, 0x0A, 0x04, 0x19, 0x14, 0xFE, 0x76, 0x63, 0x55, 0xDF, 0xBD, 0x68,
	0x61, 0x59, 0x31, 0x8E, 0x68, 0x7C
};

/* 和上面 demo 私钥匹配的 DER 公钥，用 transient object 做验签，不写 persistent NVM。 */
static const uint8_t k_demo_ec_public_key_der[] = {
	0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02,
	0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03,
	0x42, 0x00, 0x04, 0xED, 0xA7, 0xE9, 0x0B, 0xF9, 0x20, 0xCF, 0xFB, 0x9D,
	0xF6, 0xDB, 0xCE, 0xF7, 0x20, 0xE1, 0x23, 0x8B, 0x3C, 0xEE, 0x84, 0x86,
	0xD2, 0x50, 0xE4, 0xDF, 0x30, 0x11, 0x50, 0x1A, 0x15, 0x08, 0xA6, 0x2E,
	0xD7, 0x49, 0x52, 0x78, 0x63, 0x6E, 0x61, 0xE8, 0x5F, 0xED, 0xB0, 0x6D,
	0x87, 0x92, 0x0A, 0x04, 0x19, 0x14, 0xFE, 0x76, 0x63, 0x55, 0xDF, 0xBD,
	0x68, 0x61, 0x59, 0x31, 0x8E, 0x68, 0x7C
};

/* 模拟云端 challenge 或 TLS transcript hash 的 32 字节 digest。 */
static uint8_t k_demo_digest[32] = {
	0x54, 0x20, 0x06, 0x00, 0x53, 0x45, 0x30, 0x35, 0x78, 0x20, 0x64,
	0x65, 0x6D, 0x6F, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x20, 0x63, 0x68,
	0x61, 0x6C, 0x6C, 0x65, 0x6E, 0x67, 0x65, 0x20, 0x30, 0x36
};

static smStatus_t check_object_exists(pSe05xSession_t session, uint32_t object_id, bool *exists)
{
	SE05x_Result_t result = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(session, object_id, &result);

	if (sw == SM_OK) {
		*exists = (result == kSE05x_Result_SUCCESS);
	}

	return sw;
}

static void prepare_demo_key(se05x_demo_stats_t *stats, ex_sss_boot_ctx_t *boot_ctx,
			     pSe05xSession_t se_session, sss_object_t *key_pair)
{
	bool exists = false;
	smStatus_t sw = check_object_exists(se_session, SE05X_DEMO_OBJECT_ID_ECC_KEY, &exists);
	sss_status_t status;

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "CheckObjectExists(DEMO_ECC_KEY)", sw);
		return;
	}

	LOG_INF("ECC key object_id=0x%08" PRIX32 " exists=%s",
		(uint32_t)SE05X_DEMO_OBJECT_ID_ECC_KEY, exists ? "yes" : "no");

	status = sss_key_object_init(key_pair, &boot_ctx->ks);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "sss_key_object_init(ECC_KEY)", status);
		return;
	}

	if (exists) {
		status = sss_key_object_get_handle(key_pair, SE05X_DEMO_OBJECT_ID_ECC_KEY);
		if (status == kStatus_SSS_Success) {
			se05x_demo_mark_pass(stats, "GetHandle(DEMO_ECC_KEY)");
		} else {
			se05x_demo_mark_fail_status(stats, "GetHandle(DEMO_ECC_KEY)", status);
		}
		return;
	}

	status = sss_key_object_allocate_handle(key_pair, SE05X_DEMO_OBJECT_ID_ECC_KEY,
					       kSSS_KeyPart_Pair,
					       kSSS_CipherType_EC_NIST_P,
					       sizeof(k_demo_ec_key_pair_der),
					       kKeyObject_Mode_Persistent);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AllocateHandle(DEMO_ECC_KEY)", status);
		return;
	}

	status = sss_key_store_set_key(&boot_ctx->ks, key_pair, k_demo_ec_key_pair_der,
				       sizeof(k_demo_ec_key_pair_der),
				       SE05X_DEMO_ECC_KEY_BITS, NULL, 0);
	if (status == kStatus_SSS_Success) {
		LOG_INF("ECC persistent key created=yes object_id=0x%08" PRIX32,
			(uint32_t)SE05X_DEMO_OBJECT_ID_ECC_KEY);
		se05x_demo_mark_pass(stats, "SetKey(DEMO_ECC_KEY)");
	} else {
		se05x_demo_mark_fail_status(stats, "SetKey(DEMO_ECC_KEY)", status);
	}
}

static void prepare_demo_public_key(se05x_demo_stats_t *stats, ex_sss_boot_ctx_t *boot_ctx,
				    sss_object_t *public_key)
{
	sss_status_t status = sss_key_object_init(public_key, &boot_ctx->ks);

	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "sss_key_object_init(DEMO_ECC_PUB)", status);
		return;
	}

	status = sss_key_object_allocate_handle(public_key, SE05X_DEMO_OBJECT_ID_ECC_PUB,
					       kSSS_KeyPart_Public,
					       kSSS_CipherType_EC_NIST_P,
					       sizeof(k_demo_ec_public_key_der),
					       kKeyObject_Mode_Transient);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AllocateHandle(DEMO_ECC_PUB)", status);
		return;
	}

	status = sss_key_store_set_key(&boot_ctx->ks, public_key, k_demo_ec_public_key_der,
				       sizeof(k_demo_ec_public_key_der),
				       SE05X_DEMO_ECC_KEY_BITS, NULL, 0);
	if (status == kStatus_SSS_Success) {
		se05x_demo_mark_pass(stats, "SetKey(DEMO_ECC_PUB_TRANSIENT)");
	} else {
		se05x_demo_mark_fail_status(stats, "SetKey(DEMO_ECC_PUB_TRANSIENT)", status);
	}
}

static void sign_and_verify(se05x_demo_stats_t *stats, ex_sss_boot_ctx_t *boot_ctx,
			    sss_object_t *key_pair, sss_object_t *public_key)
{
	uint8_t signature[128] = { 0 };
	size_t signature_len = sizeof(signature);
	sss_asymmetric_t sign_ctx = { 0 };
	sss_asymmetric_t verify_ctx = { 0 };
	sss_status_t status;

	status = sss_asymmetric_context_init(&sign_ctx, &boot_ctx->session, key_pair,
					     kAlgorithm_SSS_SHA256, kMode_SSS_Sign);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(Sign)", status);
		goto cleanup;
	}

	status = sss_asymmetric_sign_digest(&sign_ctx, k_demo_digest, sizeof(k_demo_digest),
					    signature, &signature_len);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "SignDigest(DEMO_CHALLENGE)", status);
		goto cleanup;
	}

	se05x_demo_log_hex_preview("Signature", signature, signature_len);
	se05x_demo_mark_pass(stats, "SignDigest(DEMO_CHALLENGE)");

	status = sss_asymmetric_context_init(&verify_ctx, &boot_ctx->session, public_key,
					     kAlgorithm_SSS_SHA256, kMode_SSS_Verify);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(Verify)", status);
		goto cleanup;
	}

	status = sss_asymmetric_verify_digest(&verify_ctx, k_demo_digest, sizeof(k_demo_digest),
					      signature, signature_len);
	if (status == kStatus_SSS_Success) {
		se05x_demo_mark_pass(stats, "VerifyDigest(DEMO_CHALLENGE)");
	} else {
		se05x_demo_mark_fail_status(stats, "VerifyDigest(DEMO_CHALLENGE)", status);
	}

cleanup:
	if (sign_ctx.session != NULL) {
		sss_asymmetric_context_free(&sign_ctx);
	}
	if (verify_ctx.session != NULL) {
		sss_asymmetric_context_free(&verify_ctx);
	}
}

static sss_status_t run_ecc_sign_verify(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;
	sss_object_t key_pair = { 0 };
	sss_object_t public_key = { 0 };

	se05x_demo_stats_init(&stats, "ECC_SIGN_VERIFY");
	LOG_INF("ECC_SIGN_VERIFY started: write/reuse demo P-256 private key and sign/verify challenge");
	LOG_INF("Persistent NVM write possible: object_id=0x%08" PRIX32
		"; existing object will not be overwritten",
		(uint32_t)SE05X_DEMO_OBJECT_ID_ECC_KEY);

	prepare_demo_key(&stats, boot_ctx, se_session, &key_pair);
	if (stats.fail == 0U) {
		prepare_demo_public_key(&stats, boot_ctx, &public_key);
	}
	if (stats.fail == 0U) {
		sign_and_verify(&stats, boot_ctx, &key_pair, &public_key);
	}

	sss_key_object_free(&key_pair);
	sss_key_object_free(&public_key);
	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_ecc_sign_verify = {
	.id = SE05X_DEMO_ECC_SIGN_VERIFY,
	.name = "ecc_sign_verify",
	.when_to_use = "需要验证 SE 内应用私钥可用于设备挑战签名、云端验签或 TLS 身份前置能力时使用。",
	.flow = "检查 demo ECC object，必要时写入 persistent 私钥，导入 transient 公钥，然后签名并验签。",
	.expected_output = "看到 object_id、created/reused 状态、signature preview，最终 fail=0。",
	.se_features = "SSS key object、persistent EC key、transient public key、ECDSA sign/verify。",
	.run = run_ecc_sign_verify,
};
