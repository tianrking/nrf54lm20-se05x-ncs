#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "fsl_sss_se05x_types.h"
#include "se05x_APDU.h"
#include "se05x_APDU_apis.h"
#include "se05x_demo.h"
#include "se05x_ecc_curves.h"
#include "se05x_enums.h"

LOG_MODULE_REGISTER(se05x_demo_eth_wallet, LOG_LEVEL_INF);

#define ETH_UART_CMD_MAX_LEN 192U
#define ETH_KECCAK256_LEN 32U
#define ETH_SECP256K1_PUBKEY_LEN 65U
#define ETH_ADDRESS_LEN 20U
#define ETH_TX_DATA_MAX_LEN 64U
#define ETH_MAX_RLP_LEN 512U

/*
 * Demo 10: eth_wallet_sign.
 *
 * 这个 demo 用来把 Demo09 已经验证过的 secp256k1 能力，继续推进到“以太坊
 * legacy transfer 签名链路”。它演示的是协议流程，不是生产钱包的完整成品。
 *
 * 真实 ETH legacy 转账需要以下交易字段：
 *   1. nonce：发送地址已经发出的交易序号，必须由手机/上位机从链上查询。
 *   2. gasPrice：legacy 交易的 gas 单价，单位 wei。
 *   3. gasLimit：普通 ETH 转账通常是 21000。
 *   4. to：20 字节收款地址。
 *   5. value：转账金额，单位 wei。
 *   6. data：普通 ETH 转账为空；合约调用时是 calldata。
 *   7. chainId：EIP-155 防重放链 ID，例如 Ethereum mainnet 为 1。
 *
 * 本 demo 固定一笔示例交易，流程如下：
 *   1. nRF 侧把交易字段按 legacy/EIP-155 规则做 RLP 编码：
 *      [nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]
 *   2. nRF 侧对 RLP signing payload 做 Ethereum Keccak-256。
 *      注意：Ethereum 使用 Keccak-256，不是 FIPS SHA3-256。
 *   3. SE05x 内生成 transient secp256k1 私钥，私钥不导出、不打印。
 *   4. 从 SE05x key pair object 读取公钥部分，nRF 用 Keccak-256(pubkey X||Y)
 *      推导 ETH from address。
 *   5. SE05x 对第 2 步 digest 做 ECDSA sign_digest，输出 DER signature。
 *   6. nRF 解析 DER 得到 r/s，并做 low-S 归一化，输出 ETH 可用的 r/s。
 *   7. 因 SE05x ECDSA 签名接口不返回 recovery id，本 demo 输出两个 v 候选：
 *      chainId * 2 + 35 和 chainId * 2 + 36。PC/手机端需要用公钥恢复或验证
 *      地址，选择正确的 v，再广播 raw transaction。
 *
 * 安全边界：
 *   - Demo10 可在 secp256k1 未启用时调用 CreateCurve 写一次曲线参数 NVM。
 *   - 每次签名使用 Demo10 保留 object_id=0xEF100001 的 transient 测试 key。
 *   - 每次测试前后只清理这个保留 object_id，不会 DeleteAll，不会碰业务对象。
 *   - 生产钱包应改成持久 key 或明确的派生/备份策略，并加用户确认、交易解析、
 *     金额/地址显示、反重放和固件防回滚。
 *
 * 运行时串口只输出英文 ASCII，避免串口工具中文编码造成乱码。
 */

typedef struct {
	uint8_t *data;
	size_t len;
	size_t cap;
} eth_buf_t;

typedef struct {
	uint64_t nonce;
	uint64_t gas_price_wei;
	uint64_t gas_limit;
	uint8_t to[ETH_ADDRESS_LEN];
	uint64_t value_wei;
	uint8_t data[ETH_TX_DATA_MAX_LEN];
	size_t data_len;
	uint64_t chain_id;
} eth_legacy_tx_t;

static const uint8_t k_eth_sample_to_address[ETH_ADDRESS_LEN] = {
	0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35,
	0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35,
};

static eth_legacy_tx_t g_eth_tx;

static const uint8_t k_secp256k1_n[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
	0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41,
};

static const uint8_t k_secp256k1_half_n[32] = {
	0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x5d, 0x57, 0x6e, 0x73, 0x57, 0xa4, 0x50, 0x1d,
	0xdf, 0xe9, 0x2f, 0x46, 0x68, 0x1b, 0x20, 0xa0,
};

static char eth_upper_command_char(char ch)
{
	if (ch >= 'a' && ch <= 'z') {
		return (char)(ch - ('a' - 'A'));
	}

	return ch;
}

static bool eth_is_line_end(int ch)
{
	return ch == '\r' || ch == '\n';
}

static char *eth_trim_line(char *line)
{
	char *start = line;
	char *end;

	while (*start == ' ' || *start == '\t') {
		++start;
	}

	end = start + strlen(start);
	while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
		*--end = '\0';
	}

	return start;
}

static bool eth_read_uart_line(char *line, size_t line_cap)
{
	size_t len = 0;

	if (line_cap == 0U) {
		return false;
	}

	line[0] = '\0';
	while (1) {
		int ch = console_getchar();

		if (eth_is_line_end(ch)) {
			if (len == 0U) {
				continue;
			}

			line[len] = '\0';
			return true;
		}

		if ((ch == '\b' || ch == 0x7f) && len > 0U) {
			line[--len] = '\0';
			continue;
		}

		if (len >= line_cap - 1U) {
			len = 0;
			line[0] = '\0';
			return false;
		}

		line[len++] = (char)ch;
		line[len] = '\0';
	}
}

