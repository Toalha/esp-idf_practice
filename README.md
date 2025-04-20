# RTOS Temperature Sensor with Queues and Timer ISR (ESP-IDF)

This project is a simple FreeRTOS-based application using ESP-IDF on an ESP32-C3 board.

It reads temperature data using the internal temperature sensor, triggered by a periodic software timer (simulating an ADC interrupt). The temperature values are stored in two queues alternately. When a queue fills up, a task (TaskA) computes the average of the collected values.

Another task (TaskB) periodically prints the latest average temperature value to the console.

## Features
- Uses `esp_timer` to trigger reads periodically (every 250ms).
- Stores readings in two queues (queue1 and queue2) to avoid blocking.
- Uses task notifications to signal when a queue is full.
- Protects shared variables with a mutex (FreeRTOS semaphore).
- Spinlocks protect shared ISR resources (`currentQueue`).
- Built on ESP-IDF with FreeRTOS primitives directly.

## Diagram

Here is an image that visually explains the challenge and the system behavior:

![RTOS Timer Interrupt Challenge](img/diagram.jfif)

## Notes
- Instead of an actual ADC channel, this project uses the ESP32-C3's built-in temperature sensor.
- Serial input functionality is not implemented.
- TaskB simply prints the latest computed average periodically.
- Built assuming a single-core FreeRTOS setup (`CONFIG_FREERTOS_UNICORE`).
- ISR dispatch method uses `ESP_TIMER_TASK` instead of `ESP_TIMER_ISR` due to ESP32-C3 limitations.
