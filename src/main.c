#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ex_sss_boot.h"
#include "se05x_bus.h"
#include "se05x_bus_zephyr.h"
#include "se05x_demo.h"

LOG_MODULE_REGISTER(nrf54lm20_se05x, LOG_LEVEL_INF);

/*
 * Demo 选择入口。
 *
 * main.c 只负责初始化 transport、打开 SE05x Platform SCP03 session，
 * 然后根据 APP_SELECTED_DEMO 分发到 demo/ 目录下的具体示例。
 *
 * 后续切换示例时，只改下面这一行即可，不需要改 main() 主流程。
 *
 *   SE05X_DEMO_UART_SAFE_API       - 00：UART 交互式安全 API 菜单。
 *   SE05X_DEMO_SAFE_READ_ONLY      - 01：完整只读冒烟测试。
 *   SE05X_DEMO_IDENTITY_RANDOM     - 02：读取身份信息和随机数。
 *   SE05X_DEMO_INVENTORY           - 03：查看能力、保留对象和存储空间。
 *   SE05X_DEMO_BUSINESS_ONBOARDING - 04：设备注册/产测上报前置流程。
 *   SE05X_DEMO_PROVISIONING_CHECK  - 05：应用 key/证书写入前预检流程。
 *   SE05X_DEMO_ECC_SIGN_VERIFY     - 06：写入 demo ECC 私钥并做签名验签。
 *   SE05X_DEMO_CERTIFICATE_STORE   - 07：写入 demo 设备证书并回读校验。
 *   SE05X_DEMO_TLS_CLIENT_IDENTITY - 08：用 06/07 对象模拟 TLS 客户端身份。
 *   SE05X_DEMO_WALLET_CURVE_CHECK  - 09：验证 secp256k1 曲线和签名能力。
 *   SE05X_DEMO_ETH_WALLET_SIGN     - 10：ETH legacy transfer 签名流程研究。
 *   SE05X_DEMO_ETH_TESTNET_WALLET  - 11：ETH Sepolia 持久化钱包签名流程。
 */
#define APP_SELECTED_DEMO SE05X_DEMO_ETH_TESTNET_WALLET

static ex_sss_boot_ctx_t s_boot_ctx;
static se05x_bus_t s_i2c_bus;

static int app_register_transport(void)
{
	/*
	 * 从 devicetree alias `se05x` 创建 Zephyr I2C backend。
	 * 当前 overlay 将 SE05x 绑定到 i2c22，地址 0x48。
	 */
	int err = se05x_zephyr_i2c_bus_create(&s_i2c_bus, NULL);

	if (err != 0) {
		LOG_ERR("se05x_zephyr_i2c_bus_create failed: %d", err);
		return err;
	}

	/*
	 * NXP T=1 over I2C porting 层通过默认 bus 访问 SE05x。
	 * 必须在 ex_sss_boot_open() 之前完成注册。
	 */
	err = se05x_bus_register_default(&s_i2c_bus.ops);
	if (err != 0) {
		LOG_ERR("se05x_bus_register_default failed: %d", err);
		return err;
	}

	return 0;
}

static int app_open_se_session(void)
{
	sss_status_t status;

	/*
	 * 打开 SE05x session。
	 * 当前 fsl_sss_ftr.h 选择 Platform SCP03，所以这里会同时完成
	 * I2C/T=1 连接、ATR 读取和 SCP03 安全通道认证。
	 */
	memset(&s_boot_ctx, 0, sizeof(s_boot_ctx));
	status = ex_sss_boot_open(&s_boot_ctx, NULL);
	if (status != kStatus_SSS_Success) {
		LOG_ERR("ex_sss_boot_open failed: status=0x%04X", (unsigned int)status);
		LOG_ERR("Platform SCP03 open failed; abort all SE05x demos");
		return -EIO;
	}

	/*
	 * 初始化 SSS key store/object 上下文。
	 * 只读 demo 对它依赖不强，但 key 生成、签名、证书类 demo 需要它。
	 */
	status = ex_sss_key_store_and_object_init(&s_boot_ctx);
	if (status != kStatus_SSS_Success) {
		LOG_WRN("ex_sss_key_store_and_object_init skipped: status=0x%04X",
			(unsigned int)status);
	}

	return 0;
}

int main(void)
{
	const se05x_demo_t *demo = se05x_demo_find(APP_SELECTED_DEMO);
	sss_status_t status;
	int err;

	LOG_INF("nRF54LM20 + NXP SE05x demo runner started");
	LOG_INF("Compiled SCP03 profile: %s", se05x_demo_active_scp03_profile());
	se05x_demo_log_catalog();
	se05x_demo_log_selection(demo);

	if (demo == NULL || demo->run == NULL) {
		return -EINVAL;
	}

	err = app_register_transport();
	if (err != 0) {
		return err;
	}

	err = app_open_se_session();
	if (err != 0) {
		return err;
	}

	status = demo->run(&s_boot_ctx);
	if (status == kStatus_SSS_Success) {
		LOG_INF("Demo %s overall result: OK", demo->name);
	} else {
		LOG_ERR("Demo %s overall result: FAILED", demo->name);
	}

	/*
	 * demo 运行完成后关闭 SE05x session。
	 * 主线程随后 sleep，方便串口日志停留和 debugger 继续查看现场。
	 */
	ex_sss_session_close(&s_boot_ctx);

	while (1) {
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