static void eth_print_menu(void)
{
	printk("\n========== SE05x Demo 10: ETH wallet sign ==========\n");
	printk("AT+H : Show this menu\n");
	printk("AT+P : Print current legacy transaction fields\n");
	printk("AT+R : Reset fields to built-in sample values\n");
	printk("AT+S : Sign current ETH legacy transaction fields\n");
	printk("AT+N=<decimal> : Set nonce\n");
	printk("AT+G=<decimal> : Set gasPrice in wei\n");
	printk("AT+L=<decimal> : Set gasLimit\n");
	printk("AT+T=<40 hex>  : Set recipient address, with or without 0x\n");
	printk("AT+V=<decimal> : Set value in wei\n");
	printk("AT+C=<decimal> : Set EIP-155 chainId\n");
	printk("AT+D=<hex>     : Set data/calldata, empty allowed: AT+D=\n");
	printk("AT+Q : Quit Demo 10 and close SE05x session\n");
	printk("Private key policy: generated and used inside SE05x, never exported.\n");
	printk("ETH note: SE05x returns ECDSA DER only; recovery id is selected by PC/phone.\n");
	printk("NVM policy: may create secp256k1 curve once; cleans only 0xEF100001.\n");
	printk("====================================================\n\n");
}

static void eth_print_hex_inline(const char *label, const uint8_t *data, size_t data_len)
{
	printk("%s=0x", label);
	for (size_t i = 0; i < data_len; i++) {
		printk("%02X", data[i]);
	}
	printk("\n");
}

static void eth_print_hex_block(const char *label, const uint8_t *data, size_t data_len)
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

static void eth_reset_tx_to_sample(eth_legacy_tx_t *tx)
{
	memset(tx, 0, sizeof(*tx));
	tx->nonce = 0U;
	tx->gas_price_wei = 1000000000ULL;
	tx->gas_limit = 21000ULL;
	memcpy(tx->to, k_eth_sample_to_address, sizeof(tx->to));
	tx->value_wei = 1000000000000000ULL;
	tx->data_len = 0U;
	tx->chain_id = 1U;
}

static void eth_print_current_tx(const eth_legacy_tx_t *tx)
{
	printk("\nCurrent ETH legacy transaction fields:\n");
	printk("ETH_TX_TYPE=legacy\n");
	printk("ETH_CHAIN_ID=%" PRIu64 "\n", tx->chain_id);
	printk("ETH_NONCE=%" PRIu64 "\n", tx->nonce);
	printk("ETH_GAS_PRICE_WEI=%" PRIu64 "\n", tx->gas_price_wei);
	printk("ETH_GAS_LIMIT=%" PRIu64 "\n", tx->gas_limit);
	eth_print_hex_inline("ETH_TO", tx->to, sizeof(tx->to));
	printk("ETH_VALUE_WEI=%" PRIu64 "\n", tx->value_wei);
	eth_print_hex_inline("ETH_DATA", tx->data, tx->data_len);
}

static bool eth_parse_u64_decimal(const char *text, uint64_t *out)
{
	uint64_t value = 0U;
	bool has_digit = false;

	while (*text == ' ' || *text == '\t') {
		++text;
	}

	for (; *text != '\0'; ++text) {
		if (*text == ' ' || *text == '\t') {
			while (*text == ' ' || *text == '\t') {
				++text;
			}
			break;
		}

		if (*text < '0' || *text > '9') {
			return false;
		}

		const uint64_t digit = (uint64_t)(*text - '0');
		if (value > (UINT64_MAX - digit) / 10ULL) {
			return false;
		}
		value = (value * 10ULL) + digit;
		has_digit = true;
	}

	if (*text != '\0') {
		return false;
	}

	*out = value;
	return has_digit;
}

static int eth_hex_nibble(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	}
	if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	}

	return -1;
}

static bool eth_parse_hex_bytes(const char *text, uint8_t *out, size_t out_cap,
				size_t *out_len)
{
	size_t len = 0U;

	while (*text == ' ' || *text == '\t') {
		++text;
	}
	if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		text += 2;
	}

	while (*text != '\0') {
		if (*text == ' ' || *text == '\t') {
			while (*text == ' ' || *text == '\t') {
				++text;
			}
			break;
		}

		const int hi = eth_hex_nibble(text[0]);
		const int lo = text[1] != '\0' ? eth_hex_nibble(text[1]) : -1;
		if (hi < 0 || lo < 0 || len >= out_cap) {
			return false;
		}
		out[len++] = (uint8_t)((hi << 4) | lo);
		text += 2;
	}

	if (*text != '\0') {
		return false;
	}

	*out_len = len;
	return true;
}

