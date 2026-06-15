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
 * 这个 main.c 只做三件事：
 *   1. 初始化 nRF54LM20 到 SE05x 的 I2C 传输层。
 *   2. 通过 Platform SCP03 打开到 SE05x 的安全会话。
 *   3. 根据 APP_SELECTED_DEMO 分发到 demo/ 目录下的某一个示例。
 *
 * 后续要切换示例时，只改下面这一行即可，不需要改 main() 的流程。
 *
 *   SE05X_DEMO_SAFE_READ_ONLY   - 01 号示例：最完整的只读冒烟测试。
 *   SE05X_DEMO_IDENTITY_RANDOM  - 02 号示例：快速读取身份信息和随机数。
 *   SE05X_DEMO_INVENTORY        - 03 号示例：查看能力、保留对象和存储空间。
 *   SE05X_DEMO_BUSINESS_ONBOARDING - 04 号示例：真实设备注册/产测上报前置流程。
 *   SE05X_DEMO_PROVISIONING_CHECK  - 05 号示例：应用 key/证书写入前预检流程。
 *   SE05X_DEMO_ECC_SIGN_VERIFY     - 06 号示例：写入 demo ECC 私钥并做签名验签。
 *   SE05X_DEMO_CERTIFICATE_STORE   - 07 号示例：写入 demo 设备证书并回读校验。
 *   SE05X_DEMO_TLS_CLIENT_IDENTITY - 08 号示例：用 06/07 的对象模拟 TLS 客户端身份。
 *
 * 每个具体 demo 都放在 demo/se05x_demo_xx_*.c 里。文件顶部有中文说明：
 *   - 适合什么情况使用
 *   - 测试流程是什么
 *   - 串口上期望看到什么输出
 *   - 用到了 SE05x 的哪些功能/APDU
 *   - 是否会写入 SE05x NVM
 */
#define APP_SELECTED_DEMO SE05X_DEMO_SAFE_READ_ONLY

static ex_sss_boot_ctx_t s_boot_ctx;
static se05x_bus_t s_i2c_bus;

static int app_register_transport(void)
{
	/*
	 * 创建 Zephyr I2C 传输对象。
	 *
	 * 当前工程的 boards 目录下的 overlay 已经把 SE05x 绑定到 i2c22，地址 0x48。
	 * se05x_zephyr_i2c_bus_create() 会从 devicetree 取出这个节点，并确认
	 * I2C 控制器 ready。这里失败通常说明 overlay、管脚、供电或 I2C 外设
	 * 没准备好。
	 */
	int err = se05x_zephyr_i2c_bus_create(&s_i2c_bus, NULL);

	if (err != 0) {
		LOG_ERR("se05x_zephyr_i2c_bus_create failed: %d", err);
		return err;
	}

	/*
	 * NXP Plug & Trust hostlib 里面的 T=1 over I2C 层会通过一个“默认 bus”
	 * 访问 SE05x。这里把刚创建好的 Zephyr I2C bus 注册进去，后续
	 * ex_sss_boot_open() 和各个 APDU API 才知道该走哪条 I2C 链路。
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
	 * 打开 SE05x 会话。
	 *
	 * 当前 fsl_sss_ftr.h 选择的是 Platform SCP03，所以这一步不仅是“连上
	 * SE05x”，还会完成 SCP03 安全通道认证。之前遇到的 PSA RNG 没打开、
	 * SCP03 key/profile 不匹配，都会在这里失败。
	 */
	memset(&s_boot_ctx, 0, sizeof(s_boot_ctx));
	status = ex_sss_boot_open(&s_boot_ctx, NULL);
	if (status != kStatus_SSS_Success) {
		LOG_ERR("ex_sss_boot_open failed: status=0x%04X", (unsigned int)status);
		LOG_ERR("Platform SCP03 没有打开成功，不能继续运行任何 SE05x demo");
		return -EIO;
	}

	/*
	 * 初始化 SSS key store/object 上下文。
	 *
	 * 只读 demo 对它的依赖不重，但后续如果增加“创建 AES key”“ECC 签名”
	 * 等示例，会需要这个对象管理上下文。这里失败先记 warning，避免影响
	 * 当前已经能跑的只读示例。
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

	LOG_INF("nRF54LM20 + NXP SE05x demo 运行器启动");
	LOG_INF("当前编译选择的 SCP03 profile: %s", se05x_demo_active_scp03_profile());
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
		LOG_INF("Demo %s 总体结果：OK", demo->name);
	} else {
		LOG_ERR("Demo %s 总体结果：FAILED", demo->name);
	}

	/*
	 * 当前 demo 都是一次性执行。执行完后关闭 SE05x session，然后让主线程
	 * 睡眠保活，方便串口日志停留，也方便 debugger 连接后查看状态。
	 */
	ex_sss_session_close(&s_boot_ctx);

	while (1) {
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
