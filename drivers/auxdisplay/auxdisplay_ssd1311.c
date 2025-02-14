

#include "auxdisplay_ssd1311.h"

#include <zephyr/drivers/auxdisplay.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ssd1311, CONFIG_AUXDISPLAY_LOG_LEVEL);

struct ssd1311_config {
	union ssd1311_bus_cfg bus;
	const struct ssd1311_ops *ops;
	struct gpio_dt_spec reset;
	struct auxdisplay_capabilities capabilities;
	int font_width;
	bool invert_cursor;
};

struct ssd1311_data {
	uint32_t display_on: 1;
	uint32_t cursor_on: 1;
	uint32_t blink_on: 1;
	uint8_t contrast;
};

static inline int ssd1311_send_cmd(const struct device *dev, uint8_t cmd)
{
	const struct ssd1311_config *config = dev->config;
	return config->ops->write_cmd(&config->bus, cmd);
}

static inline int ssd1311_send_data(const struct device *dev, uint8_t data)
{
	const struct ssd1311_config *config = dev->config;
	return config->ops->write_data(&config->bus, data);
}

static inline int ssd1311_write_batch(const struct device *dev, const uint8_t *data, uint16_t len) {
	const struct ssd1311_config *config = dev->config;
	return config->ops->write_batch_data(&config->bus, data, len);
}


static int ssd1311_display_ctrl(const struct device *dev)
{
	struct ssd1311_data *data = dev->data;
#if 0
    const struct ssd1311_config* config = dev->config;
    uint8_t cmd[2] = {0x80, 0x08};

    cmd[1] |= (data->display_on << 2u) | (data->cursor_on << 1u) | (data->blink_on << 0u);

    return i2c_write_dt(&config->bus, cmd, sizeof(cmd));
#else
	uint8_t cmd =
		0x08 | (data->display_on << 2u) | (data->cursor_on << 1u) | (data->blink_on << 0u);
	return ssd1311_send_cmd(dev, cmd);
#endif
}

static int ssd1311_display_on(const struct device *dev)
{
	struct ssd1311_data *data = dev->data;
	data->display_on = 1u;
	return ssd1311_display_ctrl(dev);
}

static int ssd1311_display_off(const struct device *dev)
{
	struct ssd1311_data *data = dev->data;
	data->display_on = 0u;
	return ssd1311_display_ctrl(dev);
}

static int ssd1311_cursor_set_enabled(const struct device *dev, bool enabled)
{
	struct ssd1311_data *data = dev->data;

	data->cursor_on = enabled ? 1u : 0u;
	return ssd1311_display_ctrl(dev);
}

static int ssd1311_position_blinking_set_enabled(const struct device *dev, bool enabled)
{
	struct ssd1311_data *data = dev->data;

	data->blink_on = enabled ? 1u : 0u;
	return ssd1311_display_ctrl(dev);
}

static int ssd1311_cursor_shift_set(const struct device *dev, uint8_t direction, bool display)
{
	// const struct ssd1311_config* config = dev->config;

	uint8_t cmd = 0x10 | (direction == AUXDISPLAY_DIRECTION_RIGHT ? 0x04 : 0x00) |
		      (display ? 0x08 : 0x00);
	return ssd1311_send_cmd(dev, cmd);
}

static int ssd1311_cursor_position_set(const struct device *dev, enum auxdisplay_position type,
				       int16_t x, int16_t y)
{
	if (type != AUXDISPLAY_POSITION_ABSOLUTE) {
		return -ENOTSUP;
	}

	if (x >= 20 || y >= 4) {
		return -EINVAL;
	}

#if 0
	uint8_t pos_cmd[] = {0x80, 0x80};

	pos_cmd[1] += (y * 0x20 + x);
	int rc = i2c_write_dt(&config->bus, pos_cmd, sizeof(pos_cmd));
#else
	int rc = ssd1311_send_cmd(dev, 0x80 + (y * 0x20 + x));
#endif
	return rc;
}

static int ssd1311_capabilities_get(const struct device *dev, struct auxdisplay_capabilities *caps)
{
	const struct ssd1311_config *config = dev->config;
	memcpy(caps, &config->capabilities, sizeof(struct auxdisplay_capabilities));
	return 0;
}

