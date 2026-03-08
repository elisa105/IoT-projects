/*
 * Class A LoRaWAN sample application
 *
 * Copyright (c) 2020 Manivannan Sadhasivam <mani@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Brings up a Class A node, joins via ABP, and periodically sends a small
 * uplink message. It also listens for downlinks and logs basic info (it does not work in our project).
 */

#include <zephyr/device.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define DEV_ADDR   0x260111AA
#define NWK_SKEY   { 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08, \
                     0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10 }
#define APP_SKEY   { 0x10,0x0F,0x0E,0x0D,0x0C,0x0B,0x0A,0x09, \
                     0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01 }

#define DELAY K_SECONDS(15)

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lorawan_class_a);

char data[] = {'h', 'e', 'l', 'l', 'o', 'f', 'r', 'o', 'm', 'n', 'u', 'c', 'l', 'e', 'o', '1'};

static int apply_single_channel_mask(void) {
    static uint16_t mask[1] = { 0x0001 };
    int ret = lorawan_set_channels_mask(mask, ARRAY_SIZE(mask));
    if (ret) {
        LOG_ERR("lorawan_set_channels_mask failed: %d", ret);
    } else {
        LOG_INF("Channel mask applied: only CH0 (868.1 MHz)");
    }
    return ret;
}

// static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr, uint8_t len,
// 			const uint8_t *hex_data)
// {
// 	LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm, Time %d", port,
// 		flags & LORAWAN_DATA_PENDING, rssi, snr, !!(flags & LORAWAN_TIME_UPDATED));
// 	if (hex_data) {
// 		LOG_HEXDUMP_INF(hex_data, len, "Payload: ");
// 	}
// }

static void lorwan_datarate_changed(enum lorawan_datarate dr)
{
	uint8_t unused, max_size;

	lorawan_get_payload_sizes(&unused, &max_size);
	LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
}

int main(void)
{
	const struct device *lora_dev;
	int ret;

	struct lorawan_downlink_cb downlink_cb = {
		.port = LW_RECV_PORT_ANY,
		.cb = dl_callback
	};

	lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
	if (!device_is_ready(lora_dev)) {
		LOG_ERR("%s: device not ready.", lora_dev->name);
		return 0;
	}

#if defined(CONFIG_LORAMAC_REGION_EU868)
	/* If more than one region Kconfig is selected, app should set region
	 * before calling lorawan_start()
	 */
	ret = lorawan_set_region(LORAWAN_REGION_EU868);
	if (ret < 0) {
		LOG_ERR("lorawan_set_region failed: %d", ret);
		return 0;
	}
#endif

	ret = lorawan_start();
	if (ret < 0) {
		LOG_ERR("lorawan_start failed: %d", ret);
		return 0;
	}

	//(void)apply_single_channel_mask();

	lorawan_enable_adr(false);
	ret = lorawan_set_datarate(LORAWAN_DR_5);
	if (ret < 0) {
		LOG_ERR("lorawan_set_datarate failed: %d", ret);
		return 0;
	}

	lorawan_register_downlink_callback(&downlink_cb);
	lorawan_register_dr_changed_callback(lorwan_datarate_changed);

	uint8_t nwk_skey[] = NWK_SKEY;
    uint8_t app_skey[] = APP_SKEY;
    uint32_t dev_addr = DEV_ADDR;

    struct lorawan_join_config join_cfg = {
        .mode = LORAWAN_ACT_ABP,
        .abp = {
            .dev_addr = dev_addr,
            .nwk_skey = nwk_skey,
            .app_skey = app_skey,
        }
    };

    LOG_INF("Activating (ABP)...");
    ret = lorawan_join(&join_cfg);
    if (ret < 0) {
        LOG_ERR("lorawan_join (ABP) failed: %d", ret);
        return 0;
    }


	//(void)apply_single_channel_mask();


	LOG_INF("Sending data...");
	while (1) {
		ret = lorawan_send(2, data, sizeof(data),
				   LORAWAN_MSG_UNCONFIRMED);

		/*
		 * Note: The stack may return -EAGAIN if the provided data
		 * length exceeds the maximum possible one for the region and
		 * datarate. But since we are just sending the same data here,
		 * we'll just continue.
		 */
		if (ret == -EAGAIN) {
			LOG_ERR("lorawan_send failed: %d. Continuing...", ret);
			k_sleep(DELAY);
			continue;
		}

		if (ret < 0) {
			LOG_ERR("lorawan_send failed: %d", ret);
			return 0;
		}

		LOG_INF("Data sent!");
		k_sleep(DELAY);
	}
}
