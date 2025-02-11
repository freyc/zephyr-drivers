#define DT_DRV_COMPAT nxp_tsi_keys

#include <errno.h>
#include <soc.h>
#include <zephyr/device.h>
//#include <zephyr/drivers/pinctrl.h>
#include <zephyr/input/input.h>
//#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
//#include <zephyr/pm/device.h>
//#include <zephyr/pm/policy.h>
//#ifdef CONFIG_SOC_SERIES_MEC172X
//#include <zephyr/drivers/clock_control/mchp_xec_clock_control.h>
//#include <zephyr/drivers/interrupt_controller/intc_mchp_xec_ecia.h>
//#endif

LOG_MODULE_REGISTER(input_tsi_keys, CONFIG_INPUT_LOG_LEVEL);


struct tsi_keys_code_config {
	uint16_t press;
    uint8_t pin;
	uint8_t key_code;
    bool lp_scan_pin;
};

struct tsi_key_state {
    bool state;
};

struct tsi_config {
    TSI_Type* base;
    void(*irq_config)(const struct device*);
    const struct tsi_keys_code_config* key_config;
    struct tsi_key_state* key_state;
    uint8_t key_count;
    uint8_t scans_per_electrode;
    uint8_t ref_charge;
    uint8_t ext_charge;
};

#define DEV_BASE(dev) (((struct tsi_config *)(dev->config))->base)

static int tsi_keys_init(const struct device* dev) {
    
    TSI_Type* base = DEV_BASE(dev);
    const struct tsi_config* config = dev->config;
    
    uint32_t pen_reg = 0u;
    uint8_t lp_scan_pin = 0xff;

    for(int i = 0; i < config->key_count; i++) {
        const struct tsi_keys_code_config* key_config = &config->key_config[i];
        if(pen_reg & (1u << key_config->pin)) {
            LOG_WRN("same pin used multiple times for different keys");
            continue;
        }

        pen_reg |= (1u << key_config->pin);

        if(key_config->lp_scan_pin) {
            if(lp_scan_pin != 0xff) {
                LOG_WRN("multiple pins configured as low-power-scan-pin");
                continue;
            } 
            lp_scan_pin = key_config->pin;
        }
    }

    if(lp_scan_pin != 0xff) {
        pen_reg |= (lp_scan_pin << TSI_PEN_LPSP_SHIFT) & TSI_PEN_LPSP_MASK;
    }

    base->PEN = pen_reg;

    base->GENCS = (base->GENCS & ~TSI_GENCS_NSCN_MASK) | ((config->scans_per_electrode << TSI_GENCS_NSCN_SHIFT) & TSI_GENCS_NSCN_MASK);

    base->GENCS |= TSI_GENCS_TSIEN_MASK;

    config->irq_config(dev);

    // enable end-of-scan-interrupt and periodical scan
    base->GENCS |= TSI_GENCS_TSIIE_MASK | TSI_GENCS_ESOR_MASK | TSI_GENCS_STM_MASK;

    return 0;
}

static void tsi_isr(const struct device *dev)
{
    const struct tsi_config* config = dev->config;
    TSI_Type* base = DEV_BASE(dev);

    uint32_t gencs = base->GENCS;
    base->GENCS = gencs & ~(TSI_GENCS_SWTS_MASK);

    if(gencs & TSI_GENCS_EOSF_MASK) {
        LOG_DBG("end-of-scan");

        //TODO: this has to be done in a work-item
        for(int i = 0; i < config->key_count; i++) {
            const struct tsi_keys_code_config* key_config = &config->key_config[i];
            uint8_t pin = config->key_config[i].pin;
            const volatile uint32_t* ctr_reg = &base->CNTR1 + (pin >> 1u);
            uint16_t ctr_value = pin & 0x01 ? *ctr_reg >> 16u : *ctr_reg;

            bool new_pressed = ctr_value > key_config->press;

            if(new_pressed != config->key_state[i].state) {
                config->key_state[i].state = new_pressed;
                input_report_key(dev, key_config->key_code, new_pressed, true, K_FOREVER);
            }
        }

#if 0
        /* If gpio changed, report the event */
        if (new_pressed != pin_data->cb_data.pin_state) {
            pin_data->cb_data.pin_state = new_pressed;
            LOG_DBG("Report event %s %d, code=%d", dev->name, new_pressed,
                pin_cfg->zephyr_code);
            input_report_key(dev, pin_cfg->zephyr_code, new_pressed, true, K_FOREVER);
        }
#endif

    }
}

