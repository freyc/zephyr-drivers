#ifndef ZEPHYR_DRIVERS_AUXDISPLAY_SSD1311_H_
#define ZEPHYR_DRIVERS_AUXDISPLAY_SSD1311_H_

#define DT_DRV_COMPAT solomon_ssd1311

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>

#define SSD1311_BUS_I2C         DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
#define SSD1311_BUS_SPI         DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)


union ssd1311_bus_cfg {
#if SSD1311_BUS_I2C
        struct i2c_dt_spec i2c;
#endif
#if SSD1311_BUS_SPI
        struct spi_dt_spec spi;
#endif
};

struct ssd1311_ops {
        bool (*check_bus)(const union ssd1311_bus_cfg* bus);
        int (*write_cmd)(const union ssd1311_bus_cfg* bus, uint8_t cmd);
        int (*write_data)(const union ssd1311_bus_cfg* bus, uint8_t cmd);
        int (*write_batch_data)(const union ssd1311_bus_cfg* bus, const uint8_t* data, uint16_t size);
};


#if SSD1311_BUS_I2C
        extern const struct ssd1311_ops ssd1311_i2c_ops;
#endif
#if SSD1311_BUS_SPI
        extern const struct ssd1311_ops ssd1311_spi_ops;
#endif


#endif