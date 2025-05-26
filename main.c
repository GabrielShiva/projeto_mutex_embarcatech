#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define o máximo de carros no estacionamento
#define MAX 8

#define BTN_B_PIN 6
#define BTN_A_PIN 5
#define BTN_SW_PIN 22
#define BUZZER_PIN 21

#define LED_RED 13
#define LED_GREEN 11
#define LED_BLUE 12

// Definição de macros para o protocolo I2C (SSD1306)
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define SSD1306_ADDRESS 0x3C

// Criação das variáveis que receberão os semáforos
SemaphoreHandle_t xDisplayMutex;
SemaphoreHandle_t xCounterSemaphore;
SemaphoreHandle_t xResetBiSemaphore;
SemaphoreHandle_t xEntranceBiSemaphore;
SemaphoreHandle_t xExitBiSemaphore;

volatile uint16_t cars_counter = 0;

// Define variáveis para debounce dos botões
volatile uint32_t last_time_btn_press = 0;
const uint32_t debounce_delay_ms = 260;

// Inicializa instância do display OLED
ssd1306_t ssd;

// Inicializa os periféricos da placa
void peripheral_initialization();

// Realiza a inicialização dos botões
void btn_setup(uint gpio);

// Realiza a inicialização dos LEDs RGB
void led_rgb_setup(uint gpio);

// Realiza a inicialização do protocolo I2C para comunicação com o display OLED
void i2c_setup(uint baud_in_kilo);

// Realiza a inicialização do display OLED
void ssd1306_setup(ssd1306_t *ssd_ptr);

// Inicializa a função que realiza tratamento das interrupções dos botões
void gpio_irq_handler(uint gpio, uint32_t events);

void peripheral_initialization();

// Implementa a tarefa de entrada de carro (botão A)
void vEntranceTask();

// Implementa a tarefa de saída de carro (botão B)
void vLeaveTask();

// Implementa a tarefa de resetar o sistema (botão SW - Joystick)
void vResetTask();

int main() {
    stdio_init_all();

    // Inicializa os periféricos
    peripheral_initialization();

    // Cria os semáforos
    xCounterSemaphore = xSemaphoreCreateCounting(MAX, 0);
    xDisplayMutex = xSemaphoreCreateMutex();
    xResetBiSemaphore = xSemaphoreCreateBinary();
    xEntranceBiSemaphore = xSemaphoreCreateBinary();
    xExitBiSemaphore = xSemaphoreCreateBinary();

    // Criação das tarefas
    xTaskCreate(vEntranceTask, "Task: Entrada", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vLeaveTask, "Task: Saida", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vResetTask, "Task: Resetar", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);

    // Chamda do Scheduller de tarefas
    vTaskStartScheduler();
    panic_unsupported();
}