static bool eth_handle_set_command(const char *cmd, eth_legacy_tx_t *tx)
{
	if (!(cmd[0] == 'A' || cmd[0] == 'a') ||
	    !(cmd[1] == 'T' || cmd[1] == 't') ||
	    cmd[2] != '+' || cmd[4] != '=') {
		return false;
	}

	const char field = eth_upper_command_char(cmd[3]);
	const char *value = &cmd[5];
	uint64_t parsed_u64 = 0U;
	uint8_t parsed_bytes[ETH_TX_DATA_MAX_LEN] = { 0 };
	size_t parsed_len = 0U;

	switch (field) {
	case 'N':
		if (!eth_parse_u64_decimal(value, &parsed_u64)) {
			printk("ERR invalid nonce decimal.\n");
			return false;
		}
		tx->nonce = parsed_u64;
		printk("OK nonce=%" PRIu64 "\n", tx->nonce);
		return true;
	case 'G':
		if (!eth_parse_u64_decimal(value, &parsed_u64)) {
			printk("ERR invalid gasPrice decimal.\n");
			return false;
		}
		tx->gas_price_wei = parsed_u64;
		printk("OK gasPriceWei=%" PRIu64 "\n", tx->gas_price_wei);
		return true;
	case 'L':
		if (!eth_parse_u64_decimal(value, &parsed_u64)) {
			printk("ERR invalid gasLimit decimal.\n");
			return false;
		}
		tx->gas_limit = parsed_u64;
		printk("OK gasLimit=%" PRIu64 "\n", tx->gas_limit);
		return true;
	case 'T':
		if (!eth_parse_hex_bytes(value, parsed_bytes, sizeof(parsed_bytes), &parsed_len) ||
		    parsed_len != ETH_ADDRESS_LEN) {
			printk("ERR invalid to address; need 20 bytes / 40 hex chars.\n");
			return false;
		}
		memcpy(tx->to, parsed_bytes, ETH_ADDRESS_LEN);
		eth_print_hex_inline("OK to", tx->to, sizeof(tx->to));
		return true;
	case 'V':
		if (!eth_parse_u64_decimal(value, &parsed_u64)) {
			printk("ERR invalid value decimal.\n");
			return false;
		}
		tx->value_wei = parsed_u64;
		printk("OK valueWei=%" PRIu64 "\n", tx->value_wei);
		return true;
	case 'C':
		if (!eth_parse_u64_decimal(value, &parsed_u64)) {
			printk("ERR invalid chainId decimal.\n");
			return false;
		}
		tx->chain_id = parsed_u64;
		printk("OK chainId=%" PRIu64 "\n", tx->chain_id);
		return true;
	case 'D':
		if (!eth_parse_hex_bytes(value, parsed_bytes, sizeof(parsed_bytes), &parsed_len)) {
			printk("ERR invalid data hex; max %u bytes, even hex length required.\n",
			       (unsigned int)ETH_TX_DATA_MAX_LEN);
			return false;
		}
		memcpy(tx->data, parsed_bytes, parsed_len);
		tx->data_len = parsed_len;
		eth_print_hex_inline("OK data", tx->data, tx->data_len);
		return true;
	default:
		return false;
	}
}

static bool eth_buf_append(eth_buf_t *buf, const uint8_t *data, size_t data_len)
{
	if (buf->len + data_len > buf->cap) {
		return false;
	}

	memcpy(&buf->data[buf->len], data, data_len);
	buf->len += data_len;
	return true;
}

static bool eth_buf_append_byte(eth_buf_t *buf, uint8_t value)
{
	return eth_buf_append(buf, &value, 1U);
}

static size_t eth_uint64_to_be(uint64_t value, uint8_t out[8])
{
	size_t start = 0;

	for (size_t i = 0; i < 8U; i++) {
		out[i] = (uint8_t)(value >> ((7U - i) * 8U));
	}

	while (start < 8U && out[start] == 0U) {
		++start;
	}

	if (start == 8U) {
		return 0;
	}

	memmove(out, &out[start], 8U - start);
	return 8U - start;
}

static size_t eth_trim_leading_zeroes(const uint8_t *data, size_t data_len)
{
	size_t start = 0;

	while (start < data_len && data[start] == 0U) {
		++start;
	}

	return start;
}

static bool eth_rlp_append_length(eth_buf_t *buf, uint8_t short_offset, size_t payload_len)
{
	if (payload_len <= 55U) {
		return eth_buf_append_byte(buf, (uint8_t)(short_offset + payload_len));
	}

	uint8_t len_bytes[sizeof(size_t)] = { 0 };
	size_t tmp = payload_len;
	size_t len_len = 0;

	while (tmp > 0U) {
		len_bytes[sizeof(len_bytes) - 1U - len_len] = (uint8_t)(tmp & 0xffU);
		tmp >>= 8U;
		++len_len;
	}

	if (!eth_buf_append_byte(buf, (uint8_t)(short_offset + 55U + len_len))) {
		return false;
	}

	return eth_buf_append(buf, &len_bytes[sizeof(len_bytes) - len_len], len_len);
}

static bool eth_rlp_append_bytes(eth_buf_t *buf, const uint8_t *data, size_t data_len)
{
	if (data_len == 1U && data[0] < 0x80U) {
		return eth_buf_append_byte(buf, data[0]);
	}

	if (!eth_rlp_append_length(buf, 0x80U, data_len)) {
		return false;
	}

	return data_len == 0U || eth_buf_append(buf, data, data_len);
}

static bool eth_rlp_append_uint64(eth_buf_t *buf, uint64_t value)
{
	uint8_t be[8] = { 0 };
	size_t be_len = eth_uint64_to_be(value, be);

	return eth_rlp_append_bytes(buf, be, be_len);
}

