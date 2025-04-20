# RTOS Temperature Sensor with Queues and Timer ISR (ESP-IDF)

This project is a simple FreeRTOS-based application using ESP-IDF on an ESP32-C3 board.

It reads temperature data using the internal temperature sensor, triggered by a periodic software timer (to simulate an ADC interrupt). The temperature values are stored in two queues alternately. When a queue fills up, a task (TaskA) computes the average of the collected values.

Another task (TaskB) periodically prints the latest average temperature value to the console.

## Assignment Description

You should implement a hardware timer in the ESP32 that samples from an ADC pin once every 100 ms. This sampled data should be copied to a double buffer (you could also use a circular buffer). Whenever one of the buffers is full, the ISR should notify Task A.

Task A, when it receives notification from the ISR, should wake up and compute the average of the previously collected 10 samples. Note that during this calculation time, the ISR may trigger again. This is where a double (or circular) buffer will help: you can process one buffer while the other is filling up.

When Task A is finished, it should update a global floating point variable that contains the newly computed average. Do not assume that writing to this floating point variable will take a single instruction cycle! You will need to protect that action as we saw in the queue episode.

Task B should echo any characters received over the serial port back to the same serial port. If the command “avg” is entered, it should display whatever is in the global average variable.

## Features
- Uses `esp_timer` to trigger reads periodically (every 250ms).
- Stores readings in two queues (queue1 and queue2) to avoid blocking.
- Uses task notifications to signal when a queue is full.
- Protects shared variables with a mutex (FreeRTOS semaphore).
- Spinlocks protect shared ISR resources (`currentQueue`).
- Logs to console using `printf`.
- Built on ESP-IDF with FreeRTOS primitives directly.

## Diagram

Here is an image that visually explains the challenge and the system behavior:

![RTOS Timer Interrupt Challenge](https://www.digikey.com/maker-media/210ea308-747e-4827-978a-e7395e0487d3)

## Notes
- Instead of an actual ADC channel, this project uses the ESP32-C3's built-in temperature sensor.
- Serial input functionality is not fully implemented yet.
- TaskB simply prints the latest computed average periodically.
- Built assuming a single-core FreeRTOS setup (`CONFIG_FREERTOS_UNICORE`).
- ISR dispatch method uses `ESP_TIMER_TASK` instead of `ESP_TIMER_ISR` due to ESP32-C3 limitations.

## How It Works
- Timer interrupt triggers `readTemp()` every 250ms.
- Alternating queues store temperature samples.
- When 10 samples are collected, a task computes the average.
- Average is stored safely and can be printed by another task.