#define TSI_KEYS_CODE_CFG(node_id)                              \
	{                                                           \
        .key_code = DT_PROP(node_id, zephyr_code),              \
        .pin = DT_PROP(node_id, pin),                           \
        .press = DT_PROP(node_id, press_threshold),             \
        .lp_scan_pin = DT_PROP(node_id, low_power_scan_pin),    \
    }

#define TSI_KEYS_STATE(node_id)                                 \
	{                                                           \
        .state = false,                                         \
    }

#define TSI_KEYS_INST(n)                                                                            \
	static const struct tsi_keys_code_config tsi_keys_code_cfg_##n[] = {                            \
		DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(n, TSI_KEYS_CODE_CFG, (,))};                          \
    static struct tsi_key_state tsi_key_state_##n[] = {                                             \
		DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(n, TSI_KEYS_STATE, (,))};                             \
                                                                                                    \
    static void tsi_irq_config_fun_##n (const struct device* dev);                                  \
                                                                                                    \
    static const struct tsi_config tsi_config_##n = {                                               \
        .base = (TSI_Type*)DT_INST_REG_ADDR(n),                                                     \
        .irq_config = tsi_irq_config_fun_##n,                                                       \
        .key_config = tsi_keys_code_cfg_##n,                                                        \
        .key_state = tsi_key_state_##n,                                                             \
        .key_count = ARRAY_SIZE(tsi_keys_code_cfg_##n),                                             \
        .scans_per_electrode = DT_INST_PROP(n, scans_per_electrode),                                \
        .ref_charge = DT_INST_PROP(n, ref_charge_current_ua),                                       \
        .ext_charge = DT_INST_PROP(n, ext_charge_current_ua),                                       \
    };                                                                                              \
                                                                                                    \
    static void tsi_irq_config_fun_##n (const struct device* dev) {                                 \
        IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), tsi_isr, DEVICE_DT_INST_GET(n), 0);  \
        irq_enable(DT_INST_IRQN(n));				                                                \
    }                                                                                               \
    DEVICE_DT_INST_DEFINE(n, tsi_keys_init, NULL, NULL, &tsi_config_##n,                            \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TSI_KEYS_INST)

#if 0
static const struct tsi_keys_code_config tsi_keys_code_cfg_0[] = { 
    { .key_code = 11, .pin = 0, .press = 100 } , { .key_code = 2, .pin = 2, .press = 200 }};
static const struct tsi_config tsi_config_0 = { 
    .base = 1074024448U, 
    .key_config = tsi_keys_code_cfg_0, 
    .key_count = ((size_t) (((int) sizeof(char[1 - 2 * !(!__builtin_types_compatible_p(__typeof__(tsi_keys_code_cfg_0), __typeof__(&(tsi_keys_code_cfg_0)[0])))]) - 1) + (sizeof(tsi_keys_code_cfg_0) / sizeof((tsi_keys_code_cfg_0)[0])))), 
    .scans_per_electrode = 1, 
};
static void tsi_irq_config_fun_0 (const struct device* dev) { 
    { _Static_assert((0 || !(0 & (1UL << (0)))), "" "ZLI interrupt registered but feature is disabled"); 
    _Static_assert((((0 & (1UL << (0))) && ((1 == 1) || (2 < 1))) || (2 <= ((1UL << (4)) - ((1 + 0)) - 1))), "" "Invalid interrupt priority. Values must not exceed IRQ_PRIO_LOWEST"); 
    static __attribute__((__aligned__(__alignof(struct _isr_list)))) struct _isr_list __attribute__((section(".intList"))) __attribute__((__used__)) __isr_tsi_isr_irq_4 = {83, 0, (void *)&tsi_isr, (const void *)(&__device_dts_ord_30)}; z_arm_irq_priority_set(83, 2, 0); }; } static __attribute__((__aligned__(__alignof(struct device_state)))) struct device_state __devstate_dts_ord_30 __attribute__((__section__(".z_devstate"))); _Static_assert((sizeof("\"tsi0@40045000\"") <= 48U), "" "\"tsi0@40045000\"" " too long"); const __attribute__((__aligned__(__alignof(struct device)))) struct device __device_dts_ord_30 __attribute__((section("." "_device" "." "static" "." "3_90_"))) __attribute__((__used__)) = { .name = "tsi0@40045000", .config = (&tsi_config_0), .api = (((void *)0)), .state = (&__devstate_dts_ord_30), .data = (((void *)0)), }; static const __attribute__((__aligned__(__alignof(struct init_entry)))) struct init_entry __attribute__((__used__)) __attribute__((__section__( ".z_init_" "POST_KERNEL" "90""_" "00030""_"))) __init___device_dts_ord_30 = { .init_fn = {.dev = (tsi_keys_init)}, { .dev = &__device_dts_ord_30 }, }; ;
#endif