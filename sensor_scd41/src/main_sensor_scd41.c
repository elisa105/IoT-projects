/* Get temperature, pressure, and humidity data from a sensirion-scd41 sensor.
*
*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/print_format.h>

const struct device *const dev = DEVICE_DT_GET_ANY(sensirion_scd41);


SENSOR_DT_READ_IODEV(iodev, DT_COMPAT_GET_ANY_STATUS_OKAY(sensirion_scd41),
		{SENSOR_CHAN_CO2, 0},
		{SENSOR_CHAN_HUMIDITY, 0},
		{SENSOR_CHAN_AMBIENT_TEMP, 0});

RTIO_DEFINE(ctx, 1, 1);

static const struct device *check_scd41_device(void)
{
	if (dev == NULL) {
		printk("\nError: no device found.\n");
		return NULL;
	}

	if (!device_is_ready(dev)) {
		printk("\nError: Device \"%s\" is not ready; ", dev->name);
		return NULL;
	}

	printk("Found device \"%s\", getting sensor data\n", dev->name);
	return dev;
}

int main(void)
{
	const struct device *dev = check_scd41_device();

	if (!dev) {
		return 0;
	}

	while (1) {
		uint8_t buf[128];

		int rc = sensor_read(&iodev, &ctx, buf, 128);

		if (rc != 0) {
			printk("%s: sensor_read() failed: %d\n", dev->name, rc);
			return rc;
		}

		const struct sensor_decoder_api *decoder;

		rc = sensor_get_decoder(dev, &decoder);

		if (rc != 0) {
			printk("%s: sensor_get_decode() failed: %d\n", dev->name, rc);
			return rc;
		}

		uint32_t co2_fit = 0;
		struct sensor_q31_data co2_data = {0};

		decoder->decode(buf,
			(struct sensor_chan_spec) {SENSOR_CHAN_CO2, 0},
			&co2_fit, 1, &co2_data);

		uint32_t hum_fit = 0;
		struct sensor_q31_data hum_data = {0};

		decoder->decode(buf,
				(struct sensor_chan_spec) {SENSOR_CHAN_HUMIDITY, 0},
				&hum_fit, 1, &hum_data);

		uint32_t temp_fit = 0;
		struct sensor_q31_data temp_data = {0};

		decoder->decode(buf,
				(struct sensor_chan_spec) {SENSOR_CHAN_AMBIENT_TEMP, 0},
				&temp_fit, 1, &temp_data);

		printk("co2: %s%d.%d; humidity: %s%d.%d; temp: %s%d.%d\n",
			PRIq_arg(co2_data.readings[0].density_ppm, 2, co2_data.shift),
			PRIq_arg(hum_data.readings[0].humidity, 2, hum_data.shift),
			PRIq_arg(temp_data.readings[0].temperature, 2, temp_data.shift));

		k_sleep(K_SECONDS(120));
	}
	return 0;
}