static int ssd1311_clear(const struct device *dev)
{
	return ssd1311_send_cmd(dev, 0x01);
}

static int ssd1311_brightness_get(const struct device *dev, uint8_t *brightness)
{
	struct ssd1311_data *data = dev->data;
	*brightness = data->contrast;
	return 0;
}

static int ssd1311_brightness_set(const struct device *dev, uint8_t brightness)
{

	struct ssd1311_data *data = dev->data;
	int rc = 0;

	data->contrast = brightness;

	ssd1311_send_cmd(dev, 0x2a);
	ssd1311_send_cmd(dev, 0x79);
	ssd1311_send_cmd(dev, 0x81);
	ssd1311_send_cmd(dev, brightness);

	ssd1311_send_cmd(dev, 0x78);
	rc = ssd1311_send_cmd(dev, 0x28);

	return rc;
}

static int ssd1311_custom_character_set(const struct device *dev,
					struct auxdisplay_character *character)
{
	const struct ssd1311_config *config = dev->config;

	if (character->index >= config->capabilities.custom_characters) {
		return -EINVAL;
	}

	uint8_t cgram_addr = character->index << 3u;
	ssd1311_send_cmd(dev, 0x40 | cgram_addr);

	for (int i = 0; i < config->capabilities.custom_character_height; i++) {
		ssd1311_send_data(dev, character->data[i] & 0x1f);
	}

	character->character_code = character->index;
	return 0;
}

static int ssd1311_write(const struct device *dev, const uint8_t *data, uint16_t len)
{
#if 0
	struct i2c_msg msgs[2];

	uint8_t msg[] = {0x40};
	msgs[0].flags = I2C_MSG_WRITE;
	msgs[0].buf = msg;
	msgs[0].len = sizeof(msg);

	msgs[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;
	msgs[1].buf = (uint8_t *)data;
	msgs[1].len = len;

	const struct ssd1311_config *config = dev->config;
	return i2c_transfer_dt(&config->bus, msgs, 2);
#else
	return ssd1311_write_batch(dev, data, len);
#endif
}

static int ssd1311_custom_command(const struct device *dev, struct auxdisplay_custom_data *command)
{
	//const struct ssd1311_config *config = dev->config;
	//return i2c_write_dt(&config->bus, command->data, command->len);
	return -ENOTSUP;
}

static int ssd1311_init(const struct device *dev)
{
	const struct ssd1311_config *config = dev->config;
	const struct ssd1311_data *data = dev->data;
	int rc;

	if (config->capabilities.rows > 4 || config->capabilities.rows < 1) {
		LOG_ERR("invalid number of rows (%d); must be 1...4", config->capabilities.rows);
		return -EINVAL;
	}

	if (!config->ops->check_bus(&config->bus)) {
		LOG_ERR("bus not ready");
		return -ENODEV;
	}

	if (config->reset.port) {
		rc = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_ACTIVE);

		if (rc < 0) {
			LOG_ERR("could not initialise reset-gpio");
			return rc;
		}

		k_msleep(1);

		rc = gpio_pin_set_dt(&config->reset, 0);

		k_msleep(1);
	}

#if 0
    // untested code!!

    //https://github.com/iggymayer/SSD1311/blob/main/src/SSD1311.cpp
    //https://github.com/jafrado/2004_i2c_oled/blob/master/ssd13xx_20x4_oled.c
    // send 0x2a -> set RE bit
    // send 0x28 -> clr RE bit
    // send 0x79 -> enable oled cmd set
    // send 0x78 -> disable oled cmd set
    
    // extended function set
    ssd1311_send_cmd(dev, 0x22 | ((config->lines == 2 || config->lines == 4) ? 0x08 : 0x00) /*0x2a*/);

    /*
        NW  N
        0   0   1 line
        0   1   2 lines
        1   0   3 lines
        1   1   4 lines
    */

    uint8_t cmd = 0x08;

    if(config->phys_rows == 3 || config->phys_rows == 4)
    {
        cmd |= 0x01;
    }

    // if 6dot-font |= 0x04
    if(config->font_width == 6) {
        cmd |= 0x04;
    }

    // if invert_cursor |= 0x02
    if(config->invert_cursor) {
        cmd |= 0x02;
    }
    
    ssd1311_send_cmd(dev, cmd);
/*
    cmd = 0x10; // 1-1-2
    cmd = 0x14; // 1-2-1
    cmd = 0x18; // 2-2
    cmd = 0x1C; // 2-1-1
*/
    //cmd = 0x18; // 2-2
    //ssd1311_send_cmd(dev, cmd);

    //ssd1311_send_cmd(dev, 0x28); 
    //0x04 -> DH (double height)
    ssd1311_send_cmd(dev, /*0x04 |*/ 0x20 | ((config->lines == 2 || config->lines == 4) ? 0x08 : 0x00));
#endif

	rc = ssd1311_clear(dev);
	rc = ssd1311_clear(dev);
	if(rc < 0) {
		LOG_ERR("could not clear display");
		return -ENODEV;
	}

	return ssd1311_brightness_set(dev, data->contrast);
	//return 0;
}

