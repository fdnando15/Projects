#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "main";

static int nLatido = 0;
static int nInforme = 0;

void tarea_latido(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Latido #%d", nLatido++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void tarea_informe(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "--- Informe #%d ---", nInforme++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void) {
    xTaskCreate(tarea_latido,  "tarea_latido",  2048, NULL, 5, NULL);
    xTaskCreate(tarea_informe, "tarea_informe", 2048, NULL, 5, NULL);
}
