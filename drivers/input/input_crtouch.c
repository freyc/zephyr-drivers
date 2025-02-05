#define DT_DRV_COMPAT nxp_crtouch


#include <zephyr/input/input.h>
#include <zephyr/input/input_touch.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(crtouch, CONFIG_INPUT_LOG_LEVEL);

struct crtouch_config {
    struct i2c_dt_spec bus;
    struct gpio_dt_spec reset;
    struct gpio_dt_spec wakeup;
#ifdef CONFIG_INPUT_CRTOUCH_INTERRUPT
	/** Interrupt GPIO information. */
	struct gpio_dt_spec int_gpio;
#endif
};

struct crtouch_data {
    /** Device pointer. */
	const struct device *dev;
	/** Work queue (for deferred read). */
	struct k_work work;
#ifdef CONFIG_INPUT_CRTOUCH_INTERRUPT
	/** Interrupt GPIO callback. */
	struct gpio_callback int_gpio_cb;
#else
	/** Timer (polling mode). */
	struct k_timer timer;
#endif
	/** Last pressed state. */
	bool pressed_old;
};

#define REG_RT_STATUS_1 0x01
#define REG_X_MSB       0x03
#define REG_RT_TRIGGER  0x41

static int crtouch_process(const struct device* dev) {
    struct crtouch_data* data = dev->data;
    const struct crtouch_config* config = dev->config;

    int r;
    uint8_t status1;
    bool pressed;
    uint8_t coords[4U];

    r = i2c_reg_read_byte_dt(&config->bus, REG_RT_STATUS_1, &status1);
    if(r < 0) {
        return r;
    }

    pressed = status1 & 0x80;

    if (pressed) {

        r = i2c_burst_read_dt(&config->bus, REG_X_MSB, coords, sizeof(coords));
		if (r < 0) {
			return r;
		}

		uint16_t x = ((coords[0]) << 8U) | coords[1];
		uint16_t y = ((coords[2]) << 8U) | coords[3];

		//uint8_t touch_id = FIELD_GET(TOUCH_ID_MSK, coords[2]);

		input_touchscreen_report_pos(dev, x, y, K_FOREVER);
		input_report_key(dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);
	} else if (data->pressed_old && !pressed) {
		input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);
	}

    data->pressed_old = pressed;
    return 0;
}

static void crtouch_work_handler(struct k_work *work) {
    struct crtouch_data* data = CONTAINER_OF(work, struct crtouch_data, work);
    crtouch_process(data->dev);
}

#ifdef CONFIG_INPUT_CRTOUCH_INTERRUPT
static void crtouch_irq_handler(const struct device *dev,
			       struct gpio_callback *cb, uint32_t pins)
{
	struct crtouch_data *data = CONTAINER_OF(cb, struct crtouch_data, int_gpio_cb);

	k_work_submit(&data->work);
}
#else
static void crtouch_timer_handler(struct k_timer *timer) {
    struct crtouch_data* data = CONTAINER_OF(timer, struct crtouch_data, timer);
    k_work_submit(&data->work);
}
#endif

static int crtouch_init(const struct device* dev) {
    const struct crtouch_config* config = dev->config;
    struct crtouch_data* data = dev->data;
    int rc;

    if (!device_is_ready(config->bus.bus)) {
        LOG_ERR("i2c-bus not ready");
		return -ENODEV;
	}

    data->dev = dev;

	k_work_init(&data->work, crtouch_work_handler);

    if(config->reset.port) {
        rc = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        
        if(rc < 0) {
            LOG_ERR("could not initialise reset-gpio");
            return rc;
        }

        k_msleep(1);
    
        rc = gpio_pin_set_dt(&config->reset, 1);
    }

#ifdef CONFIG_INPUT_CRTOUCH_INTERRUPT
    if (!gpio_is_ready_dt(&config->int_gpio)) {
		LOG_ERR("Interrupt GPIO controller device not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	if (rc < 0) {
		LOG_ERR("Could not configure interrupt GPIO pin");
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&config->int_gpio,
					    GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		LOG_ERR("Could not configure interrupt GPIO interrupt.");
		return rc;
	}

	gpio_init_callback(&data->int_gpio_cb, crtouch_irq_handler,
			   BIT(config->int_gpio.pin));
	rc = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
	if (rc < 0) {
		LOG_ERR("Could not set gpio callback");
		return rc;
	}
#else
    k_timer_init(&data->timer, crtouch_timer_handler, NULL);
	k_timer_start(&data->timer, K_MSEC(CONFIG_INPUT_CRTOUCH_PERIOD),
		      K_MSEC(CONFIG_INPUT_CRTOUCH_PERIOD));
#endif

    LOG_INF("initialised");

    return 0;
}

#define CRTOUCH_INIT(inst)          \
    static const struct crtouch_config crtouch_config_##inst = {    \
        .bus = I2C_DT_SPEC_INST_GET(inst),                          \
        .reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),  \
        .int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),  \
    };                                                              \
    static struct crtouch_data crtouch_data_##inst = {              \
    };                                                              \
                                                                    \
    DEVICE_DT_DEFINE(                                               \
        DT_DRV_INST(inst),                                          \
        &crtouch_init,                                              \
        NULL,\
        &crtouch_data_##inst,\
        &crtouch_config_##inst,\
        POST_KERNEL,\
        CONFIG_INPUT_INIT_PRIORITY,\
        NULL \
    );

DT_INST_FOREACH_STATUS_OKAY(CRTOUCH_INIT)
