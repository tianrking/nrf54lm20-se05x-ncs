/*
 * Minimal placeholder type for the NXP HOSTCRYPTO_USER SSS structs.
 *
 * The ESP-IDF implementation in port/sss_user_esp_crypto.c uses PSA Crypto
 * directly and stores its private context behind the aes_ctx_t pointers.
 */
#pragma once

#include <stdint.h>

typedef struct aes_ctx
{
    uint8_t reserved;
} aes_ctx_t;

