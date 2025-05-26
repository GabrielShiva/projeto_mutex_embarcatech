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
volatile uint16_t parking_counter = 0;

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

// Define variáveis para debounce dos botões
volatile uint32_t last_time_btn_press = 0;
const uint32_t debounce_delay_ms = 260;

// Inicializa instância do display OLED
ssd1306_t ssd;

// pwm
uint32_t clock   = 125000000;
uint32_t divider = 0;
uint32_t wrap    = 0;
uint slice_num   = 0;
uint channel_num = 0;

// Inicializa os periféricos da placa
void peripheral_initialization();

// Realiza a inicialização dos botões
void btn_setup(uint gpio);

// Inicializa o buzzer
void buzzer_setup();

// Cálculo dos paramêtros do PWM para buzzer emitir frequência especificada
void pwm_set_frequency(float frequency);

// Realiza a inicialização dos LEDs RGB
void led_rgb_setup(uint gpio);

// Realiza a inicialização do protocolo I2C para comunicação com o display OLED
void i2c_setup(uint baud_in_kilo);

// Realiza a inicialização do display OLED
void ssd1306_setup(ssd1306_t *ssd_ptr);

// Atualiza o conteúdo do display (contador) e do LED RGB
void update_counter_led();

// Inicializa a função que realiza tratamento das interrupções dos botões
void gpio_irq_handler(uint gpio, uint32_t events);

// Exibe mensagem temporária no display
void show_message(const char *message, uint8_t x, uint8_t y, uint32_t delay_ms);

// Inicializa os periféricos da placa
void peripheral_initialization();

// função que faz com que o buzzer emita um som
void buzzer_sound();

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

// Inicializa os periféricos da placa
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
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    slice_num   = pwm_gpio_to_slice_num(BUZZER_PIN);
    channel_num = pwm_gpio_to_channel(BUZZER_PIN);

    // Configuração inicial do PWM
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, true);

    // Desliga PWM do pino ligado ao buzzer
    pwm_set_enabled(slice_num, false);

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
    ssd1306_line(&ssd, 53, 15, 53, 40, true); // linha vertical
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

// Cálculo dos paramêtros do PWM para buzzer emitir frequência especificada
void pwm_set_frequency(float frequency) {
    // Se frequência for menor que zero não executa nada
    if (frequency <= 0.0f) {
        pwm_set_enabled(slice_num, false);
        return;
    }

    // Calcula os valores para o divisor e para o wrap
    divider = clock / (uint32_t)(frequency * 1000);
    wrap = clock / (divider * (uint32_t)frequency) - 1;

    // Aplica as configurações calculados
    pwm_set_clkdiv_int_frac(slice_num, divider, 0);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, channel_num, wrap / 2); // Define o Duty cycle de 50%
}

// Emite o som no buzzer
void buzzer_sound(uint beep_type) {
    // buzzer emite um beep único com freq. de 60hz
    if (beep_type == 0) {
        // Define a frequência do buzzer
        pwm_set_frequency(60);

        // Liga o buzzer por 100ms
        pwm_set_enabled(slice_num, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        pwm_set_enabled(slice_num, false);
    }

    // buzzer emite um beep duplo com freq. de 60hz
    if (beep_type == 1) {
        for (int i = 0; i < 2; i++) {
            // Define a frequência do buzzer
            pwm_set_frequency(60);

            // Liga o buzzer por 100ms
            pwm_set_enabled(slice_num, true);
            vTaskDelay(pdMS_TO_TICKS(100));
            pwm_set_enabled(slice_num, false);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Exibe mensagem temporária no display
void show_message(const char *message, uint8_t x, uint8_t y, uint32_t delay_ms) {
    xSemaphoreTake(xDisplayMutex, portMAX_DELAY);

    // Exibe a mensagem
    ssd1306_draw_string(&ssd, message, x, y);
    ssd1306_send_data(&ssd);

    xSemaphoreGive(xDisplayMutex);

    // Aguarda o tempo de exibição
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // Apaga a área da mensagem
    xSemaphoreTake(xDisplayMutex, portMAX_DELAY);

    ssd1306_rect(&ssd, 42, 5, 118, 19, true, true);
    ssd1306_rect(&ssd, 42, 5, 118, 19, false, true);
    ssd1306_send_data(&ssd);

    xSemaphoreGive(xDisplayMutex);
}

// Atualiza o conteúdo do display (contador) e do LED RGB
void update_counter_led() {
    // Assume temporariamente o controle do display OLED
    xSemaphoreTake(xDisplayMutex, portMAX_DELAY);

    // Cria o buffer para texto que será carregado no display
    char buffer[32];

    // Atualiza o contador do display
    ssd1306_rect(&ssd, 20, 56, 65, 18, false, false); // Limpa região do contador
    sprintf(buffer, "%d de %d", PARKING_MAX - parking_counter, PARKING_MAX);
    ssd1306_draw_string(&ssd, buffer, 64, 25);
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
            // Incrementa o contador do número de carros no estacionamento
            parking_counter = parking_counter + 1;

            // Atualiza o display OLED, o LED RGB
            update_counter_led();

            show_message("Carro entrou", 9, 48, 1500);

            printf("Carro entrou no estacionamento!\n");
        } else {
            // Atualiza o display OLED, o LED RGB e o buzzer
            buzzer_sound(0);

            show_message("Vaga indisp.", 9, 48, 1500);

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

            update_counter_led();

            show_message("Carro saiu", 9, 48, 1500);
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

        show_message("Reiniciado sis", 9, 48, 2500);

        // Emite um beep duplo
        buzzer_sound(1);

        // Atualiza o display OLED, o LED RGB e o buzzer
        update_counter_led();

        printf("Sistema reiniciado!\n");
    }
}
