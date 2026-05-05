#include "atomic_info.h"
#include "atomic_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "init.h"

static const char *TAG = "󰜈";

void app_main(void) {
    notice(TAG, "app_main()");
    atomic_info_print_task_stacks();

    atomic_info_print();
    init();

    atomic_info_print_heap();
    atomic_info_print_task_stacks();

    uint32_t cnt = 0;
    while (1) {
        cnt++;
        // debug(TAG, "tick(%u)", cnt);
        /*
        if (cnt % 5 == 0) {
            warn(TAG, "---- before task creation:");
            atomic_info_print_heap();
            atomic_info_print_task_stacks();
            warn(TAG, "---------------------");
        }
        */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}