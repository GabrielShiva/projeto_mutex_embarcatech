#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    stdio_init_all();

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
