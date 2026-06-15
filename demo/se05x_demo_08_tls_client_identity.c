#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include "fsl_sss_se05x_types.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_demo.h"

LOG_MODULE_REGISTER(se05x_demo_tls_id, LOG_LEVEL_INF);

/*
 * 08 号业务 demo：TLS_CLIENT_IDENTITY，TLS 客户端身份材料检查和挑战签名。
 *
 * 真实业务含义：
 *   设备做 mTLS/TLS client authentication 时，通常需要两类材料：
 *     - SE 内不可导出的私钥，用来签 TLS handshake 或云端 challenge。
 *     - 设备证书/证书链，用来发给服务器，让服务器找到对应公钥和身份。
 *
 * 当前 demo 的边界：
 *   - 本 demo 不新写 NVM。
 *   - 它依赖 06 号 demo 创建/复用的 ECC 私钥对象 0xEF060001。
 *   - 它依赖 07 号 demo 创建/复用的证书对象 0xEF070001。
 *   - 如果对象不存在，本 demo 会 FAIL，并提示先运行 06 和 07。
 *   - 当前工程还没有接入 Zephyr TLS socket；这里演示 TLS 身份最关键的
 *     SE05x 调用骨架：读取证书 + 用 SE 内私钥签名 handshake digest。
 *
 * 执行流程：
 *   1. CheckObjectExists(0xEF060001)：确认 TLS private key 存在。
 *   2. CheckObjectExists(0xEF070001)：确认 TLS certificate 存在。
 *   3. sss_key_store_get_key() 回读证书，模拟 TLS 发送 Certificate 消息。
 *   4. sss_asymmetric_sign_digest() 用 SE 内私钥签名 32 字节 handshake digest。
 *   5. 打印签名 preview，模拟 TLS CertificateVerify 的签名产物。
 *
 * 期望串口输出：
 *   - 能看到 key/cert 两个 object 都存在。
 *   - 能看到 certificate preview。
 *   - 能看到 TLS handshake signature preview。
 *   - 最后应看到 “TLS_CLIENT_IDENTITY 汇总：... fail=0”。
 */

static uint8_t k_tls_handshake_digest[32] = {
	0x54, 0x4C, 0x53, 0x20, 0x63, 0x6C, 0x69, 0x65, 0x6E, 0x74, 0x20,
	0x69, 0x64, 0x65, 0x6E, 0x74, 0x69, 0x74, 0x79, 0x20, 0x64, 0x69,
	0x67, 0x65, 0x73, 0x74, 0x20, 0x30, 0x38, 0x00, 0x00, 0x01
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

static void require_object(se05x_demo_stats_t *stats, pSe05xSession_t session,
			   uint32_t object_id, const char *name)
{
	bool exists = false;
	smStatus_t sw = check_object_exists(session, object_id, &exists);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, name, sw);
		return;
	}

	LOG_INF("%s object_id=0x%08" PRIX32 " exists=%s", name, object_id,
		exists ? "yes" : "no");
	if (exists) {
		se05x_demo_mark_pass(stats, name);
	} else {
		LOG_ERR("%s missing: run Demo 06/07 first", name);
		se05x_demo_mark_fail_status(stats, name, kStatus_SSS_Fail);
	}
}

static void load_key_handle(se05x_demo_stats_t *stats, ex_sss_boot_ctx_t *boot_ctx,
			    sss_object_t *key_pair)
{
	sss_status_t status = sss_key_object_init(key_pair, &boot_ctx->ks);

	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "sss_key_object_init(TLS_KEY)", status);
		return;
	}

	status = sss_key_object_get_handle(key_pair, SE05X_DEMO_OBJECT_ID_ECC_KEY);
	if (status == kStatus_SSS_Success) {
		se05x_demo_mark_pass(stats, "GetHandle(TLS_KEY)");
	} else {
		se05x_demo_mark_fail_status(stats, "GetHandle(TLS_KEY)", status);
	}
}

