/*
* This Zephyr application configures the USART3 port 
* at 9600 baud, using interrupt-driven reads into a message 
* queue and polling-based writes for sending commands.
* After an initial “K 2” setup, it enters a loop where it 
* sends a “Q” query, waits for a complete line of sensor 
* data from the queue, logs the measurement string, and then 
* sleeps before repeating.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <math.h>


#include <stdio.h>    
#include <string.h>  

#define UART_SENSOR_NODE DT_NODELABEL(usart3)
#define MSG_SIZE 32

K_MSGQ_DEFINE(sensor_msgq, MSG_SIZE, 10, 4);


static const struct device *const uart_sensor = DEVICE_DT_GET(UART_SENSOR_NODE);

/* receive buffer */
static char sensor_rx_buf[MSG_SIZE];
static int sensor_rx_pos;

void sensor_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_sensor)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_sensor)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_sensor, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && sensor_rx_pos > 0) {
			sensor_rx_buf[sensor_rx_pos] = '\0';

			k_msgq_put(&sensor_msgq, sensor_rx_buf, K_NO_WAIT);

			sensor_rx_pos = 0;
		} else if (sensor_rx_pos < (sizeof(sensor_rx_buf) - 1)) {
			sensor_rx_buf[sensor_rx_pos++] = c;
		}
	}
}


/*
 * Print a null-terminated string character by character to the UART interface
 */
void print_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(uart_sensor, buf[i]);
	}
}

int main(void)
{
	if (!device_is_ready(uart_sensor)) {
		printk("UART device not found!");
		return 0;
	}

	struct uart_config uart_cfg = {
		.baudrate = 9600,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE
	};

	int ret_uart = uart_configure(uart_sensor, &uart_cfg);

	if (ret_uart) {
		printk("ERROR: uart_configure() = %d\n", ret_uart);
		return 0;
	}

	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_sensor, sensor_cb, NULL);

	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}
		return 0;
	}
	uart_irq_rx_enable(uart_sensor);

	char resp[MSG_SIZE];

	print_uart("K 2\r\n");
	k_msleep(500);
	
	while(1){
		print_uart("Q\r\n");

		k_msgq_get(&sensor_msgq, resp, K_FOREVER);
		printk("\nMeasurements per 5 minutes: %s\n", resp);
		k_sleep(K_SECONDS(360));
	}
	
	return 0;
}