void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time()); // retorna o tempo total em ms desde o boot do rp2040

    // verifica se a diff entre o tempo atual e a ultima vez que o botão foi pressionado é maior que o tempo de debounce
    if (current_time - last_time_btn_press > debounce_delay_ms) {
        last_time_btn_press = current_time;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        if (gpio == BTN_A_PIN) {
            printf("Botão A pressionado!\n");
            xSemaphoreGiveFromISR(xEntranceBiSemaphore, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        } else if (gpio == BTN_B_PIN) {
            printf("Botão B pressionado!\n");
            xSemaphoreGiveFromISR(xExitBiSemaphore, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        } else if (gpio == BTN_SW_PIN) {
            printf("Botão SW pressionado!\n");
            xSemaphoreGiveFromISR(xResetBiSemaphore, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

void peripheral_initialization() {
    // Inicialização dos botões
    btn_setup(BTN_A_PIN);
    btn_setup(BTN_B_PIN);
    btn_setup(BTN_SW_PIN);

    // Adiciona a interrupção para os botões
    gpio_set_irq_enabled_with_callback(BTN_A_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled(BTN_B_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_SW_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Inicializa os LEDs RGB
    led_rgb_setup(LED_RED);
    led_rgb_setup(LED_GREEN);
    led_rgb_setup(LED_BLUE);

    // Inicializa o buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    // Inicialização do protocolo I2C com 400Khz
    i2c_setup(400);

    // Inicializa a estrutura do display
    ssd1306_setup(&ssd);

    // Realiza a limpeza do display
    ssd1306_fill(&ssd, false);
    ssd1306_init();

    ssd1306_draw_string(&ssd, "Carros: 0 de 8", 10, 5);
    ssd1306_send_data(&ssd);
}

// Realiza a inicialização dos botões
void btn_setup(uint gpio) {
  gpio_init(gpio);
  gpio_set_dir(gpio, GPIO_IN);
  gpio_pull_up(gpio);
}

// Realiza a inicialização dos LEDs RGB
void led_rgb_setup(uint gpio) {
  gpio_init(gpio);
  gpio_set_dir(gpio, GPIO_OUT);
}

// Realiza a inicialização do protocolo I2C para comunicação com o display OLED
void i2c_setup(uint baud_in_kilo) {
  i2c_init(I2C_PORT, baud_in_kilo * 1000);

  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);
}

// Realiza a inicialização do display OLED
void ssd1306_setup(ssd1306_t *ssd_ptr) {
  ssd1306_init(ssd_ptr, WIDTH, HEIGHT, false, SSD1306_ADDRESS, I2C_PORT); // Inicializa o display
  ssd1306_config(ssd_ptr);                                                // Configura o display
  ssd1306_send_data(ssd_ptr);                                             // Envia os dados para o display

  // Limpa o display. O display inicia com todos os pixels apagados.
  ssd1306_fill(ssd_ptr, false);
  ssd1306_send_data(ssd_ptr);
}

// Implementa a tarefa de entrada de carro (botão A)
void vEntranceTask() {
    while (true) {
        // Obtém o semáforo do contador de carros
        xSemaphoreTake(xEntranceBiSemaphore, portMAX_DELAY);

        // Verifica se o semáforo do cotandor atingiu o limite (MAX). Caso não tenha atingido, executa o bloco abaixo
        if (uxSemaphoreGetCount(xCounterSemaphore) < MAX) {
            printf("Incrementou!\n");

            xSemaphoreGive(xCounterSemaphore);

            xSemaphoreTake(xDisplayMutex, portMAX_DELAY);
            int active = uxSemaphoreGetCount(xCounterSemaphore);

            // Display OLED
            char buffer[32];
            sprintf(buffer, "Carros: %d de %d", active, MAX);
            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, buffer, 10, 5);
            ssd1306_send_data(&ssd);

             // LED RGB
            gpio_put(LED_RED, 0);
            gpio_put(LED_GREEN, 0);
            gpio_put(LED_BLUE, 0);
            if (active == 0) {
                gpio_put(LED_RED, 0);
                gpio_put(LED_GREEN, 0);
                gpio_put(LED_BLUE, 1);
            } else if (active < MAX_USERS - 1) {
                gpio_put(LED_RED, 0);
                gpio_put(LED_GREEN, 1);
                gpio_put(LED_BLUE, 0);
            } else if (active == MAX_USERS - 1) {
                gpio_put(LED_RED, 1);
                gpio_put(LED_GREEN, 1);
                gpio_put(LED_BLUE, 0);
            } else {
                gpio_put(LED_RED, 1);
                gpio_put(LED_GREEN, 0);
                gpio_put(LED_BLUE, 0);
            }

            xSemaphoreGive(mtxDisplay);
        } else {
            printf("Máximo atingido!\n");
        }

        // Assume o semáforo do display
        // if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
        //     ssd1306_fill(&ssd, 0);
        //     ssd1306_draw_string(&ssd, "Carro entrou", 5, 20);
        //     ssd1306_send_data(&ssd);

        //     // Libera o semáforo do display
        //     xSemaphoreGive(xDisplayMutex);
        // }
    }
}

// Implementa a tarefa de saída de carro (botão B)
void vLeaveTask() {
    while (true) {
        // Obtém o semáforo do contador de carros
        if (xSemaphoreTake(xExitBiSemaphore, portMAX_DELAY) == pdTRUE) {
            if (uxSemaphoreGetCount(xCounterSemaphore) < MAX) {
                printf("Incrementou!\n");
                xSemaphoreGive(xCounterSemaphore);
            }
        }
        // Assume o semáforo do display
        // if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
        //     ssd1306_fill(&ssd, 0);
        //     ssd1306_draw_string(&ssd, "Carro saiu", 5, 20);
        //     ssd1306_send_data(&ssd);

        //     // Libera o semáforo do display
        //     xSemaphoreGive(xDisplayMutex);
        // }
    }
}

// Implementa a tarefa de resetar o sistema (botão SW - Joystick)
void vResetTask() {
    while (true) {
        // Assume o semáforo do display
        // if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
        //     ssd1306_fill(&ssd, 0);
        //     ssd1306_draw_string(&ssd, "Reset", 5, 20);
        //     ssd1306_send_data(&ssd);

        //     // Libera o semáforo do display
        //     xSemaphoreGive(xDisplayMutex);
        // }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
