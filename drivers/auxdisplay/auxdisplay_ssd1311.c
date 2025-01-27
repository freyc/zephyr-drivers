
#define DT_DRV_COMPAT solomon_ssd1311

#include <zephyr/drivers/auxdisplay.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ssd1311, CONFIG_AUXDISPLAY_LOG_LEVEL);

struct ssd1311_config {
    struct i2c_dt_spec bus;
    int lines;
    int font_width;
    bool invert_cursor;
};

struct ssd1311_data {
    uint32_t display_on : 1;
    uint32_t cursor_on : 1;
    uint32_t blink_on : 1;
};

static int ssd1311_send_cmd(const struct device* dev, uint8_t cmd) {
    const struct ssd1311_config* config = dev->config;
    uint8_t cmd_buf[2] = {0x80, cmd};
    return i2c_write_dt(&config->bus, cmd_buf, sizeof(cmd_buf));
}

static int ssd1311_display_ctrl(const struct device* dev) {
    struct ssd1311_data* data = dev->data;
#if 0
    const struct ssd1311_config* config = dev->config;
    uint8_t cmd[2] = {0x80, 0x08};

    cmd[1] |= (data->display_on << 2u) | (data->cursor_on << 1u) | (data->blink_on << 0u);

    return i2c_write_dt(&config->bus, cmd, sizeof(cmd));
#else
    uint8_t cmd = 0x08 | (data->display_on << 2u) | (data->cursor_on << 1u) | (data->blink_on << 0u);
    return ssd1311_send_cmd(dev, cmd);
#endif
}

static int ssd1311_display_on(const struct device* dev) {
    struct ssd1311_data* data = dev->data;
    data->display_on = 1u;
    return ssd1311_display_ctrl(dev);
}

static int ssd1311_display_off(const struct device* dev) {
    struct ssd1311_data* data = dev->data;
    data->display_on = 0u;
    return ssd1311_display_ctrl(dev);
}

static int ssd1311_custom_command(const struct device *dev, struct auxdisplay_custom_data *command) {
    const struct ssd1311_config* config = dev->config;
    return i2c_write_dt(&config->bus, command->data, command->len);
}

static int ssd1311_brightness_set(const struct device* dev, uint8_t brightness) {
    const struct ssd1311_config* config = dev->config;

    //uint8_t cmd = 0x2a;
    ssd1311_send_cmd(dev, 0x2a);
    ssd1311_send_cmd(dev, 0x79);
    ssd1311_send_cmd(dev, 0x81);
    ssd1311_send_cmd(dev, brightness);

    ssd1311_send_cmd(dev, 0x78);
    ssd1311_send_cmd(dev, 0x28);
}

static int ssd1311_init(const struct device* dev) {
    const struct ssd1311_config* config = dev->config;

    //https://github.com/iggymayer/SSD1311/blob/main/src/SSD1311.cpp
    //https://github.com/jafrado/2004_i2c_oled/blob/master/ssd13xx_20x4_oled.c
    // send 0x2a -> set RE bit
    // send 0x28 -> clr RE bit
    // send 0x79 -> enable oled cmd set
    // send 0x78 -> disable oled cmd set
    
    // extended function set
    ssd1311_send_cmd(dev, 0x22 | ((config->lines == 2 || config->lines == 4) ? 0x08 : 0x00) /*0x2a*/);

    uint8_t cmd = 0x08;

    if(config->lines == 3 || config->lines == 4) {
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
    ssd1311_send_cmd(dev, 0x28);
    return -ENOTSUP;
}

static DEVICE_API(auxdisplay, ssd1311_api) = {
    .display_on = ssd1311_display_on,
    .display_off = ssd1311_display_off,
    .brightness_get = NULL,
    .brightness_set = ssd1311_brightness_set,
    .custom_command = ssd1311_custom_command,
};

#define SSD1311_INIT(inst)          \
    static const struct ssd1311_config ssd1311_config_##inst = {    \
        .bus = I2C_DT_SPEC_INST_GET(inst),                          \
        .lines = DT_INST_PROP(inst, rows),                          \
        .font_width = DT_INST_PROP(inst, font_width),               \
        .invert_cursor = DT_INST_PROP(inst, invert_cursor),         \
    };                                                              \
    static struct ssd1311_data ssd1311_data_##inst = {0u};          \
                                                                    \
    DEVICE_DT_DEFINE(                                               \
        DT_DRV_INST(inst),                                          \
        &ssd1311_init,                                              \
        NULL,\
        &ssd1311_data_##inst,\
        &ssd1311_config_##inst,\
        POST_KERNEL,\
        CONFIG_AUXDISPLAY_INIT_PRIORITY,\
        &ssd1311_api\
    );

DT_INST_FOREACH_STATUS_OKAY(SSD1311_INIT)
