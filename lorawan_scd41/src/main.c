
/*
 * Nucleo-WL55JC1 + SCD41 + LoRaWAN (ABP)
 * Sends RAW BINARY payload (6 bytes): CO2 u16, Temp , HUM 
 * Decoding happens on the server (ChirpStack payload formatter)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/print_format.h>

#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define DEV_ADDR   0x260111AA
#define NWK_SKEY   { 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08, \
                     0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10 }
#define APP_SKEY   { 0x10,0x0F,0x0E,0x0D,0x0C,0x0B,0x0A,0x09, \
                     0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01 }

#define UPLINK_PORT    2
#define UPLINK_PERIOD  K_SECONDS(120) 

/* restrict to EU868 CH0 868.1 MHz */
static int apply_single_channel_mask(void) {
    static uint16_t mask[1] = { 0x0001 };
    int ret = lorawan_set_channels_mask(mask, ARRAY_SIZE(mask));
    if (ret) LOG_ERR("lorawan_set_channels_mask failed: %d", ret);
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
        printk("\nError: Device \"%s\" is not ready; "
               "check the driver initialization logs for errors.\n",
               dev->name);
        return NULL;
    }
    printk("Sensor ready.\n");
    return dev;
}

/* RAW payload packing
 * 6 bytes, little-endian: CO2 u16, Temp s16 (*100), Hum u16 (*100)
 */
static size_t encode_payload(uint8_t *buf, uint16_t co2_ppm, int16_t t_c_x100, uint16_t hum_x100)
{
    buf[0] = (uint8_t)(co2_ppm & 0xFF);
    buf[1] = (uint8_t)((co2_ppm >> 8) & 0xFF);
    buf[2] = (uint8_t)(t_c_x100 & 0xFF);
    buf[3] = (uint8_t)((t_c_x100 >> 8) & 0xFF);
    buf[4] = (uint8_t)(hum_x100 & 0xFF);
    buf[5] = (uint8_t)((hum_x100 >> 8) & 0xFF);
    return 6;
}

int main(void)
{
    /*LoRaWAN*/
#if defined(CONFIG_LORAMAC_REGION_EU868)
    int ret = lorawan_set_region(LORAWAN_REGION_EU868);
    if (ret < 0) { LOG_ERR("lorawan_set_region failed: %d", ret); return 0; }
#endif

    ret = lorawan_start();
    if (ret < 0) {
        LOG_ERR("LoRaWAN start failed: %d", ret);
        return 0;
    }

    apply_single_channel_mask();

    lorawan_enable_adr(false);
    lorawan_set_datarate(LORAWAN_DR_5);

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
    const struct device *sdev = check_scd41_device();
    if (!sdev) return 0;

    /* Main loop */
    while (1) {
        uint8_t buf[128];

        /* Read sensor bytes */
        int rc = sensor_read(&iodev, &ctx, buf, sizeof(buf));
        if (rc != 0) {
            printk("Sensor_read failed: %d\n", rc);
            k_sleep(K_SECONDS(5));
            continue;
        }

        /* hex-print first 16 bytes of the raw buffer */
        printk("sensor raw bytes:");
        for (int i = 0; i < 16 && i < sizeof(buf); i++) {
            printk(" %02X", buf[i]);
        }
        printk("\n");

        /* Decoder for local logging */
        const struct sensor_decoder_api *decoder;
        rc = sensor_get_decoder(sdev, &decoder);
        if (rc != 0 || !decoder) {
            printk("%s: sensor_get_decoder() failed: %d\n", sdev->name, rc);
            k_sleep(K_SECONDS(5));
            continue;
        }

        uint32_t co2_fit = 0, hum_fit = 0, temp_fit = 0;
        struct sensor_q31_data co2_data = {0}, hum_data = {0}, temp_data = {0};

        decoder->decode(buf,
            (struct sensor_chan_spec){SENSOR_CHAN_CO2, 0},
            &co2_fit, 1, &co2_data);

        decoder->decode(buf,
            (struct sensor_chan_spec){SENSOR_CHAN_HUMIDITY, 0},
            &hum_fit, 1, &hum_data);

        decoder->decode(buf,
            (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0},
            &temp_fit, 1, &temp_data);

        printk("co2: %s%d.%d; humidity: %s%d.%d; temp: %s%d.%d\n",
            PRIq_arg(co2_data.readings[0].density_ppm, 2, co2_data.shift),
            PRIq_arg(hum_data.readings[0].humidity,    2, hum_data.shift),
            PRIq_arg(temp_data.readings[0].temperature,2, temp_data.shift));

        /* RAW binary payload (6 bytes) over the air (encrypted by LoRaWAN AppSKey)
         */
        struct sensor_value co2, t, hum;

        rc  = sensor_channel_get(sdev, SENSOR_CHAN_CO2, &co2);
        rc |= sensor_channel_get(sdev, SENSOR_CHAN_AMBIENT_TEMP, &t);
        rc |= sensor_channel_get(sdev, SENSOR_CHAN_HUMIDITY, &hum);
        if (rc) {
            LOG_WRN("sensor_channel_get failed (%d); skipping uplink", rc);
            k_sleep(UPLINK_PERIOD);
            continue;
        }

        /* Convert to integers for raw packing */
        uint16_t co2_ppm = (co2.val1 < 0) ? 0 : (uint16_t)MIN(co2.val1, 65535);

        /* Temperature in 0.01°C */
        int32_t t_x100 = (int32_t)t.val1 * 100 + (t.val2 / 10000);
        if (t_x100 > INT16_MAX) t_x100 = INT16_MAX;
        if (t_x100 < INT16_MIN) t_x100 = INT16_MIN;

        /* Humidity in 0.01%hum */
        int32_t hum_x100 = (int32_t)hum.val1 * 100 + (hum.val2 / 10000);
        if (hum_x100 < 0) hum_x100 = 0;
        if (hum_x100 > 65535) hum_x100 = 65535;

        uint8_t payload[6];
        size_t len = encode_payload(payload, co2_ppm, (int16_t)t_x100, (uint16_t)hum_x100);

        rc = lorawan_send(UPLINK_PORT, payload, len, LORAWAN_MSG_UNCONFIRMED);
        if (rc == -EAGAIN) {
            LOG_WRN("lorawan_send -EAGAIN; will retry next period");
        } else if (rc < 0) {
            LOG_ERR("lorawan_send failed: %d", rc);
        } else {
            LOG_INF("Uplink sent");
        }

        k_sleep(UPLINK_PERIOD);
    }

    return 0;
}
