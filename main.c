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
#define PARKING_MAX 8

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

volatile uint16_t parking_counter = 0;

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
    xCounterSemaphore = xSemaphoreCreateCounting(PARKING_MAX, 0);
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

    // Ativa o LED azul para o contador == 0
    gpio_put(LED_RED, 0);
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_BLUE, 1);

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
    ssd1306_send_data(&ssd);

    ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
    ssd1306_line(&ssd, 3, 15, 123, 15, true); // linha horizontal - primeira
    ssd1306_line(&ssd, 3, 40, 123, 40, true); // linha horizontal - segunda
    ssd1306_line(&ssd, 53, 15, 53, 41, true); // linha vertical
    ssd1306_draw_string(&ssd, "Estacionamento", 9, 6);
    ssd1306_draw_string(&ssd, "Vagas", 9, 20);
    ssd1306_draw_string(&ssd, "Disp.", 9, 30);

    ssd1306_draw_string(&ssd, "8 de 8", 64, 25);

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

// Emite o som no buzzer
void buzzer_sound(uint beep_type) {
    if (beep_type == 0) {
        gpio_put(BUZZER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_put(BUZZER_PIN, 0);
    }

    if (beep_type == 1) {
        for (int i = 0; i < 2; i++) {
            gpio_put(BUZZER_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_put(BUZZER_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void update_display_led() {
    // Assume temporariamente o controle do display OLED
    xSemaphoreTake(xDisplayMutex, portMAX_DELAY);

    // Atualiza o display OLED
    char buffer[32];
    sprintf(buffer, "Carros: %d de %d", parking_counter, PARKING_MAX);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, buffer, 10, 5);
    ssd1306_send_data(&ssd);

    // Atualiza o LED RGB com base no valor do contador
    if (parking_counter == 0) {
        gpio_put(LED_RED, 0);
        gpio_put(LED_GREEN, 0);
        gpio_put(LED_BLUE, 1);
    } else if (parking_counter < PARKING_MAX - 1) {
        gpio_put(LED_RED, 0);
        gpio_put(LED_GREEN, 1);
        gpio_put(LED_BLUE, 0);
    } else if (parking_counter == PARKING_MAX - 1) {
        gpio_put(LED_RED, 1);
        gpio_put(LED_GREEN, 1);
        gpio_put(LED_BLUE, 0);
    } else {
        gpio_put(LED_RED, 1);
        gpio_put(LED_GREEN, 0);
        gpio_put(LED_BLUE, 0);
    }

    // Libera o display OLED
    xSemaphoreGive(xDisplayMutex);
}

// Implementa a tarefa de entrada de carro (botão A)
void vEntranceTask() {
    while (true) {
        // Obtém o semáforo do contador de carros
        xSemaphoreTake(xEntranceBiSemaphore, portMAX_DELAY);

        // Verifica se o semáforo do cotandor atingiu o limite (PARKING_MAX). Caso não tenha atingido, executa o bloco abaixo
        if (parking_counter < PARKING_MAX) {
            printf("Carro entrou no estacionamento!\n");

            // Incrementa o contador do número de carros no estacionamento
            parking_counter = parking_counter + 1;

            // Atualiza o display OLED, o LED RGB e o buzzer
            update_display_led();
        } else {
            // O buzzer emite um beep
            buzzer_sound(0);

            printf("Limite máximo de carros foi atingido!\n");
        }
    }
}

// Implementa a tarefa de saída de carro (botão B)
void vLeaveTask() {
    while (true) {
        // Obtém o semáforo do contador de carros
        xSemaphoreTake(xExitBiSemaphore, portMAX_DELAY);

        // Verifica se o semáforo do cotandor atingiu o limite (PARKING_MAX). Caso não tenha atingido, executa o bloco abaixo
        if (parking_counter > 0) {
            printf("Carro saiu do estacionamento!\n");

            // Decrementa o contador do número de carros no estacionamento
            parking_counter = parking_counter - 1;

            // Atualiza o display OLED, o LED RGB e o buzzer
            update_display_led();
        } else {
            printf("Nenhum carro estacionado!\n");
        }
    }
}

// Implementa a tarefa de resetar o sistema (botão SW - Joystick)
void vResetTask() {
    while (true) {
        // Obtém o semáforo do contador de carros
        xSemaphoreTake(xResetBiSemaphore, portMAX_DELAY);

        // Reseta o contador do sistema
        parking_counter = 0;

        // O buzzer emite um beep duplo
        buzzer_sound(1);

        printf("Sistema reiniciado!\n");

        // Atualiza o display OLED, o LED RGB e o buzzer
        update_display_led();
    }
}
