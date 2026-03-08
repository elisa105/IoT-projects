/*
* This Zephyr application configures two UART ports—one 
* for receiving sensor data via interrupts and message queues, 
* and another for sending commands to the sensor.
* It continuously sends commands, reads line-based responses, 
* parses raw temperature, humidity, and CO₂ readings into human-readable 
* values, and logs them.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <math.h>


#include <stdio.h>    
#include <string.h>  

#define UART_SENSOR_NODE DT_NODELABEL(usart3)
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

#define MSG_SIZE 32

/* queue to store up to 10 messages */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static const struct device *const uart_sensor = DEVICE_DT_GET(UART_SENSOR_NODE);

/* receive buffer */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;


/*
 * Read characters from UART until line end is detected and push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			rx_buf[rx_buf_pos] = '\0';

			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
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
	if (!device_is_ready(uart_dev) || !device_is_ready(uart_sensor)) {
		printk("UART devices not found!");
		return 0;
	}


	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

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
	uart_irq_rx_enable(uart_dev);


	printk("Sensor on usart3 starting...\n");

	char cmd[MSG_SIZE];
	char resp[MSG_SIZE];
    uint8_t c;
    int raw;
    int temp, hum, co2;

	print_uart("K 1\r\n");

	while (1) {
		int  idx = 0;
		k_msgq_get(&uart_msgq, &cmd, K_FOREVER);
		printk("In while: cmd = '%s'\n", cmd);
		temp = hum = co2 = 0;

		print_uart(cmd);
		print_uart("\r\n");

		while (1) {
            if (uart_poll_in(uart_sensor, &c) == 0) {
                if ((c == '\n' || c == '\r') && idx > 0) {
                    break;
                }
                if (idx < MSG_SIZE - 1) {
                    resp[idx++] = c;
                }
            }
        }
        resp[idx] = '\0';
        printk("sensor message: '%s'\n", resp);
    

		if (sscanf(resp, "%c %d", &cmd[0], &raw) == 2){
			printk("raw: %d", raw);
			if (cmd[0] == 'T'){
				temp = (raw - 1000) / 10.0;
				printk("Temperature: %dC\n", temp);
			}
			else if (cmd[0] == 'H') {
				hum = raw / 10.0;
				printk("Humidity: %d%%\n", hum);
			}
			else if (cmd[0] == 'Z') {
				co2 = raw * 10.0;
				printk("CO2: %d ppm\n", co2);
			}
			else {
				printk("Unknown command.\n");
			}
		}
	}
	return 0;
}