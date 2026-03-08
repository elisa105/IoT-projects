/*
 * Nucleo-WL55JC1 + ADT7420 (I2C) + LoRaWAN (ABP)
 * Sends RAW BINARY payload (2 bytes): Temperature 
 * Decoding happens on the server (ChirpStack payload formatter)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define DEV_ADDR   0x260111BB
#define NWK_SKEY   { 0xE3,0xDC,0x88,0x4F,0xCA,0x46,0x42,0x23,0xA1,0xE8,0xAC,0x43,0x1D,0xEE,0xD3,0xD0 }
#define APP_SKEY   { 0xE2,0x8B,0x71,0xA6,0x75,0xBA,0x23,0x6A,0x5F,0xB8,0xAD,0xF6,0xF6,0x70,0xCF,0x77 }

#define UPLINK_PORT    2
#define UPLINK_PERIOD  K_SECONDS(60)

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


static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                        uint8_t len, const uint8_t *hex_data)
{
    LOG_INF("DL port=%u pend=%u RSSI=%d SNR=%d timeupd=%u len=%u",
            port, !!(flags & LORAWAN_DATA_PENDING), rssi, snr,
            !!(flags & LORAWAN_TIME_UPDATED), len);
    if (hex_data) {
        LOG_HEXDUMP_INF(hex_data, len, "Downlink payload:");
    }
}

/* RAW payload packing - 2 bytes little-endian: Temperature */
static size_t encode_payload(uint8_t *buf, int16_t temp_x100)
{
    buf[0] = (uint8_t)(temp_x100 & 0xFF);
    buf[1] = (uint8_t)((temp_x100 >> 8) & 0xFF);
    return 2;
}

int main(void)
{
    const struct device *lora_dev;
    const struct device *sensor_dev;
    int ret;

    /* LoRaWAN */
    lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
    if (!device_is_ready(lora_dev)) {
        LOG_ERR("%s: device not ready.", lora_dev->name);
        return 0;
    }

#if defined(CONFIG_LORAMAC_REGION_EU868)
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

    apply_single_channel_mask();

    lorawan_enable_adr(false);
    ret = lorawan_set_datarate(LORAWAN_DR_5);
    if (ret < 0) {
        LOG_ERR("lorawan_set_datarate failed: %d", ret);
        return 0;
    }

    struct lorawan_downlink_cb downlink_cb = {
        .port = LW_RECV_PORT_ANY,
        .cb = dl_callback
    };
    lorawan_register_downlink_callback(&downlink_cb);

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

    ret = lorawan_join(&join_cfg);
    if (ret < 0) {
        LOG_ERR("lorawan_join failed: %d", ret);
        return 0;
    }

    LOG_INF("LoRaWAN ready.");

    /* Sensor */
    sensor_dev = DEVICE_DT_GET_ONE(adi_adt7420);
    if (!device_is_ready(sensor_dev)) {
        LOG_ERR("ADT7420: device not ready.");
        return 0;
    }

    LOG_INF("Sensor ready.");


    /* Main */
    while (1) {
        struct sensor_value temp_val;
        
        /* Read temperature from sensor */
        ret = sensor_sample_fetch(sensor_dev);
        if (ret) {
            LOG_ERR("sensor_sample_fetch failed: %d", ret);
            k_sleep(K_SECONDS(5));
            continue;
        }

        ret = sensor_channel_get(sensor_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_val);
        if (ret) {
            LOG_ERR("sensor_channel_get failed: %d", ret);
            k_sleep(K_SECONDS(5));
            continue;
        }

        /* Convert to 0.01°C units for transmission */
        int32_t temp_x100 = (int32_t)(temp_val.val1 * 100) + (temp_val.val2 / 10000);
        
        if (temp_x100 > INT16_MAX) temp_x100 = INT16_MAX;
        if (temp_x100 < INT16_MIN) temp_x100 = INT16_MIN;

        /* Local logging */
        LOG_INF("Temperature: %d.%02d C", temp_x100 / 100, temp_x100 % 100);

        /* raw binary payload */
        uint8_t payload[2];
        size_t len = encode_payload(payload, (int16_t)temp_x100);

        /* Send raw binary payload */
        ret = lorawan_send(UPLINK_PORT, payload, len, LORAWAN_MSG_UNCONFIRMED);
        if (ret == -EAGAIN) {
            LOG_WRN("lorawan_send -EAGAIN; will retry next period");
        } else if (ret < 0) {
            LOG_ERR("lorawan_send failed: %d", ret);
        } else {
            LOG_INF("Uplink sent");
        }

        k_sleep(UPLINK_PERIOD);
    }

    return 0;
}