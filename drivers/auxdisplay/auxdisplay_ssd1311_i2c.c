#include "auxdisplay_ssd1311.h"

#if SSD1311_BUS_I2C

static bool check_bus(const union ssd1311_bus_cfg* bus) {
        return device_is_ready(bus->i2c.bus);
}

static int write_cmd(const union ssd1311_bus_cfg* bus, uint8_t cmd) {
        uint8_t cmd_buf[2] = {0x80, cmd};
	return i2c_write_dt(&bus->i2c, cmd_buf, sizeof(cmd_buf));
}
static int write_data(const union ssd1311_bus_cfg* bus, uint8_t data) {
        uint8_t cmd_buf[2] = {0x40, data};
	return i2c_write_dt(&bus->i2c, cmd_buf, sizeof(cmd_buf));
}

static int write_batch_data(const union ssd1311_bus_cfg* bus, const uint8_t *data, uint16_t len)
{
	struct i2c_msg msgs[2];

	uint8_t msg[] = {0x40};
	msgs[0].flags = I2C_MSG_WRITE;
	msgs[0].buf = msg;
	msgs[0].len = sizeof(msg);

	msgs[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;
	msgs[1].buf = (uint8_t *)data;
	msgs[1].len = len;

	return i2c_transfer_dt(&bus->i2c, msgs, 2);
}

const struct ssd1311_ops ssd1311_i2c_ops = {
        .check_bus = check_bus,
        .write_cmd = write_cmd,
        .write_data = write_data,
        .write_batch_data = write_batch_data
};

#endif
