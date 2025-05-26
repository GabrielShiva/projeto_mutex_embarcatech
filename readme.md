# Sistema de Controle de Estacionamento - Multi-tarefas com FreeRTOS - Embarcatech

Este projeto tem como objetivo implementar um sistema simples de controle de estacionamento utilizando a placa **Raspberry Pi Pico W** integrada à **BITDOGLAB**, em conjunto com o **sistema operacional de tempo real FreeRTOS**. São aplicados conceitos fundamentais como **semáforos (binário e de contagem)** e **mutexes** para gerenciar o acesso concorrente aos recursos.

O número máximo de vagas é definido por uma macro (`PARKING_MAX = 8`). O **botão A** simula a **entrada de um veículo**: ao ser pressionado, gera uma **interrupção** que ativa a tarefa de entrada (`vEntranceTask`). Essa tarefa verifica se o estacionamento está cheio. Caso ainda haja vagas, o contador `parking_counter` é incrementado, o display é atualizado e a cor do **LED RGB** muda conforme a ocupação:

* **Azul**: nenhuma vaga ocupada
* **Verde**: pelo menos uma vaga ocupada
* **Amarelo**: apenas uma vaga restante
* **Vermelho**: estacionamento lotado

Se não houver mais vagas disponíveis, o **buzzer emite um beep** para indicar a lotação.

O **botão B** representa a **saída de um veículo**. Quando pressionado, decrementa o contador, atualiza o display e ajusta a cor do LED RGB conforme a nova ocupação.

Por fim, o **botão SW (joystick)** reinicia o sistema, zerando o contador de vagas, atualizando o display e emitindo um **beep duplo** pelo buzzer como sinal de reinicialização.