static bool eth_rlp_append_bigint(eth_buf_t *buf, const uint8_t *data, size_t data_len)
{
	const size_t start = eth_trim_leading_zeroes(data, data_len);

	if (start == data_len) {
		return eth_rlp_append_bytes(buf, NULL, 0U);
	}

	return eth_rlp_append_bytes(buf, &data[start], data_len - start);
}

static bool eth_rlp_wrap_list(eth_buf_t *out, const uint8_t *payload, size_t payload_len)
{
	if (!eth_rlp_append_length(out, 0xc0U, payload_len)) {
		return false;
	}

	return eth_buf_append(out, payload, payload_len);
}

static uint64_t eth_rotl64(uint64_t value, unsigned int shift)
{
	return (value << shift) | (value >> (64U - shift));
}

static void eth_keccakf(uint64_t st[25])
{
	static const uint64_t rndc[24] = {
		0x0000000000000001ULL, 0x0000000000008082ULL,
		0x800000000000808aULL, 0x8000000080008000ULL,
		0x000000000000808bULL, 0x0000000080000001ULL,
		0x8000000080008081ULL, 0x8000000000008009ULL,
		0x000000000000008aULL, 0x0000000000000088ULL,
		0x0000000080008009ULL, 0x000000008000000aULL,
		0x000000008000808bULL, 0x800000000000008bULL,
		0x8000000000008089ULL, 0x8000000000008003ULL,
		0x8000000000008002ULL, 0x8000000000000080ULL,
		0x000000000000800aULL, 0x800000008000000aULL,
		0x8000000080008081ULL, 0x8000000000008080ULL,
		0x0000000080000001ULL, 0x8000000080008008ULL,
	};
	static const unsigned int rotc[24] = {
		1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
		27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
	};
	static const unsigned int piln[24] = {
		10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
		15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
	};

	for (size_t round = 0; round < 24U; round++) {
		uint64_t bc[5];

		for (size_t i = 0; i < 5U; i++) {
			bc[i] = st[i] ^ st[i + 5U] ^ st[i + 10U] ^
				st[i + 15U] ^ st[i + 20U];
		}
		for (size_t i = 0; i < 5U; i++) {
			const uint64_t t = bc[(i + 4U) % 5U] ^
					   eth_rotl64(bc[(i + 1U) % 5U], 1U);

			for (size_t j = 0; j < 25U; j += 5U) {
				st[j + i] ^= t;
			}
		}

		uint64_t t = st[1];
		for (size_t i = 0; i < 24U; i++) {
			const size_t j = piln[i];
			const uint64_t old = st[j];

			st[j] = eth_rotl64(t, rotc[i]);
			t = old;
		}

		for (size_t j = 0; j < 25U; j += 5U) {
			for (size_t i = 0; i < 5U; i++) {
				bc[i] = st[j + i];
			}
			for (size_t i = 0; i < 5U; i++) {
				st[j + i] ^= (~bc[(i + 1U) % 5U]) &
					     bc[(i + 2U) % 5U];
			}
		}

		st[0] ^= rndc[round];
	}
}

static void eth_keccak_absorb_block(uint64_t st[25], const uint8_t *block, size_t rate)
{
	for (size_t i = 0; i < rate; i++) {
		st[i / 8U] ^= ((uint64_t)block[i]) << ((i % 8U) * 8U);
	}
	eth_keccakf(st);
}

static void eth_keccak256(const uint8_t *input, size_t input_len, uint8_t out[32])
{
	uint64_t st[25] = { 0 };
	uint8_t block[136] = { 0 };
	const size_t rate = sizeof(block);

	while (input_len >= rate) {
		eth_keccak_absorb_block(st, input, rate);
		input += rate;
		input_len -= rate;
	}

	memcpy(block, input, input_len);
	block[input_len] = 0x01U;
	block[rate - 1U] |= 0x80U;
	eth_keccak_absorb_block(st, block, rate);

	for (size_t i = 0; i < 32U; i++) {
		out[i] = (uint8_t)(st[i / 8U] >> ((i % 8U) * 8U));
	}
}

static bool eth_build_signing_rlp(const eth_legacy_tx_t *tx, uint8_t *out,
				  size_t out_cap, size_t *out_len)
{
	uint8_t payload_storage[256] = { 0 };
	eth_buf_t payload = { .data = payload_storage, .len = 0, .cap = sizeof(payload_storage) };
	eth_buf_t rlp = { .data = out, .len = 0, .cap = out_cap };

	if (!eth_rlp_append_uint64(&payload, tx->nonce) ||
	    !eth_rlp_append_uint64(&payload, tx->gas_price_wei) ||
	    !eth_rlp_append_uint64(&payload, tx->gas_limit) ||
	    !eth_rlp_append_bytes(&payload, tx->to, sizeof(tx->to)) ||
	    !eth_rlp_append_uint64(&payload, tx->value_wei) ||
	    !eth_rlp_append_bytes(&payload, tx->data, tx->data_len) ||
	    !eth_rlp_append_uint64(&payload, tx->chain_id) ||
	    !eth_rlp_append_uint64(&payload, 0U) ||
	    !eth_rlp_append_uint64(&payload, 0U)) {
		return false;
	}

	if (!eth_rlp_wrap_list(&rlp, payload.data, payload.len)) {
		return false;
	}

	*out_len = rlp.len;
	return true;
}

