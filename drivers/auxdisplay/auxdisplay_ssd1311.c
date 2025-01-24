
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

static int ssd1311_init(const struct device* dev) {
    const struct ssd1311_config* config = dev->config;

    //https://github.com/iggymayer/SSD1311/blob/main/src/SSD1311.cpp
    //https://github.com/jafrado/2004_i2c_oled/blob/master/ssd13xx_20x4_oled.c
    // send 0x2a -> set RE bit
    // send 0x28 -> clr RE bit
    // send 0x79 -> enable oled cmd set
    // send 0x78 -> disable oled cmd set
    uint8_t cmd = 0x08;
    ssd1311_send_cmd(dev, 0x2a);

    if(config->lines == 3 || config->lines == 4) {
        cmd |= 0x01;
    }
    // if invert_cursor |= 0x02
    // if 6dot-font |= 0x04
    ssd1311_send_cmd(dev, cmd);

    ssd1311_send_cmd(dev, 0x28);
    return -ENOTSUP;
}

static DEVICE_API(auxdisplay, ssd1311_api) = {
    .display_on = ssd1311_display_on,
    .display_off = ssd1311_display_off,
};

#define SSD1311_INIT(inst)          \
    static const struct ssd1311_config ssd1311_config_##inst = {    \
        .bus = I2C_DT_SPEC_INST_GET(inst),                          \
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
