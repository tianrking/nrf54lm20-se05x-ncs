cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf54lm20_se05x)

set(NXP_DIR "${CMAKE_CURRENT_LIST_DIR}/nxp_se05x/nxp/plug-and-trust")

add_library(se05x_bus STATIC
    se05x_bus/src/se05x_bus.c
    se05x_bus/src/se05x_bus_zephyr_i2c.c
)
add_dependencies(se05x_bus zephyr_generated_headers)

target_include_directories(se05x_bus PUBLIC
    se05x_bus/include
)

target_link_libraries(se05x_bus PUBLIC zephyr_interface)

add_library(nxp_se05x STATIC
    "${NXP_DIR}/sss/ex/src/ex_sss_boot.c"
    "${NXP_DIR}/sss/ex/src/ex_sss_boot_connectstring.c"
    "${NXP_DIR}/sss/ex/src/ex_sss_se05x.c"
    "${NXP_DIR}/sss/ex/src/ex_sss_se05x_auth.c"
    "${NXP_DIR}/sss/src/fsl_sss_apis.c"
    "${NXP_DIR}/sss/src/fsl_sss_util_asn1_der.c"
    "${NXP_DIR}/sss/src/fsl_sss_util_rsa_sign_utils.c"
    "${NXP_DIR}/sss/src/keystore/keystore_cmn.c"
    "${NXP_DIR}/sss/src/keystore/keystore_pc.c"
    "${NXP_DIR}/sss/src/se05x/fsl_sss_se05x_apis.c"
    "${NXP_DIR}/sss/src/se05x/fsl_sss_se05x_eckey.c"
    "${NXP_DIR}/sss/src/se05x/fsl_sss_se05x_mw.c"
    "${NXP_DIR}/sss/src/se05x/fsl_sss_se05x_policy.c"
    "${NXP_DIR}/sss/src/se05x/fsl_sss_se05x_scp03.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/infra/global_platf.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/infra/sm_apdu.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/infra/sm_connect.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/infra/sm_errors.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/infra/sm_printf.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/log/nxLog.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/nxScp/nxScp03_Com.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom/smCom.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom/smComT1oI2C.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom/T1oI2C/phNxpEse_Api.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom/T1oI2C/phNxpEseProto7816_3.c"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom/T1oI2C/phNxpEsePal_i2c.c"
    "${NXP_DIR}/hostlib/hostLib/se05x/src/se05x_ECC_curves.c"
    "${NXP_DIR}/hostlib/hostLib/se05x/src/se05x_mw.c"
    "${NXP_DIR}/hostlib/hostLib/se05x/src/se05x_tlv.c"
    "${NXP_DIR}/hostlib/hostLib/se05x_03_xx_xx/se05x_APDU.c"
    "${NXP_DIR}/hostlib/hostLib/se05x_03_xx_xx/se05x_perso_api.c"
    nxp_se05x/port/i2c_a7_zephyr_bus.c
    nxp_se05x/port/sm_timer_zephyr.c
    nxp_se05x/port/ax_reset_zephyr.c
    nxp_se05x/port/sss_user_zephyr_crypto.c
    nxp_se05x/port/freertos_zephyr_shim.c
)
add_dependencies(nxp_se05x zephyr_generated_headers)

target_include_directories(nxp_se05x PUBLIC
    nxp_se05x/include
    nxp_se05x/port/include
    "${NXP_DIR}"
    "${NXP_DIR}/sss/inc"
    "${NXP_DIR}/sss/ex/inc"
    "${NXP_DIR}/sss/ex/src"
    "${NXP_DIR}/sss/port/default"
    "${NXP_DIR}/hostlib/hostLib/inc"
    "${NXP_DIR}/hostlib/hostLib/libCommon/infra"
    "${NXP_DIR}/hostlib/hostLib/libCommon/log"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom"
    "${NXP_DIR}/hostlib/hostLib/libCommon/smCom/T1oI2C"
    "${NXP_DIR}/hostlib/hostLib/platform/inc"
    "${NXP_DIR}/hostlib/hostLib/se05x_03_xx_xx"
)

target_compile_definitions(nxp_se05x PUBLIC
    SSS_USE_FTR_FILE
    SMCOM_T1oI2C
    T1oI2C
    T1oI2C_UM11225
    T1OI2C_RETRY_ON_I2C_FAILED
    USE_RTOS=1
    AX_EMBEDDED=1
)

target_compile_options(nxp_se05x PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wno-format
    -Wno-incompatible-pointer-types
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-value
)

target_link_libraries(nxp_se05x PUBLIC se05x_bus zephyr_interface)

target_sources(app PRIVATE
    src/main.c
    demo/se05x_demo.c
    demo/se05x_demo_01_safe_read_only.c
    demo/se05x_demo_02_identity_random.c
    demo/se05x_demo_03_inventory.c
    demo/se05x_demo_04_business_onboarding.c
    demo/se05x_demo_05_provisioning_check.c
    demo/se05x_demo_06_ecc_sign_verify.c
    demo/se05x_demo_07_certificate_store.c
    demo/se05x_demo_08_tls_client_identity.c
)

target_include_directories(app PRIVATE demo)
target_link_libraries(app PRIVATE nxp_se05x se05x_bus)