static bool eth_build_signed_rlp(const eth_legacy_tx_t *tx, uint8_t *out,
				 size_t out_cap, size_t *out_len,
				 uint64_t v, const uint8_t r[32], const uint8_t s[32])
{
	uint8_t payload_storage[288] = { 0 };
	eth_buf_t payload = { .data = payload_storage, .len = 0, .cap = sizeof(payload_storage) };
	eth_buf_t rlp = { .data = out, .len = 0, .cap = out_cap };

	if (!eth_rlp_append_uint64(&payload, tx->nonce) ||
	    !eth_rlp_append_uint64(&payload, tx->gas_price_wei) ||
	    !eth_rlp_append_uint64(&payload, tx->gas_limit) ||
	    !eth_rlp_append_bytes(&payload, tx->to, sizeof(tx->to)) ||
	    !eth_rlp_append_uint64(&payload, tx->value_wei) ||
	    !eth_rlp_append_bytes(&payload, tx->data, tx->data_len) ||
	    !eth_rlp_append_uint64(&payload, v) ||
	    !eth_rlp_append_bigint(&payload, r, 32U) ||
	    !eth_rlp_append_bigint(&payload, s, 32U)) {
		return false;
	}

	if (!eth_rlp_wrap_list(&rlp, payload.data, payload.len)) {
		return false;
	}

	*out_len = rlp.len;
	return true;
}

static const uint8_t *eth_find_uncompressed_ec_point(const uint8_t *key,
						     size_t key_len,
						     size_t *point_len)
{
	const uint8_t *point = NULL;

	for (size_t i = 0; (i + ETH_SECP256K1_PUBKEY_LEN) <= key_len; i++) {
		if (key[i] == 0x04U) {
			point = &key[i];
		}
	}

	if (point != NULL) {
		*point_len = ETH_SECP256K1_PUBKEY_LEN;
	}

	return point;
}

static bool eth_curve_status_is_set(const uint8_t *curve_list, size_t curve_list_len,
				    SE05x_ECCurve_t curve_id)
{
	if (curve_id == kSE05x_ECCurve_NA || (size_t)(curve_id - 1U) >= curve_list_len) {
		return false;
	}

	return curve_list[curve_id - 1U] == kSE05x_SetIndicator_SET;
}

static smStatus_t eth_read_secp256k1_status(pSe05xSession_t session, bool *is_set)
{
	uint8_t curve_list[32] = { 0 };
	size_t curve_list_len = sizeof(curve_list);
	smStatus_t sw = Se05x_API_ReadECCurveList(session, curve_list, &curve_list_len);

	if (sw == SM_OK) {
		*is_set = eth_curve_status_is_set(curve_list, curve_list_len,
						  kSE05x_ECCurve_Secp256k1);
		LOG_INF("ReadECCurveList len=%u Secp256k1=%s", (unsigned int)curve_list_len,
			*is_set ? "SET" : "NOT_SET");
	}

	return sw;
}

static bool eth_ensure_secp256k1_curve(se05x_demo_stats_t *stats, pSe05xSession_t session)
{
	bool is_set = false;
	smStatus_t sw = eth_read_secp256k1_status(session, &is_set);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "ReadECCurveList(before)", sw);
		return false;
	}

	if (is_set) {
		se05x_demo_mark_pass(stats, "Secp256k1AlreadySET");
		return true;
	}

	LOG_WRN("Secp256k1 is NOT_SET; creating curve writes SE05x persistent NVM once");
	sw = Se05x_API_CreateCurve_secp256k1(session, kSE05x_ECCurve_Secp256k1);
	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "CreateCurve_secp256k1", sw);
		return false;
	}
	se05x_demo_mark_pass(stats, "CreateCurve_secp256k1");

	sw = eth_read_secp256k1_status(session, &is_set);
	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "ReadECCurveList(after)", sw);
		return false;
	}

	if (!is_set) {
		se05x_demo_mark_fail_sw(stats, "Secp256k1StillNOT_SET", SM_NOT_OK);
		return false;
	}

	se05x_demo_mark_pass(stats, "Secp256k1SETAfterCreate");
	return true;
}

static bool eth_delete_test_object_if_exists(se05x_demo_stats_t *stats,
					     pSe05xSession_t se_session,
					     const char *phase)
{
	SE05x_Result_t exists = kSE05x_Result_NA;
	smStatus_t sw = Se05x_API_CheckObjectExists(se_session,
						    SE05X_DEMO_OBJECT_ID_ETH_WALLET,
						    &exists);

	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "CheckObjectExists(ETH_TEST_KEY)", sw);
		return false;
	}

	if (exists != kSE05x_Result_SUCCESS) {
		return true;
	}

	LOG_WRN("%s: deleting reserved Demo10 test object_id=0x%08" PRIX32
		"; DeleteSecureObject writes SE05x NVM metadata",
		phase != NULL ? phase : "cleanup",
		(uint32_t)SE05X_DEMO_OBJECT_ID_ETH_WALLET);

	sw = Se05x_API_DeleteSecureObject(se_session, SE05X_DEMO_OBJECT_ID_ETH_WALLET);
	if (sw != SM_OK) {
		se05x_demo_mark_fail_sw(stats, "DeleteSecureObject(ETH_TEST_KEY)", sw);
		return false;
	}

	se05x_demo_mark_pass(stats, "DeleteStaleEthTestObject");
	return true;
}