static void read_tls_certificate(se05x_demo_stats_t *stats, ex_sss_boot_ctx_t *boot_ctx)
{
	sss_object_t cert_obj = { 0 };
	uint8_t cert[512] = { 0 };
	size_t cert_len = sizeof(cert);
	size_t cert_bit_len = 0;
	sss_status_t status = sss_key_object_init(&cert_obj, &boot_ctx->ks);

	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "sss_key_object_init(TLS_CERT)", status);
		return;
	}

	status = sss_key_object_get_handle(&cert_obj, SE05X_DEMO_OBJECT_ID_CERT);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "GetHandle(TLS_CERT)", status);
		goto cleanup;
	}

	status = sss_key_store_get_key(&boot_ctx->ks, &cert_obj, cert, &cert_len,
				       &cert_bit_len);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "GetKey(TLS_CERT)", status);
		goto cleanup;
	}

	LOG_INF("TLS certificate ready len=%u bit_len=%u", (unsigned int)cert_len,
		(unsigned int)cert_bit_len);
	se05x_demo_log_hex_preview("TLSCertificate", cert, cert_len);
	se05x_demo_mark_pass(stats, "GetKey(TLS_CERT)");

cleanup:
	sss_key_object_free(&cert_obj);
}

static void sign_tls_handshake_digest(se05x_demo_stats_t *stats,
				      ex_sss_boot_ctx_t *boot_ctx,
				      sss_object_t *key_pair)
{
	uint8_t signature[128] = { 0 };
	size_t signature_len = sizeof(signature);
	sss_asymmetric_t sign_ctx = { 0 };
	sss_status_t status = sss_asymmetric_context_init(&sign_ctx, &boot_ctx->session,
							  key_pair,
							  kAlgorithm_SSS_SHA256,
							  kMode_SSS_Sign);

	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(TLS_SIGN)", status);
		goto cleanup;
	}

	status = sss_asymmetric_sign_digest(&sign_ctx, k_tls_handshake_digest,
					    sizeof(k_tls_handshake_digest),
					    signature, &signature_len);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "SignDigest(TLS_HANDSHAKE)", status);
		goto cleanup;
	}

	LOG_INF("TLS CertificateVerify signature produced");
	se05x_demo_log_hex_preview("TLSSignature", signature, signature_len);
	se05x_demo_mark_pass(stats, "SignDigest(TLS_HANDSHAKE)");

cleanup:
	if (sign_ctx.session != NULL) {
		sss_asymmetric_context_free(&sign_ctx);
	}
}

static sss_status_t run_tls_client_identity(ex_sss_boot_ctx_t *boot_ctx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;
	sss_object_t key_pair = { 0 };

	se05x_demo_stats_init(&stats, "TLS_CLIENT_IDENTITY");
	LOG_INF("TLS_CLIENT_IDENTITY started: read certificate and sign TLS handshake digest in SE");
	LOG_INF("No new NVM writes; requires key=0x%08" PRIX32 " cert=0x%08" PRIX32,
		(uint32_t)SE05X_DEMO_OBJECT_ID_ECC_KEY,
		(uint32_t)SE05X_DEMO_OBJECT_ID_CERT);

	require_object(&stats, se_session, SE05X_DEMO_OBJECT_ID_ECC_KEY,
		       "CheckObjectExists(TLS_KEY)");
	require_object(&stats, se_session, SE05X_DEMO_OBJECT_ID_CERT,
		       "CheckObjectExists(TLS_CERT)");

	if (stats.fail == 0U) {
		load_key_handle(&stats, boot_ctx, &key_pair);
	}
	if (stats.fail == 0U) {
		read_tls_certificate(&stats, boot_ctx);
	}
	if (stats.fail == 0U) {
		sign_tls_handshake_digest(&stats, boot_ctx, &key_pair);
	}

	sss_key_object_free(&key_pair);
	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

const se05x_demo_t g_se05x_demo_tls_client_identity = {
	.id = SE05X_DEMO_TLS_CLIENT_IDENTITY,
	.name = "tls_client_identity",
	.when_to_use = "Verify TLS/mTLS client identity material is already available in SE05x.",
	.flow = "Check ECC private key and certificate objects, read certificate, sign TLS handshake digest in SE.",
	.expected_output = "key/cert objects exist, certificate preview, TLS signature preview, and final fail=0.",
	.se_features = "Persistent EC key, persistent certificate object, TLS Certificate/CertificateVerify skeleton.",
	.run = run_tls_client_identity,
};
