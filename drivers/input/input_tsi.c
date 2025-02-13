#define DT_DRV_COMPAT nxp_tsi_keys

#include <errno.h>
#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>

#include <zephyr/input/input.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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
	TSI_Type *base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	const struct pinctrl_dev_config *pincfg;
	void (*irq_config)(const struct device *);
	const struct tsi_keys_code_config *key_config;
	struct tsi_key_state *key_state;
	uint8_t key_count;
	uint8_t scans_per_electrode;
	uint8_t prescaler;
	uint8_t ref_charge;
	uint8_t ext_charge;
	uint8_t clk_source;
};

struct tsi_data {
	const struct device *dev;
	struct k_work work;
};

#define DEV_BASE(dev) (((struct tsi_config *)(dev->config))->base)

static void tsi_process(struct k_work *work)
{
	struct tsi_data *data = CONTAINER_OF(work, struct tsi_data, work);
	const struct device *dev = data->dev;
}

static int tsi_keys_init(const struct device *dev)
{

	TSI_Type *base = DEV_BASE(dev);
	const struct tsi_config *config = dev->config;
	struct tsi_data *data = dev->data;

	data->dev = dev;
	k_work_init(&data->work, tsi_process);

	int error = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (error) {
		LOG_ERR("could not setup pinctrl");
		return error;
	}

	clock_control_on(config->clock_dev, config->clock_subsys);

	uint32_t pen_reg = 0u;
	uint8_t lp_scan_pin = 0xff;

	for (int i = 0; i < config->key_count; i++) {
		const struct tsi_keys_code_config *key_config = &config->key_config[i];
		if (pen_reg & (1u << key_config->pin)) {
			LOG_WRN("same pin used multiple times for different keys");
			continue;
		}

		pen_reg |= (1u << key_config->pin);

		if (key_config->lp_scan_pin) {
			if (lp_scan_pin != 0xff) {
				LOG_WRN("multiple pins configured as low-power-scan-pin");
				continue;
			}
			lp_scan_pin = key_config->pin;
		}
	}

	if (lp_scan_pin != 0xff) {
		pen_reg |= (lp_scan_pin << TSI_PEN_LPSP_SHIFT) & TSI_PEN_LPSP_MASK;
	}

	base->PEN = pen_reg;

	uint32_t gencs = base->GENCS;
	gencs &= ~(TSI_GENCS_NSCN_MASK | TSI_GENCS_PS_MASK);
	gencs |= (config->scans_per_electrode << TSI_GENCS_NSCN_SHIFT) & TSI_GENCS_NSCN_MASK;
	gencs |= (config->prescaler << TSI_GENCS_PS_SHIFT) & TSI_GENCS_PS_MASK;

	base->GENCS = gencs; //(base->GENCS & ~TSI_GENCS_NSCN_MASK) | ((config->scans_per_electrode
			     //<< TSI_GENCS_NSCN_SHIFT) & TSI_GENCS_NSCN_MASK);

	config->irq_config(dev);

	// enable end-of-scan-interrupt and periodical scan
	base->GENCS |= TSI_GENCS_TSIIE_MASK | TSI_GENCS_ESOR_MASK | TSI_GENCS_STM_MASK;

	base->GENCS |= TSI_GENCS_TSIEN_MASK;

	return 0;
}

static void tsi_isr(const struct device *dev)
{
	const struct tsi_config *config = dev->config;
	TSI_Type *base = DEV_BASE(dev);

	uint32_t gencs = base->GENCS;
	base->GENCS = gencs & ~(TSI_GENCS_SWTS_MASK);
#if 1
	if (gencs & TSI_GENCS_EOSF_MASK) {
		// LOG_DBG("end-of-scan");

		// TODO: this has to be done in a work-item
		for (int i = 0; i < config->key_count; i++) {
			const struct tsi_keys_code_config *key_config = &config->key_config[i];
			uint8_t pin = config->key_config[i].pin;
			const volatile uint32_t *ctr_reg = &base->CNTR1 + (pin >> 1u);
			uint16_t ctr_value = pin & 0x01 ? *ctr_reg >> 16u : *ctr_reg;

			bool new_pressed = ctr_value > key_config->press;

			if (new_pressed != config->key_state[i].state) {
				config->key_state[i].state = new_pressed;
				input_report_key(dev, key_config->key_code, new_pressed, true,
						 K_FOREVER);
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
#endif
}

#define TSI_KEYS_CODE_CFG(node_id)                                                                 \
	{                                                                                          \
		.key_code = DT_PROP(node_id, zephyr_code),                                         \
		.pin = DT_PROP(node_id, channel),                                                  \
		.press = DT_PROP(node_id, press_threshold),                                        \
		.lp_scan_pin = DT_PROP(node_id, low_power_scan_pin),                               \
	}

#define TSI_KEYS_STATE(node_id)                                                                    \
	{                                                                                          \
		.state = false,                                                                    \
	}

#define TSI_DT_INST_CLOCK_SUBSYS(n)                                                                \
	CLK_GATE_DEFINE(DT_INST_CLOCKS_CELL(n, offset), DT_INST_CLOCKS_CELL(n, bits))

#define TSI_KEYS_INST(n)                                                                           \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static const struct tsi_keys_code_config tsi_keys_code_cfg_##n[] = {                       \
		DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(n, TSI_KEYS_CODE_CFG, (, ))};                \
	static struct tsi_key_state tsi_key_state_##n[] = {                                        \
		DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(n, TSI_KEYS_STATE, (, ))};                   \
                                                                                                   \
	static void tsi_irq_config_fun_##n(const struct device *dev);                              \
                                                                                                   \
	static const struct tsi_config tsi_config_##n = {                                          \
		.base = (TSI_Type *)DT_INST_REG_ADDR(n),                                           \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_subsys = (clock_control_subsys_t)TSI_DT_INST_CLOCK_SUBSYS(n),               \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.irq_config = tsi_irq_config_fun_##n,                                              \
		.key_config = tsi_keys_code_cfg_##n,                                               \
		.key_state = tsi_key_state_##n,                                                    \
		.key_count = ARRAY_SIZE(tsi_keys_code_cfg_##n),                                    \
		.scans_per_electrode = DT_INST_PROP(n, scans_per_electrode),                       \
		.prescaler = DT_INST_ENUM_IDX(n, prescaler),                                       \
		.ref_charge = DT_INST_PROP(n, ref_charge_current_ua),                              \
		.ext_charge = DT_INST_PROP(n, ext_charge_current_ua),                              \
		.clk_source = DT_INST_PROP(n, clk_source),                                         \
	};                                                                                         \
                                                                                                   \
	static struct tsi_data tsi_data_##n;                                                       \
                                                                                                   \
	static void tsi_irq_config_fun_##n(const struct device *dev)                               \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), tsi_isr,                    \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
	DEVICE_DT_INST_DEFINE(n, tsi_keys_init, NULL, &tsi_data_##n, &tsi_config_##n, POST_KERNEL, \
			      CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TSI_KEYS_INST)