static bool eth_export_public_key_and_address(se05x_demo_stats_t *stats,
					      ex_sss_boot_ctx_t *boot_ctx,
					      sss_object_t *wallet_key,
					      uint8_t point_out[ETH_SECP256K1_PUBKEY_LEN],
					      uint8_t address_out[ETH_ADDRESS_LEN])
{
	uint8_t public_key[160] = { 0 };
	uint8_t address_hash[ETH_KECCAK256_LEN] = { 0 };
	size_t public_key_len = sizeof(public_key);
	size_t public_key_bits = 0U;
	size_t point_len = 0U;
	const uint8_t *point;
	sss_status_t status;

	status = sss_key_store_get_key(&boot_ctx->ks, wallet_key, public_key,
				       &public_key_len, &public_key_bits);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "GetPublicKey(SECP256K1)", status);
		return false;
	}

	point = eth_find_uncompressed_ec_point(public_key, public_key_len, &point_len);
	if (point == NULL) {
		se05x_demo_mark_fail_sw(stats, "ExtractPublicKey04XY", SM_NOT_OK);
		return false;
	}

	memcpy(point_out, point, ETH_SECP256K1_PUBKEY_LEN);
	eth_keccak256(&point[1], 64U, address_hash);
	memcpy(address_out, &address_hash[12], ETH_ADDRESS_LEN);

	eth_print_hex_block("ETH_PUBLIC_KEY_DER", public_key, public_key_len);
	eth_print_hex_block("ETH_PUBLIC_KEY_UNCOMPRESSED_04XY", point_out,
			    ETH_SECP256K1_PUBKEY_LEN);
	eth_print_hex_inline("ETH_FROM_ADDRESS", address_out, ETH_ADDRESS_LEN);
	se05x_demo_mark_pass(stats, "GetPublicKeyAndEthAddress");

	return true;
}

static bool eth_der_read_len(const uint8_t *der, size_t der_len, size_t *offset, size_t *out_len)
{
	if (*offset >= der_len) {
		return false;
	}

	uint8_t first = der[(*offset)++];
	if ((first & 0x80U) == 0U) {
		*out_len = first;
		return true;
	}

	size_t count = first & 0x7fU;
	if (count == 0U || count > sizeof(size_t) || *offset + count > der_len) {
		return false;
	}

	size_t value = 0;
	for (size_t i = 0; i < count; i++) {
		value = (value << 8U) | der[(*offset)++];
	}
	*out_len = value;
	return true;
}

static bool eth_copy_der_int_to_32(const uint8_t *value, size_t value_len, uint8_t out[32])
{
	while (value_len > 0U && *value == 0U) {
		++value;
		--value_len;
	}

	if (value_len > 32U) {
		return false;
	}

	memset(out, 0, 32U);
	memcpy(&out[32U - value_len], value, value_len);
	return true;
}

static bool eth_parse_ecdsa_der_rs(const uint8_t *der, size_t der_len, uint8_t r[32],
				   uint8_t s[32])
{
	size_t off = 0;
	size_t seq_len = 0;
	size_t int_len = 0;

	if (der_len < 8U || der[off++] != 0x30U ||
	    !eth_der_read_len(der, der_len, &off, &seq_len) ||
	    off + seq_len != der_len) {
		return false;
	}

	if (off >= der_len || der[off++] != 0x02U ||
	    !eth_der_read_len(der, der_len, &off, &int_len) ||
	    off + int_len > der_len ||
	    !eth_copy_der_int_to_32(&der[off], int_len, r)) {
		return false;
	}
	off += int_len;

	if (off >= der_len || der[off++] != 0x02U ||
	    !eth_der_read_len(der, der_len, &off, &int_len) ||
	    off + int_len > der_len ||
	    !eth_copy_der_int_to_32(&der[off], int_len, s)) {
		return false;
	}

	return off + int_len == der_len;
}

static bool eth_s_is_high(const uint8_t s[32])
{
	return memcmp(s, k_secp256k1_half_n, 32U) > 0;
}

static void eth_subtract_from_n(const uint8_t s[32], uint8_t out[32])
{
	int borrow = 0;

	for (int i = 31; i >= 0; i--) {
		int value = (int)k_secp256k1_n[i] - (int)s[i] - borrow;
		if (value < 0) {
			value += 256;
			borrow = 1;
		} else {
			borrow = 0;
		}
		out[i] = (uint8_t)value;
	}
}