static DEVICE_API(auxdisplay, ssd1311_api) = {
	.display_on = ssd1311_display_on,
	.display_off = ssd1311_display_off,
	.cursor_set_enabled = ssd1311_cursor_set_enabled,
	.position_blinking_set_enabled = ssd1311_position_blinking_set_enabled,
	.cursor_shift_set = ssd1311_cursor_shift_set,
	.cursor_position_set = ssd1311_cursor_position_set,
	.cursor_position_get = NULL,
	.display_position_set = NULL,
	.display_position_get = NULL,
	.capabilities_get = ssd1311_capabilities_get,
	.clear = ssd1311_clear,
	.brightness_get = ssd1311_brightness_get,
	.brightness_set = ssd1311_brightness_set,
	.backlight_get = NULL,
	.backlight_set = NULL,
	.is_busy = NULL,
	.custom_character_set = ssd1311_custom_character_set,
	.write = ssd1311_write,
	.custom_command = ssd1311_custom_command};

#define SSD1311_I2C_CONFIG(inst)                                                                   \
	.bus = {.i2c = I2C_DT_SPEC_INST_GET(inst)}, \
	.ops = &ssd1311_i2c_ops,

#define SSD1311_SPI_CONFIG(inst)                                                                   \
	.bus = {.spi = SPI_DT_SPEC_INST_GET(inst, (SPI_WORD_SET(8) | SPI_TRANSFER_LSB | SPI_MODE_CPOL | SPI_MODE_CPHA), 0) }, \
	.ops = &ssd1311_spi_ops,

#define SSD1311_INIT(inst)                                                                         \
	static const struct ssd1311_config ssd1311_config_##inst = {                               \
		COND_CODE_1(DT_INST_ON_BUS(inst, spi), 						   \
		(SSD1311_SPI_CONFIG(inst)),							   \
		(SSD1311_I2C_CONFIG(inst))) \
		.reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                \
		.capabilities =                                                           \
			{                                                                 \
				.columns = DT_INST_PROP(inst, columns),                   \
				.rows = DT_INST_PROP(inst, rows),                         \
				.mode = 0,                                                \
				.brightness.minimum = 0,                                  \
				.brightness.maximum = 255,                                \
				.backlight.minimum = AUXDISPLAY_LIGHT_NOT_SUPPORTED,      \
				.backlight.maximum = AUXDISPLAY_LIGHT_NOT_SUPPORTED,      \
				.custom_characters = 8,                                   \
				.custom_character_width = 5u,                             \
				.custom_character_height = 8u,                            \
			},                                                                \
		.font_width = DT_INST_PROP(inst, font_width),                             \
		.invert_cursor = DT_INST_PROP(inst, invert_cursor),                       \
	};                                                                                         \
	static struct ssd1311_data ssd1311_data_##inst = {                                         \
		.display_on = 0u,                                                                  \
		.cursor_on = 0u,                                                                   \
		.blink_on = 0u,                                                                    \
		.contrast = DT_INST_PROP(inst, contrast),                                          \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_DEFINE(DT_DRV_INST(inst), &ssd1311_init, NULL, &ssd1311_data_##inst,             \
			 &ssd1311_config_##inst, POST_KERNEL, CONFIG_AUXDISPLAY_INIT_PRIORITY,     \
			 &ssd1311_api);

DT_INST_FOREACH_STATUS_OKAY(SSD1311_INIT)
