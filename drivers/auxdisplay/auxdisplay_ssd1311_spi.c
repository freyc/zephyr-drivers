#include "auxdisplay_ssd1311.h"

#if SSD1311_BUS_SPI

static bool check_bus(const union ssd1311_bus_cfg* bus) {
        return device_is_ready(bus->spi.bus);
}

static int write_cmd(const union ssd1311_bus_cfg* bus, uint8_t cmd) {
        uint8_t cmd_buf[3] = {0x1f, cmd & 0x0f, (cmd >> 4) & 0x0f};
	
	const struct spi_buf tx_buf = {
		.buf = cmd_buf,
		.len = sizeof(cmd_buf)
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	return spi_write_dt(&bus->spi, &tx);
}
static int write_data(const union ssd1311_bus_cfg* bus, uint8_t data) {
        uint8_t cmd_buf[3] = {0x5f, data & 0x0f, (data >> 4) & 0x0f};
	
	const struct spi_buf tx_buf = {
		.buf = cmd_buf,
		.len = sizeof(cmd_buf)
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	return spi_write_dt(&bus->spi, &tx);
}

static int write_batch_data(const union ssd1311_bus_cfg* bus, const uint8_t *data, uint16_t len)
{
	int rc = 0;
	while(len--) {
		rc = write_data(bus, *data++);
		if(rc < 0) {
			break;
		}
	}
	return rc;
}

const struct ssd1311_ops ssd1311_spi_ops = {
        .check_bus = check_bus,
        .write_cmd = write_cmd,
        .write_data = write_data,
        .write_batch_data = write_batch_data
};

#endif