static bool eth_sign_and_print_transaction(se05x_demo_stats_t *stats,
					   ex_sss_boot_ctx_t *boot_ctx,
					   pSe05xSession_t se_session,
					   const eth_legacy_tx_t *tx)
{
	sss_object_t wallet_key = { 0 };
	sss_asymmetric_t sign_ctx = { 0 };
	sss_asymmetric_t verify_ctx = { 0 };
	uint8_t public_point[ETH_SECP256K1_PUBKEY_LEN] = { 0 };
	uint8_t from_address[ETH_ADDRESS_LEN] = { 0 };
	uint8_t signing_rlp[ETH_MAX_RLP_LEN] = { 0 };
	uint8_t signing_hash[ETH_KECCAK256_LEN] = { 0 };
	uint8_t signature_der[128] = { 0 };
	uint8_t sig_r[32] = { 0 };
	uint8_t sig_s_raw[32] = { 0 };
	uint8_t sig_s_low[32] = { 0 };
	uint8_t raw_tx_0[ETH_MAX_RLP_LEN] = { 0 };
	uint8_t raw_tx_1[ETH_MAX_RLP_LEN] = { 0 };
	size_t signing_rlp_len = 0;
	size_t signature_der_len = sizeof(signature_der);
	size_t raw_tx_0_len = 0;
	size_t raw_tx_1_len = 0;
	uint64_t v_candidate_0;
	uint64_t v_candidate_1;
	bool s_was_high = false;
	sss_status_t status;

	if (!eth_build_signing_rlp(tx, signing_rlp, sizeof(signing_rlp), &signing_rlp_len)) {
		se05x_demo_mark_fail_sw(stats, "BuildEthSigningRLP", SM_NOT_OK);
		return false;
	}

	eth_keccak256(signing_rlp, signing_rlp_len, signing_hash);

	eth_print_current_tx(tx);
	eth_print_hex_block("ETH_SIGNING_RLP", signing_rlp, signing_rlp_len);
	eth_print_hex_inline("ETH_SIGNING_HASH_KECCAK256", signing_hash, sizeof(signing_hash));
	se05x_demo_mark_pass(stats, "BuildEthSigningPayload");

	if (!eth_delete_test_object_if_exists(stats, se_session, "before sign")) {
		return false;
	}

	status = sss_key_object_init(&wallet_key, &boot_ctx->ks);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "sss_key_object_init(ETH)", status);
		return false;
	}

	status = sss_key_object_allocate_handle(&wallet_key, SE05X_DEMO_OBJECT_ID_ETH_WALLET,
					       kSSS_KeyPart_Pair,
					       kSSS_CipherType_EC_NIST_K,
					       32, kKeyObject_Mode_Transient);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AllocateHandle(ETH_TRANSIENT)", status);
		goto cleanup;
	}
	se05x_demo_mark_pass(stats, "AllocateHandle(ETH_TRANSIENT)");

	status = sss_key_store_generate_key(&boot_ctx->ks, &wallet_key, 256, NULL);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "GenerateKey(ETH_TRANSIENT)", status);
		goto cleanup;
	}
	se05x_demo_mark_pass(stats, "GenerateKey(ETH_TRANSIENT)");

	if (!eth_export_public_key_and_address(stats, boot_ctx, &wallet_key,
					       public_point, from_address)) {
		goto cleanup;
	}

	status = sss_asymmetric_context_init(&sign_ctx, &boot_ctx->session, &wallet_key,
					     kAlgorithm_SSS_SHA256, kMode_SSS_Sign);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(SignETH)", status);
		goto cleanup;
	}

	status = sss_asymmetric_sign_digest(&sign_ctx, signing_hash, sizeof(signing_hash),
					    signature_der, &signature_der_len);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "SignDigest(ETH_KECCAK)", status);
		goto cleanup;
	}
	eth_print_hex_block("ETH_SIGNATURE_DER", signature_der, signature_der_len);
	se05x_demo_mark_pass(stats, "SignDigest(ETH_KECCAK)");

	status = sss_asymmetric_context_init(&verify_ctx, &boot_ctx->session, &wallet_key,
					     kAlgorithm_SSS_SHA256, kMode_SSS_Verify);
	if (status != kStatus_SSS_Success) {
		se05x_demo_mark_fail_status(stats, "AsymContext(VerifyETH)", status);
		goto cleanup;
	}

	status = sss_asymmetric_verify_digest(&verify_ctx, signing_hash, sizeof(signing_hash),
					      signature_der, signature_der_len);
	if (status == kStatus_SSS_Success) {
		se05x_demo_mark_pass(stats, "VerifyDigest(ETH_KECCAK)");
	} else {
		se05x_demo_mark_fail_status(stats, "VerifyDigest(ETH_KECCAK)", status);
		goto cleanup;
	}

	if (!eth_parse_ecdsa_der_rs(signature_der, signature_der_len, sig_r, sig_s_raw)) {
		se05x_demo_mark_fail_sw(stats, "ParseDerSignatureRS", SM_NOT_OK);
		goto cleanup;
	}

	s_was_high = eth_s_is_high(sig_s_raw);
	if (s_was_high) {
		eth_subtract_from_n(sig_s_raw, sig_s_low);
	} else {
		memcpy(sig_s_low, sig_s_raw, sizeof(sig_s_low));
	}

	eth_print_hex_inline("ETH_SIGNATURE_R", sig_r, sizeof(sig_r));
	eth_print_hex_inline("ETH_SIGNATURE_S_RAW", sig_s_raw, sizeof(sig_s_raw));
	eth_print_hex_inline("ETH_SIGNATURE_S_LOW", sig_s_low, sizeof(sig_s_low));
	printk("ETH_SIGNATURE_S_WAS_HIGH=%s\n", s_was_high ? "yes" : "no");
	if (tx->chain_id > (UINT64_MAX - 36ULL) / 2ULL) {
		se05x_demo_mark_fail_sw(stats, "BuildEip155VOverflow", SM_NOT_OK);
		goto cleanup;
	}
	v_candidate_0 = (tx->chain_id * 2ULL) + 35ULL;
	v_candidate_1 = (tx->chain_id * 2ULL) + 36ULL;
	printk("ETH_V_CANDIDATE_0=%" PRIu64 "\n", v_candidate_0);
	printk("ETH_V_CANDIDATE_1=%" PRIu64 "\n", v_candidate_1);
	se05x_demo_mark_pass(stats, "ParseSignatureForEthereum");

	if (!eth_build_signed_rlp(tx, raw_tx_0, sizeof(raw_tx_0), &raw_tx_0_len,
				  v_candidate_0, sig_r, sig_s_low) ||
	    !eth_build_signed_rlp(tx, raw_tx_1, sizeof(raw_tx_1), &raw_tx_1_len,
				  v_candidate_1, sig_r, sig_s_low)) {
		se05x_demo_mark_fail_sw(stats, "BuildSignedRawTransaction", SM_NOT_OK);
		goto cleanup;
	}

	eth_print_hex_inline("ETH_RAW_TX_CANDIDATE_V0", raw_tx_0, raw_tx_0_len);
	eth_print_hex_inline("ETH_RAW_TX_CANDIDATE_V1", raw_tx_1, raw_tx_1_len);
	printk("ETH_NEXT_STEP=PC or phone recovers signer address and selects the matching v.\n");
	se05x_demo_mark_pass(stats, "BuildSignedRawTransactionCandidates");

