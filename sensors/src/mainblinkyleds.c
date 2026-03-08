/*
* This Zephyr application configures a GPIO input on the user 
* button with an interrupt callback and three GPIO outputs for LEDs.
* Each time the button is pressed, it debounces briefly, turns 
* all LEDs off, then lights the next LED in a rotating sequence.
*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#define SLEEP_TIME_MS 20



#define LED0 DT_ALIAS(led0)
#define LED1 DT_ALIAS(led1)
#define LED2 DT_ALIAS(led2)
#define SW0_NODE DT_ALIAS(sw0)

#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif


static struct gpio_dt_spec leds[] = { GPIO_DT_SPEC_GET_OR(LED0, gpios, {0}),
                                      GPIO_DT_SPEC_GET_OR(LED1, gpios, {0}),
                                      GPIO_DT_SPEC_GET_OR(LED2, gpios, {0})
};

#define NUM_LEDS 3


static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});

static struct gpio_callback button_cb_data;

static bool button_pressed;

void button_pressed_isr(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
    button_pressed = true;
}

static int current_led = 0;

int main(void)
{
	int ret;
    int i;

	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return 0;
	}

	gpio_init_callback(&button_cb_data, button_pressed_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

    for (i = 0; i < NUM_LEDS; i++) {
        if (leds[i].port && !gpio_is_ready_dt(&leds[i])) {
            printk("Error %d: LED %d device %s is not ready; ignoring it\n",
                ret, i + 1, leds[i].port->name);
            leds[i].port = NULL;
            continue;
            }
        
        if (leds[i].port) {
            ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT);
            if (ret != 0) {
                printk("Error %d: failed to configure LED %d device %s pin %d\n",
                    ret, i + 1, leds[i].port->name, leds[i].pin);
                leds[i].port = NULL;
            } else {
                printk("Set up LED %d at %s pin %d\n", i + 1, leds[i].port->name, leds[i].pin);
            }
        }
    }
    printk("Press the button\n");


    while(1){
        if (button_pressed){
            button_pressed = false;
            k_msleep(20);

            for (i = 0; i < NUM_LEDS; i++) {
                if (leds[i].port) {
                    gpio_pin_set_dt(&leds[i], 0);
                }
            }

            if (leds[current_led].port){
                gpio_pin_set_dt(&leds[current_led], 1);
            }
            current_led = (current_led + 1) % NUM_LEDS;
        }
        k_msleep(SLEEP_TIME_MS);
    }
    return 0;
}
