/*
 * Zephyr/NCS host crypto implementation for NXP SSS HOSTCRYPTO_USER.
 *
 * The upstream mbedTLS SSS backend targets the older public mbedtls/cipher.h
 * API. NCS exposes the modern PSA crypto API, so this file implements
 * the SSS user backend directly on PSA for the primitives required by SE05x
 * SCP03: AES-CBC no padding, AES-CMAC, RNG, and transient key storage.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <fsl_sss_user_apis.h>
#include <psa/crypto.h>

#if SSS_HAVE_HOSTCRYPTO_USER

typedef struct se05x_psa_mac_ctx
{
    psa_key_id_t key_id;
    psa_mac_operation_t op;
    bool active;
} se05x_psa_mac_ctx_t;

typedef struct se05x_psa_cipher_ctx
{
    psa_key_id_t key_id;
    psa_cipher_operation_t op;
    bool active;
} se05x_psa_cipher_ctx_t;

typedef struct se05x_psa_hash_ctx
{
    psa_hash_operation_t op;
    bool active;
} se05x_psa_hash_ctx_t;

static sss_status_t psa_to_sss(psa_status_t status)
{
    return (status == PSA_SUCCESS) ? kStatus_SSS_Success : kStatus_SSS_Fail;
}

static sss_status_t ensure_psa_ready(void)
{
    return psa_to_sss(psa_crypto_init());
}

static sss_status_t import_aes_key(const sss_user_impl_object_t *keyObject, psa_key_usage_t usage, psa_algorithm_t alg, psa_key_id_t *key_id)
{
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;

    if ((keyObject == NULL) || (key_id == NULL) || (keyObject->contents == NULL) || (keyObject->contents_size == 0)) {
        return kStatus_SSS_InvalidArgument;
    }

    if (ensure_psa_ready() != kStatus_SSS_Success) {
        return kStatus_SSS_Fail;
    }

    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attrs, keyObject->contents_size * 8U);
    psa_set_key_usage_flags(&attrs, usage);
    psa_set_key_algorithm(&attrs, alg);

    psa_status_t status = psa_import_key(&attrs, keyObject->contents, keyObject->contents_size, key_id);
    psa_reset_key_attributes(&attrs);
    return psa_to_sss(status);
}

static psa_algorithm_t to_psa_cipher_alg(sss_algorithm_t algorithm)
{
    switch (algorithm) {
    case kAlgorithm_SSS_AES_ECB:
        return PSA_ALG_ECB_NO_PADDING;
    case kAlgorithm_SSS_AES_CBC:
        return PSA_ALG_CBC_NO_PADDING;
    case kAlgorithm_SSS_AES_CTR:
        return PSA_ALG_CTR;
    default:
        return 0;
    }
}

sss_status_t sss_user_impl_session_create(sss_user_impl_session_t *session,
    sss_type_t subsystem,
    uint32_t application_id,
    sss_connection_type_t connetion_type,
    void *connectionData)
{
    (void)application_id;
    (void)connetion_type;
    (void)connectionData;

    if ((session == NULL) || !SSS_SUBSYSTEM_TYPE_IS_HOST(subsystem)) {
        return kStatus_SSS_InvalidArgument;
    }
    session->subsystem = subsystem;
    session->ptr       = NULL;
    return ensure_psa_ready();
}

sss_status_t sss_user_impl_session_open(sss_user_impl_session_t *session,
    sss_type_t subsystem,
    uint32_t application_id,
    sss_connection_type_t connetion_type,
    void *connectionData)
{
    return sss_user_impl_session_create(session, subsystem, application_id, connetion_type, connectionData);
}

sss_status_t sss_user_impl_session_prop_get_u32(sss_user_impl_session_t *session, uint32_t property, uint32_t *pValue)
{
    (void)session;
    (void)property;
    (void)pValue;
    return kStatus_SSS_InvalidArgument;
}

sss_status_t sss_user_impl_session_prop_get_au8(
    sss_user_impl_session_t *session, uint32_t property, uint8_t *pValue, size_t *pValueLen)
{
    (void)session;
    (void)property;
    (void)pValue;
    (void)pValueLen;
    return kStatus_SSS_InvalidArgument;
}

void sss_user_impl_session_close(sss_user_impl_session_t *session)
{
    if (session != NULL) {
        session->subsystem = kType_SSS_SubSystem_NONE;
        session->ptr       = NULL;
    }
}

void sss_user_impl_session_delete(sss_user_impl_session_t *session)
{
    sss_user_impl_session_close(session);
}

sss_status_t sss_user_impl_key_object_init(sss_user_impl_object_t *keyObject, sss_user_impl_key_store_t *keyStore)
{
    if ((keyObject == NULL) || (keyStore == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    memset(keyObject, 0, sizeof(*keyObject));
    keyObject->keyStore = keyStore;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_allocate_handle(sss_user_impl_object_t *keyObject,
    uint32_t keyId,
    sss_key_part_t keyPart,
    sss_cipher_type_t cipherType,
    size_t keyByteLenMax,
    uint32_t options)
{
    (void)keyPart;
    (void)keyByteLenMax;
    (void)options;

    if ((keyObject == NULL) || (keyObject->keyStore == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    keyObject->keyId      = keyId;
    keyObject->cipherType = cipherType;
    keyObject->objectType = keyPart;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_get_handle(sss_user_impl_object_t *keyObject, uint32_t keyId)
{
    if (keyObject == NULL) {
        return kStatus_SSS_InvalidArgument;
    }
    return (keyObject->keyId == keyId) ? kStatus_SSS_Success : kStatus_SSS_Fail;
}

sss_status_t sss_user_impl_key_object_set_user(sss_user_impl_object_t *keyObject, uint32_t user, uint32_t options)
{
    (void)keyObject;
    (void)user;
    (void)options;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_set_purpose(sss_user_impl_object_t *keyObject, sss_mode_t purpose, uint32_t options)
{
    (void)keyObject;
    (void)purpose;
    (void)options;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_set_access(sss_user_impl_object_t *keyObject, uint32_t access, uint32_t options)
{
    (void)keyObject;
    (void)access;
    (void)options;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_set_eccgfp_group(sss_user_impl_object_t *keyObject, sss_eccgfp_group_t *group)
{
    (void)keyObject;
    (void)group;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_get_user(sss_user_impl_object_t *keyObject, uint32_t *user)
{
    (void)keyObject;
    if (user != NULL) {
        *user = 0;
    }
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_get_purpose(sss_user_impl_object_t *keyObject, sss_mode_t *purpose)
{
    (void)keyObject;
    if (purpose != NULL) {
        *purpose = kMode_SSS_Mac;
    }
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_object_get_access(sss_user_impl_object_t *keyObject, uint32_t *access)
{
    (void)keyObject;
    if (access != NULL) {
        *access = 0;
    }
    return kStatus_SSS_Success;
}

void sss_user_impl_key_object_free(sss_user_impl_object_t *keyObject)
{
    if (keyObject == NULL) {
        return;
    }

    if ((keyObject->contents != NULL) && (keyObject->contents != keyObject->key)) {
        free(keyObject->contents);
    }
    keyObject->contents      = NULL;
    keyObject->contents_size = 0;
    memset(keyObject->key, 0, sizeof(keyObject->key));
}

sss_status_t sss_user_impl_derive_key_context_init(sss_user_impl_derive_key_t *context,
    sss_user_impl_session_t *session,
    sss_user_impl_object_t *keyObject,
    sss_algorithm_t algorithm,
    sss_mode_t mode)
{
    if ((context == NULL) || (session == NULL) || (keyObject == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    context->session   = session;
    context->keyObject = keyObject;
    context->algorithm = algorithm;
    context->mode      = mode;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_derive_key_go(sss_user_impl_derive_key_t *context,
    const uint8_t *saltData,
    size_t saltLen,
    const uint8_t *info,
    size_t infoLen,
    sss_user_impl_object_t *derivedKeyObject,
    uint16_t deriveDataLen,
    uint8_t *hkdfOutput,
    size_t *hkdfOutputLen)
{
    (void)context;
    (void)saltData;
    (void)saltLen;
    (void)info;
    (void)infoLen;
    (void)derivedKeyObject;
    (void)deriveDataLen;
    (void)hkdfOutput;
    (void)hkdfOutputLen;
    return kStatus_SSS_Fail;
}

sss_status_t sss_user_impl_derive_key_dh(sss_user_impl_derive_key_t *context,
    sss_user_impl_object_t *otherPartyKeyObject,
    sss_user_impl_object_t *derivedKeyObject)
{
    (void)context;
    (void)otherPartyKeyObject;
    (void)derivedKeyObject;
    return kStatus_SSS_Fail;
}

void sss_user_impl_derive_key_context_free(sss_user_impl_derive_key_t *context)
{
    (void)context;
}

sss_status_t sss_user_impl_key_store_context_init(sss_user_impl_key_store_t *keyStore, sss_user_impl_session_t *session)
{
    if ((keyStore == NULL) || (session == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    keyStore->session = session;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_allocate(sss_user_impl_key_store_t *keyStore, uint32_t keyStoreId)
{
    (void)keyStoreId;
    return (keyStore != NULL) ? kStatus_SSS_Success : kStatus_SSS_InvalidArgument;
}

sss_status_t sss_user_impl_key_store_save(sss_user_impl_key_store_t *keyStore)
{
    (void)keyStore;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_load(sss_user_impl_key_store_t *keyStore)
{
    (void)keyStore;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_set_key(sss_user_impl_key_store_t *keyStore,
    sss_user_impl_object_t *keyObject,
    const uint8_t *data,
    size_t dataLen,
    size_t keyBitLen,
    void *options,
    size_t optionsLen)
{
    (void)keyStore;
    (void)keyBitLen;
    (void)options;
    (void)optionsLen;

    if ((keyObject == NULL) || (data == NULL) || (dataLen == 0)) {
        return kStatus_SSS_InvalidArgument;
    }

    if ((keyObject->contents != NULL) && (keyObject->contents != keyObject->key)) {
        free(keyObject->contents);
        keyObject->contents = NULL;
    }

    if (dataLen <= sizeof(keyObject->key)) {
        memcpy(keyObject->key, data, dataLen);
        keyObject->contents = keyObject->key;
    }
    else {
        keyObject->contents = malloc(dataLen);
        if (keyObject->contents == NULL) {
            keyObject->contents_size = 0;
            return kStatus_SSS_Fail;
        }
        memcpy(keyObject->contents, data, dataLen);
    }

    keyObject->contents_size = dataLen;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_generate_key(
    sss_user_impl_key_store_t *keyStore, sss_user_impl_object_t *keyObject, size_t keyBitLen, void *options)
{
    (void)keyStore;
    (void)options;

    if ((keyObject == NULL) || ((keyBitLen % 8U) != 0U)) {
        return kStatus_SSS_InvalidArgument;
    }

    size_t keyLen = keyBitLen / 8U;
    uint8_t *tmp  = malloc(keyLen);
    if (tmp == NULL) {
        return kStatus_SSS_Fail;
    }

    sss_status_t status = psa_to_sss(psa_generate_random(tmp, keyLen));
    if (status == kStatus_SSS_Success) {
        status = sss_user_impl_key_store_set_key(keyStore, keyObject, tmp, keyLen, keyBitLen, NULL, 0);
    }
    free(tmp);
    return status;
}

sss_status_t sss_user_impl_key_store_get_key(sss_user_impl_key_store_t *keyStore,
    sss_user_impl_object_t *keyObject,
    uint8_t *data,
    size_t *dataLen,
    size_t *pKeyBitLen)
{
    (void)keyStore;

    if ((keyObject == NULL) || (data == NULL) || (dataLen == NULL) || (keyObject->contents == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    if (*dataLen < keyObject->contents_size) {
        return kStatus_SSS_Fail;
    }

    memcpy(data, keyObject->contents, keyObject->contents_size);
    *dataLen = keyObject->contents_size;
    if (pKeyBitLen != NULL) {
        *pKeyBitLen = keyObject->contents_size * 8U;
    }
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_open_key(sss_user_impl_key_store_t *keyStore, sss_user_impl_object_t *keyObject)
{
    (void)keyStore;
    return (keyObject != NULL) ? kStatus_SSS_Success : kStatus_SSS_InvalidArgument;
}

sss_status_t sss_user_impl_key_store_freeze_key(sss_user_impl_key_store_t *keyStore, sss_user_impl_object_t *keyObject)
{
    (void)keyStore;
    (void)keyObject;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_erase_key(sss_user_impl_key_store_t *keyStore, sss_user_impl_object_t *keyObject)
{
    (void)keyStore;
    sss_user_impl_key_object_free(keyObject);
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_key_store_prop_get_u32(sss_user_impl_key_store_t *session, uint32_t property, uint32_t *pValue)
{
    (void)session;
    (void)property;
    (void)pValue;
    return kStatus_SSS_InvalidArgument;
}

sss_status_t sss_user_impl_key_store_prop_get_au8(
    sss_user_impl_key_store_t *session, uint32_t property, uint8_t *pValue, size_t *pValueLen)
{
    (void)session;
    (void)property;
    (void)pValue;
    (void)pValueLen;
    return kStatus_SSS_InvalidArgument;
}

void sss_user_impl_key_store_context_free(sss_user_impl_key_store_t *keyStore)
{
    if (keyStore != NULL) {
        keyStore->session = NULL;
    }
}

sss_status_t sss_user_impl_asymmetric_context_init(sss_user_impl_asymmetric_t *context,
    sss_user_impl_session_t *session,
    sss_user_impl_object_t *keyObject,
    sss_algorithm_t algorithm,
    sss_mode_t mode)
{
    if ((context == NULL) || (session == NULL) || (keyObject == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    context->session   = session;
    context->keyObject = keyObject;
    context->algorithm = algorithm;
    context->mode      = mode;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_asymmetric_encrypt(
    sss_user_impl_asymmetric_t *context, const uint8_t *srcData, size_t srcLen, uint8_t *destData, size_t *destLen)
{
    (void)context;
    (void)srcData;
    (void)srcLen;
    (void)destData;
    (void)destLen;
    return kStatus_SSS_Fail;
}

sss_status_t sss_user_impl_asymmetric_decrypt(
    sss_user_impl_asymmetric_t *context, const uint8_t *srcData, size_t srcLen, uint8_t *destData, size_t *destLen)
{
    (void)context;
    (void)srcData;
    (void)srcLen;
    (void)destData;
    (void)destLen;
    return kStatus_SSS_Fail;
}

sss_status_t sss_user_impl_asymmetric_sign_digest(
    sss_user_impl_asymmetric_t *context, uint8_t *digest, size_t digestLen, uint8_t *signature, size_t *signatureLen)
{
    (void)context;
    (void)digest;
    (void)digestLen;
    (void)signature;
    (void)signatureLen;
    return kStatus_SSS_Fail;
}

sss_status_t sss_user_impl_asymmetric_verify_digest(
    sss_user_impl_asymmetric_t *context, uint8_t *digest, size_t digestLen, uint8_t *signature, size_t signatureLen)
{
    (void)context;
    (void)digest;
    (void)digestLen;
    (void)signature;
    (void)signatureLen;
    return kStatus_SSS_Fail;
}

void sss_user_impl_asymmetric_context_free(sss_user_impl_asymmetric_t *context)
{
    (void)context;
}

sss_status_t sss_user_impl_symmetric_context_init(sss_user_impl_symmetric_t *context,
    sss_user_impl_session_t *session,
    sss_user_impl_object_t *keyObject,
    sss_algorithm_t algorithm,
    sss_mode_t mode)
{
    if ((context == NULL) || (session == NULL) || (keyObject == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    context->session   = session;
    context->keyObject = keyObject;
    context->algorithm = algorithm;
    context->mode      = mode;
    context->pAesctx   = NULL;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_cipher_one_go(sss_user_impl_symmetric_t *context,
    uint8_t *iv,
    size_t ivLen,
    const uint8_t *srcData,
    uint8_t *destData,
    size_t dataLen)
{
    if ((context == NULL) || (iv == NULL) || (ivLen != 16U) || (srcData == NULL) || (destData == NULL) ||
        (context->keyObject == NULL) || (context->algorithm != kAlgorithm_SSS_AES_CBC) || ((dataLen % 16U) != 0U)) {
        return kStatus_SSS_InvalidArgument;
    }

    const bool encrypt = (context->mode == kMode_SSS_Encrypt);
    if (!encrypt && (context->mode != kMode_SSS_Decrypt)) {
        return kStatus_SSS_InvalidArgument;
    }

    psa_key_id_t key_id = 0;
    psa_key_usage_t usage =
        encrypt ? (PSA_KEY_USAGE_ENCRYPT) : (PSA_KEY_USAGE_DECRYPT);
    sss_status_t sss_status = import_aes_key(context->keyObject, usage, PSA_ALG_CBC_NO_PADDING, &key_id);
    if (sss_status != kStatus_SSS_Success) {
        return sss_status;
    }

    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    psa_status_t status = encrypt ? psa_cipher_encrypt_setup(&op, key_id, PSA_ALG_CBC_NO_PADDING) :
                                    psa_cipher_decrypt_setup(&op, key_id, PSA_ALG_CBC_NO_PADDING);
    size_t outLen = 0;
    size_t total  = 0;

    if (status == PSA_SUCCESS) {
        status = psa_cipher_set_iv(&op, iv, ivLen);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_update(&op, srcData, dataLen, destData, dataLen, &outLen);
        total += outLen;
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_finish(&op, destData + total, dataLen - total, &outLen);
        total += outLen;
    }

    psa_cipher_abort(&op);
    psa_destroy_key(key_id);

    if ((status != PSA_SUCCESS) || (total != dataLen)) {
        return kStatus_SSS_Fail;
    }
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_cipher_init(sss_user_impl_symmetric_t *context, uint8_t *iv, size_t ivLen)
{
    if ((context == NULL) || (context->keyObject == NULL) || (context->pAesctx != NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    const bool encrypt = (context->mode == kMode_SSS_Encrypt);
    if (!encrypt && (context->mode != kMode_SSS_Decrypt)) {
        return kStatus_SSS_InvalidArgument;
    }

    psa_algorithm_t alg = to_psa_cipher_alg(context->algorithm);
    if (alg == 0) {
        return kStatus_SSS_InvalidArgument;
    }
    if ((context->algorithm == kAlgorithm_SSS_AES_ECB) && (ivLen != 0U)) {
        return kStatus_SSS_InvalidArgument;
    }
    if (((context->algorithm == kAlgorithm_SSS_AES_CBC) || (context->algorithm == kAlgorithm_SSS_AES_CTR)) &&
        ((iv == NULL) || (ivLen != 16U))) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_cipher_ctx_t *cipher_ctx = calloc(1, sizeof(*cipher_ctx));
    if (cipher_ctx == NULL) {
        return kStatus_SSS_Fail;
    }

    psa_key_usage_t usage = encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT;
    sss_status_t sss_status = import_aes_key(context->keyObject, usage, alg, &cipher_ctx->key_id);
    if (sss_status != kStatus_SSS_Success) {
        free(cipher_ctx);
        return sss_status;
    }

    cipher_ctx->op = psa_cipher_operation_init();
    psa_status_t status = encrypt ? psa_cipher_encrypt_setup(&cipher_ctx->op, cipher_ctx->key_id, alg) :
                                    psa_cipher_decrypt_setup(&cipher_ctx->op, cipher_ctx->key_id, alg);
    if ((status == PSA_SUCCESS) && (context->algorithm != kAlgorithm_SSS_AES_ECB)) {
        status = psa_cipher_set_iv(&cipher_ctx->op, iv, ivLen);
    }
    if (status != PSA_SUCCESS) {
        psa_cipher_abort(&cipher_ctx->op);
        psa_destroy_key(cipher_ctx->key_id);
        free(cipher_ctx);
        return kStatus_SSS_Fail;
    }

    cipher_ctx->active = true;
    context->pAesctx = (aes_ctx_t *)cipher_ctx;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_cipher_update(
    sss_user_impl_symmetric_t *context, const uint8_t *srcData, size_t srcLen, uint8_t *destData, size_t *destLen)
{
    if ((context == NULL) || (context->pAesctx == NULL) || (destData == NULL) || (destLen == NULL) ||
        ((srcData == NULL) && (srcLen != 0U))) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_cipher_ctx_t *cipher_ctx = (se05x_psa_cipher_ctx_t *)context->pAesctx;
    if (!cipher_ctx->active) {
        return kStatus_SSS_Fail;
    }

    size_t outLen = 0;
    psa_status_t status = psa_cipher_update(&cipher_ctx->op, srcData, srcLen, destData, *destLen, &outLen);
    if (status == PSA_SUCCESS) {
        *destLen = outLen;
    }
    return psa_to_sss(status);
}

sss_status_t sss_user_impl_cipher_finish(
    sss_user_impl_symmetric_t *context, const uint8_t *srcData, size_t srcLen, uint8_t *destData, size_t *destLen)
{
    if ((context == NULL) || (context->pAesctx == NULL) || (destData == NULL) || (destLen == NULL) ||
        ((srcData == NULL) && (srcLen != 0U))) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_cipher_ctx_t *cipher_ctx = (se05x_psa_cipher_ctx_t *)context->pAesctx;
    if (!cipher_ctx->active) {
        return kStatus_SSS_Fail;
    }

    size_t total = 0;
    size_t outLen = 0;
    psa_status_t status = PSA_SUCCESS;
    if (srcLen != 0U) {
        status = psa_cipher_update(&cipher_ctx->op, srcData, srcLen, destData, *destLen, &outLen);
        total += outLen;
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_finish(&cipher_ctx->op, destData + total, *destLen - total, &outLen);
        total += outLen;
    }

    cipher_ctx->active = false;
    if (status == PSA_SUCCESS) {
        *destLen = total;
    }
    return psa_to_sss(status);
}

sss_status_t sss_user_impl_cipher_crypt_ctr(sss_user_impl_symmetric_t *context,
    const uint8_t *srcData,
    uint8_t *destData,
    size_t size,
    uint8_t *initialCounter,
    uint8_t *lastEncryptedCounter,
    size_t *szLeft)
{
    if ((context == NULL) || (srcData == NULL) || (destData == NULL) || (initialCounter == NULL) ||
        (context->keyObject == NULL) || (context->algorithm != kAlgorithm_SSS_AES_CTR)) {
        return kStatus_SSS_InvalidArgument;
    }

    psa_key_id_t key_id = 0;
    sss_status_t sss_status =
        import_aes_key(context->keyObject, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_CTR, &key_id);
    if (sss_status != kStatus_SSS_Success) {
        return sss_status;
    }

    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    psa_status_t status = psa_cipher_encrypt_setup(&op, key_id, PSA_ALG_CTR);
    size_t outLen = 0;
    size_t finalLen = 0;
    if (status == PSA_SUCCESS) {
        status = psa_cipher_set_iv(&op, initialCounter, 16U);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_update(&op, srcData, size, destData, size, &outLen);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_finish(&op, destData + outLen, size - outLen, &finalLen);
    }

    psa_cipher_abort(&op);
    psa_destroy_key(key_id);
    if (status != PSA_SUCCESS || ((outLen + finalLen) != size)) {
        return kStatus_SSS_Fail;
    }
    if (lastEncryptedCounter != NULL) {
        memcpy(lastEncryptedCounter, initialCounter, 16U);
    }
    if (szLeft != NULL) {
        *szLeft = 0;
    }
    return kStatus_SSS_Success;
}

void sss_user_impl_symmetric_context_free(sss_user_impl_symmetric_t *context)
{
    if ((context == NULL) || (context->pAesctx == NULL)) {
        return;
    }

    se05x_psa_cipher_ctx_t *cipher_ctx = (se05x_psa_cipher_ctx_t *)context->pAesctx;
    if (cipher_ctx->active) {
        psa_cipher_abort(&cipher_ctx->op);
    }
    if (cipher_ctx->key_id != 0) {
        psa_destroy_key(cipher_ctx->key_id);
    }
    free(cipher_ctx);
    context->pAesctx = NULL;
}

sss_status_t sss_user_impl_mac_context_init(sss_user_impl_mac_t *context,
    sss_user_impl_session_t *session,
    sss_user_impl_object_t *keyObject,
    sss_algorithm_t algorithm,
    sss_mode_t mode)
{
    if ((context == NULL) || (session == NULL) || (keyObject == NULL) || (algorithm != kAlgorithm_SSS_CMAC_AES) ||
        (mode != kMode_SSS_Mac)) {
        return kStatus_SSS_InvalidArgument;
    }

    memset(context, 0, sizeof(*context));
    context->session   = session;
    context->keyObject = keyObject;
    context->algorithm = algorithm;
    context->mode      = mode;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_mac_one_go(
    sss_user_impl_mac_t *context, const uint8_t *message, size_t messageLen, uint8_t *mac, size_t *macLen)
{
    sss_status_t status = sss_user_impl_mac_init(context);
    if (status != kStatus_SSS_Success) {
        return status;
    }
    status = sss_user_impl_mac_update(context, message, messageLen);
    if (status == kStatus_SSS_Success) {
        status = sss_user_impl_mac_finish(context, mac, macLen);
    }
    sss_user_impl_mac_context_free(context);
    return status;
}

sss_status_t sss_user_impl_mac_init(sss_user_impl_mac_t *context)
{
    if ((context == NULL) || (context->keyObject == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_mac_ctx_t *mac_ctx = calloc(1, sizeof(*mac_ctx));
    if (mac_ctx == NULL) {
        return kStatus_SSS_Fail;
    }

    sss_status_t status =
        import_aes_key(context->keyObject, PSA_KEY_USAGE_SIGN_MESSAGE, PSA_ALG_CMAC, &mac_ctx->key_id);
    if (status != kStatus_SSS_Success) {
        free(mac_ctx);
        return status;
    }

    mac_ctx->op = psa_mac_operation_init();
    psa_status_t psa_status = psa_mac_sign_setup(&mac_ctx->op, mac_ctx->key_id, PSA_ALG_CMAC);
    if (psa_status != PSA_SUCCESS) {
        psa_mac_abort(&mac_ctx->op);
        psa_destroy_key(mac_ctx->key_id);
        free(mac_ctx);
        return kStatus_SSS_Fail;
    }

    mac_ctx->active     = true;
    context->pAesmacctx = (aes_ctx_t *)mac_ctx;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_mac_update(sss_user_impl_mac_t *context, const uint8_t *message, size_t messageLen)
{
    if ((context == NULL) || (context->pAesmacctx == NULL) || ((message == NULL) && (messageLen != 0))) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_mac_ctx_t *mac_ctx = (se05x_psa_mac_ctx_t *)context->pAesmacctx;
    if (!mac_ctx->active) {
        return kStatus_SSS_Fail;
    }
    return psa_to_sss(psa_mac_update(&mac_ctx->op, message, messageLen));
}

sss_status_t sss_user_impl_mac_finish(sss_user_impl_mac_t *context, uint8_t *mac, size_t *macLen)
{
    if ((context == NULL) || (context->pAesmacctx == NULL) || (mac == NULL) || (macLen == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_mac_ctx_t *mac_ctx = (se05x_psa_mac_ctx_t *)context->pAesmacctx;
    size_t outLen                = 0;
    psa_status_t status          = psa_mac_sign_finish(&mac_ctx->op, mac, *macLen, &outLen);
    mac_ctx->active              = false;
    if (status == PSA_SUCCESS) {
        *macLen = outLen;
        memcpy(context->calc_mac, mac, (outLen <= sizeof(context->calc_mac)) ? outLen : sizeof(context->calc_mac));
    }
    return psa_to_sss(status);
}

void sss_user_impl_mac_context_free(sss_user_impl_mac_t *context)
{
    if ((context == NULL) || (context->pAesmacctx == NULL)) {
        return;
    }

    se05x_psa_mac_ctx_t *mac_ctx = (se05x_psa_mac_ctx_t *)context->pAesmacctx;
    if (mac_ctx->active) {
        psa_mac_abort(&mac_ctx->op);
    }
    if (mac_ctx->key_id != 0) {
        psa_destroy_key(mac_ctx->key_id);
    }
    free(mac_ctx);
    context->pAesmacctx = NULL;
}

sss_status_t sss_user_impl_digest_context_init(
    sss_user_impl_digest_t *context, sss_user_impl_session_t *session, sss_algorithm_t algorithm, sss_mode_t mode)
{
    if ((context == NULL) || (session == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    context->session = session;
    context->algorithm = algorithm;
    context->mode = mode;
    context->pDigestCtx = NULL;
    switch (algorithm) {
    case kAlgorithm_SSS_SHA1:
        context->digestFullLen = 20;
        break;
    case kAlgorithm_SSS_SHA224:
        context->digestFullLen = 28;
        break;
    case kAlgorithm_SSS_SHA256:
        context->digestFullLen = 32;
        break;
    case kAlgorithm_SSS_SHA384:
        context->digestFullLen = 48;
        break;
    case kAlgorithm_SSS_SHA512:
        context->digestFullLen = 64;
        break;
    default:
        return kStatus_SSS_InvalidArgument;
    }
    return kStatus_SSS_Success;
}

static psa_algorithm_t to_psa_hash_alg(sss_algorithm_t algorithm)
{
    switch (algorithm) {
    case kAlgorithm_SSS_SHA1:
        return PSA_ALG_SHA_1;
    case kAlgorithm_SSS_SHA224:
        return PSA_ALG_SHA_224;
    case kAlgorithm_SSS_SHA256:
        return PSA_ALG_SHA_256;
    case kAlgorithm_SSS_SHA384:
        return PSA_ALG_SHA_384;
    case kAlgorithm_SSS_SHA512:
        return PSA_ALG_SHA_512;
    default:
        return 0;
    }
}

sss_status_t sss_user_impl_digest_one_go(
    sss_user_impl_digest_t *context, const uint8_t *message, size_t messageLen, uint8_t *digest, size_t *digestLen)
{
    if ((context == NULL) || (digest == NULL) || (digestLen == NULL) || ((message == NULL) && (messageLen != 0))) {
        return kStatus_SSS_InvalidArgument;
    }

    psa_algorithm_t alg = to_psa_hash_alg(context->algorithm);
    if (alg == 0) {
        return kStatus_SSS_InvalidArgument;
    }

    size_t outLen = 0;
    psa_status_t status = psa_hash_compute(alg, message, messageLen, digest, *digestLen, &outLen);
    if (status == PSA_SUCCESS) {
        *digestLen = outLen;
    }
    return psa_to_sss(status);
}

sss_status_t sss_user_impl_digest_init(sss_user_impl_digest_t *context)
{
    if ((context == NULL) || (context->pDigestCtx != NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    psa_algorithm_t alg = to_psa_hash_alg(context->algorithm);
    if (alg == 0) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_hash_ctx_t *hash_ctx = calloc(1, sizeof(*hash_ctx));
    if (hash_ctx == NULL) {
        return kStatus_SSS_Fail;
    }

    hash_ctx->op = psa_hash_operation_init();
    psa_status_t status = psa_hash_setup(&hash_ctx->op, alg);
    if (status != PSA_SUCCESS) {
        psa_hash_abort(&hash_ctx->op);
        free(hash_ctx);
        return kStatus_SSS_Fail;
    }

    hash_ctx->active = true;
    context->pDigestCtx = hash_ctx;
    return kStatus_SSS_Success;
}

sss_status_t sss_user_impl_digest_update(sss_user_impl_digest_t *context, const uint8_t *message, size_t messageLen)
{
    if ((context == NULL) || (context->pDigestCtx == NULL) || ((message == NULL) && (messageLen != 0U))) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_hash_ctx_t *hash_ctx = (se05x_psa_hash_ctx_t *)context->pDigestCtx;
    if (!hash_ctx->active) {
        return kStatus_SSS_Fail;
    }
    return psa_to_sss(psa_hash_update(&hash_ctx->op, message, messageLen));
}

sss_status_t sss_user_impl_digest_finish(sss_user_impl_digest_t *context, uint8_t *digest, size_t *digestLen)
{
    if ((context == NULL) || (context->pDigestCtx == NULL) || (digest == NULL) || (digestLen == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }

    se05x_psa_hash_ctx_t *hash_ctx = (se05x_psa_hash_ctx_t *)context->pDigestCtx;
    size_t outLen = 0;
    psa_status_t status = psa_hash_finish(&hash_ctx->op, digest, *digestLen, &outLen);
    hash_ctx->active = false;
    if (status == PSA_SUCCESS) {
        *digestLen = outLen;
    }
    return psa_to_sss(status);
}

void sss_user_impl_digest_context_free(sss_user_impl_digest_t *context)
{
    if ((context == NULL) || (context->pDigestCtx == NULL)) {
        return;
    }

    se05x_psa_hash_ctx_t *hash_ctx = (se05x_psa_hash_ctx_t *)context->pDigestCtx;
    if (hash_ctx->active) {
        psa_hash_abort(&hash_ctx->op);
    }
    free(hash_ctx);
    context->pDigestCtx = NULL;
}

sss_status_t sss_user_impl_rng_context_init(sss_user_impl_rng_context_t *context, sss_user_impl_session_t *session)
{
    if ((context == NULL) || (session == NULL)) {
        return kStatus_SSS_InvalidArgument;
    }
    context->session = session;
    return ensure_psa_ready();
}

sss_status_t sss_user_impl_rng_get_random(sss_user_impl_rng_context_t *context, uint8_t *random_data, size_t dataLen)
{
    (void)context;
    if ((random_data == NULL) || (dataLen == 0)) {
        return kStatus_SSS_InvalidArgument;
    }
    return psa_to_sss(psa_generate_random(random_data, dataLen));
}

sss_status_t sss_user_impl_rng_context_free(sss_user_impl_rng_context_t *context)
{
    if (context != NULL) {
        context->session = NULL;
    }
    return kStatus_SSS_Success;
}

#endif /* SSS_HAVE_HOSTCRYPTO_USER */