cleanup:
	if (sign_ctx.session != NULL) {
		sss_asymmetric_context_free(&sign_ctx);
	}
	if (verify_ctx.session != NULL) {
		sss_asymmetric_context_free(&verify_ctx);
	}
	sss_key_object_free(&wallet_key);
	(void)eth_delete_test_object_if_exists(stats, se_session, "after sign");

	return stats->fail == 0U;
}

static sss_status_t run_eth_wallet_sign_once(ex_sss_boot_ctx_t *boot_ctx,
					     const eth_legacy_tx_t *tx)
{
	se05x_demo_stats_t stats;
	sss_se05x_session_t *session = (sss_se05x_session_t *)&boot_ctx->session;
	pSe05xSession_t se_session = &session->s_ctx;

	se05x_demo_stats_init(&stats, "ETH_WALLET_SIGN");
	LOG_INF("ETH_WALLET_SIGN started: legacy tx RLP + Keccak + SE05x secp256k1 sign");
	LOG_WRN("This demo may write SE05x NVM once if secp256k1 is NOT_SET");

	if (eth_ensure_secp256k1_curve(&stats, se_session)) {
		(void)eth_sign_and_print_transaction(&stats, boot_ctx, se_session, tx);
	}

	se05x_demo_log_summary(&stats);
	return se05x_demo_stats_result(&stats);
}

static sss_status_t run_eth_wallet_sign(ex_sss_boot_ctx_t *boot_ctx)
{
	sss_status_t last_status = kStatus_SSS_Success;
	int err = console_init();

	if (err != 0) {
		LOG_ERR("console_init failed: %d", err);
		return kStatus_SSS_Fail;
	}

	eth_reset_tx_to_sample(&g_eth_tx);
	eth_print_menu();
	eth_print_current_tx(&g_eth_tx);

	while (1) {
		char line_storage[ETH_UART_CMD_MAX_LEN] = { 0 };
		printk("se05x-eth-demo> ");
		if (!eth_read_uart_line(line_storage, sizeof(line_storage))) {
			printk("\nERR command too long. Type AT+H for help.\n");
			continue;
		}

		char *line = eth_trim_line(line_storage);
		if (strlen(line) == 4U &&
		    (line[0] == 'A' || line[0] == 'a') &&
		    (line[1] == 'T' || line[1] == 't') &&
		    line[2] == '+') {
			const char cmd = eth_upper_command_char(line[3]);

			switch (cmd) {
			case 'H':
				printk("\nCMD AT+H -> Show menu\n");
				eth_print_menu();
				break;
			case 'P':
				printk("\nCMD AT+P -> Print current transaction\n");
				eth_print_current_tx(&g_eth_tx);
				break;
			case 'R':
				printk("\nCMD AT+R -> Reset transaction to sample values\n");
				eth_reset_tx_to_sample(&g_eth_tx);
				eth_print_current_tx(&g_eth_tx);
				break;
			case 'S':
				printk("\nCMD AT+S -> Sign current ETH legacy transaction\n");
				last_status = run_eth_wallet_sign_once(boot_ctx, &g_eth_tx);
				break;
			case 'Q':
				printk("\nCMD AT+Q -> Quit Demo 10\n");
				return last_status;
			default:
				printk("\nUnknown command. Type AT+H for help.\n");
				break;
			}
		} else if (eth_handle_set_command(line, &g_eth_tx)) {
			/* The setter already printed the updated field. */
		} else {
			printk("\nUnknown command. Type AT+H for help.\n");
		}
	}
}

const se05x_demo_t g_se05x_demo_eth_wallet_sign = {
	.id = SE05X_DEMO_ETH_WALLET_SIGN,
	.name = "eth_wallet_sign",
	.when_to_use = "Study a complete ETH legacy transfer signing path with SE05x secp256k1.",
	.flow = "Build RLP, Keccak-256 it, sign digest in SE05x, print address/r/s/v candidates.",
	.expected_output = "ETH from address, signing hash, DER signature, r/s, raw tx candidates, fail=0.",
	.se_features = "secp256k1 curve, EC_NIST_K transient key, public key export, ECDSA sign/verify.",
	.run = run_eth_wallet_sign,
};
