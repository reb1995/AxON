#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

// Include GPIO drivers specifically for the C6 antenna switch
#ifdef CONFIG_IDF_TARGET_ESP32C6
#include "driver/gpio.h"
#endif

#define TAG "AxON"
#define FUZZ_INTERVAL_MS 500

static uint8_t ble_addr_type;
static uint16_t fuzz_value = 0;

// The raw advertising payload (31 bytes max)
static uint8_t raw_adv_data[] = {
    0x02, 0x01, 0x06,
    0x1B, 0x16, 0x6C, 0xFE,
    0x01, 0x58, 0x38, 0x37, 0x30, 0x30, 0x32, 0x46,
    0x50, 0x34, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
    0xCE, 0x1B, 0x33, 0x00, 0x00, 0x02, 0x00, 0x00
};

static void start_advertising(void) {
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Load raw payload into NimBLE
    ble_gap_adv_set_data(raw_adv_data, sizeof(raw_adv_data));

    int rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Advertising start failed, rc: %d", rc);
    }
}

static void fuzz_task(void *pvParameters) {
    bool use_format_a = true;

    while (1) {
        // Stop advertising to swap payload
        ble_gap_adv_stop();

        // Only increment the fuzz value after both formats have been broadcast
        if (use_format_a) {
            fuzz_value++;
        }

        // Apply the base command bytes (these are identical in both versions)
        raw_adv_data[17] = (fuzz_value >> 8) & 0xFF;
        raw_adv_data[18] = fuzz_value & 0xFF;

        if (use_format_a) {
            // Format A: The original Kotlin logic (Bitwise shift by 4)
            raw_adv_data[27] = (fuzz_value >> 4) & 0xFF;
            raw_adv_data[28] = (fuzz_value << 4) & 0xFF;

            ESP_LOGI(TAG, "Format A | FUZZ: 0x%04X | END BYTES: %02X %02X",
                     fuzz_value, raw_adv_data[27], raw_adv_data[28]);
        } else {
            // Format B: The Screenshot logic (Bitwise shift by 8)
            raw_adv_data[27] = (fuzz_value >> 8) & 0xFF;
            raw_adv_data[28] = fuzz_value & 0xFF;

            ESP_LOGI(TAG, "Format B | FUZZ: 0x%04X | END BYTES: %02X %02X",
                     fuzz_value, raw_adv_data[27], raw_adv_data[28]);
        }

        // Restart advertising with the new payload
        start_advertising();

        // Toggle the format flag for the next loop
        use_format_a = !use_format_a;

        // Delay for half the interval so one full fuzz cycle still takes 500ms
        vTaskDelay(pdMS_TO_TICKS(FUZZ_INTERVAL_MS));
    }
}

static void ble_app_on_sync(void) {
    // Determine the best address type automatically
    ble_hs_id_infer_auto(0, &ble_addr_type);

    start_advertising();
    xTaskCreate(fuzz_task, "fuzz_task", 4096, NULL, 5, NULL);
}

void ble_app_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#ifdef CONFIG_IDF_TARGET_ESP32C6
    // Power on the RF switch (Active LOW)
    gpio_reset_pin(GPIO_NUM_3);
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 0);

    // Give the RF switch a brief moment to stabilize
    vTaskDelay(pdMS_TO_TICKS(10));

    // Route signal to external U.FL connector (Active HIGH)
    gpio_reset_pin(GPIO_NUM_14);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 1);
#endif

    // Initialize NimBLE stack
    nimble_port_init();

    // Assign callback for when the host and controller are synced
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // Run the NimBLE thread
    nimble_port_freertos_init(ble_app_host_task);

    ESP_LOGI(TAG, "AxON initialized. Starting Fuzz Loop...");
}
