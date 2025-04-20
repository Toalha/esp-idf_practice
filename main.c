/*
* SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: CC0-1.0
*
* My solution to the challenge suggested in Introduction to RTOS Part 9 - https://www.youtube.com/watch?v=qsflCf6ahXU
* reading values from the temperature sensor instead of an ADC
*
* Resources:
*   https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html#_CPPv418esp_timer_handle_t
*   https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html#tasks
*   https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html#tasks
    input:
    https://esp32.com/viewtopic.php?t=284
    https://esp32.com/viewtopic.php?t=284#p1295

*/

#define RUNEXAMPLECODE 0
#if RUNEXAMPLECODE == 0

#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD 1
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

// using the built-in temperature sensor instead of an ADC
#include "esp_log.h"
#include "driver/temperature_sensor.h"


// program parameters
#define ISR_PERIOD 250000 //µs
#define QUEUE_SIZE 10
#define AVG_T_PRINT_PERIOD 7000 //ms


// only use one core (although this board has only 1 cpu anyway)
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

#define VIRTUAL_LED 0 
#if VIRTUAL_LED == 1
// Virtual LED variables and function
volatile bool virtualLedStatus = 0;

void toggleVirtualLed(void *parameter){
    while(1){
      printf("Current LED status: %d\n", virtualLedStatus);
  
      vTaskDelay(1000/portTICK_PERIOD_MS);
      virtualLedStatus = !virtualLedStatus;
    }
}
#endif

// ************Global Variables******************
//LOGGING
static const char *TAG = "Setup >>";

//Handlers
static esp_timer_handle_t xtimerISR; 
static TaskHandle_t xTaskA = NULL, xTaskB = NULL;  

//Temperature Sensor 
static temperature_sensor_handle_t temp_sensor = NULL;

//Mutexes
// static SemaphoreHandle_t mutexISR;
static SemaphoreHandle_t mutexAVG_t;

//Spinlock
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

//Queues
static QueueHandle_t queue1;
/* The index within the target task's array of task notifications to use when referring to queue1. */
const UBaseType_t xArrayIndexQueue1 = 0;
static QueueHandle_t queue2;
/* The index within the target task's array of task notifications to use when referring to queue2. */
const UBaseType_t xArrayIndexQueue2 = 1;
/* Current queue being written to */
volatile bool currentQueue = 1;
/* Latest average temperature value */
volatile float avg_tVal = 0;

// calculates the average of the last 10 temperature values
// alternates between using queue1 and queue2
void taskA(void* parameter){
    const TickType_t xBlockTime = pdMS_TO_TICKS( 10 );
    uint32_t ulNotifiedVal = {0};
    float t_value={0}, average_t = {0};

    while(1){
        ulNotifiedVal = ulTaskNotifyTake(
                                pdTRUE,
                                xBlockTime 
                            );

        //if notification is == 1, queue1 is full; notification == 2, queue2 is full
        if( ulNotifiedVal != 0 ){
            if(ulNotifiedVal == 1){
                while(xQueueReceive(queue1, (void*) &t_value, xBlockTime) != pdFALSE){
                    average_t += t_value;
                }
            }
            else if(ulNotifiedVal == 2){
                while(xQueueReceive(queue2, (void*) &t_value, xBlockTime) != pdFALSE){
                    average_t += t_value;
                }
            }

            //avg_tVal can be accessed in taskB, needs to be protected
            xSemaphoreTake(mutexAVG_t, xBlockTime);
            avg_tVal = average_t/QUEUE_SIZE;
            xSemaphoreGive(mutexAVG_t);
            printf("taskA read from the queue%ld!\n\n\n", ulNotifiedVal);
        }
        else{
            //resets the value of average_t
            average_t = 0;
            vTaskDelay((QUEUE_SIZE*ISR_PERIOD/1000) / portTICK_PERIOD_MS);
        }
    }
}

// ocasionally prints the average temperature value
void taskB(void* parameter){
    const TickType_t xBlockTime = pdMS_TO_TICKS( 10 );

    while(1){
        vTaskDelay((AVG_T_PRINT_PERIOD-2000)/portTICK_PERIOD_MS);
        printf("print from taskB, will print the avg value in 2 seconds..\n");
        vTaskDelay(2000/portTICK_PERIOD_MS);
        xSemaphoreTake(mutexAVG_t, xBlockTime);
        //copies the avg_tVal to a local variable to prevent issues
        float currAvg_tVal = avg_tVal;
        xSemaphoreGive(mutexAVG_t);
        printf("print from taskB, latest avg temp value: %f\n", currAvg_tVal);
    
    }
}

void readTemp(void* parameter){
    float tsens_value = 0;
    short notificationVal = 0;
    BaseType_t xHigherPriorityTaskWoken = pdTRUE;
    QueueHandle_t* currQueue;


    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));
       
    //if currentQueue = 1, saves to queue 1, else saves to queue2 
    if(currentQueue){
        currQueue = &queue1;
        notificationVal = 1;
    }
    else{
        currQueue = &queue2;
        notificationVal = 2;
    }

    //the task to read the queue is not higher priority so pdFALSE
    xQueueSendFromISR((*currQueue), &tsens_value, pdFALSE);
    //verify if queue is full after writing to it
    if(xQueueIsQueueFullFromISR((*currQueue)) == pdTRUE){
        portENTER_CRITICAL_ISR(&spinlock)
        //changes current queue
        currentQueue=!currentQueue;
        //notifies the task that the queue is ready to be read
        xTaskNotifyIndexedFromISR(xTaskA, 0, notificationVal, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);  // "queue1"
        portEXIT_CRITICAL_ISR(&spinlock);
        printf("queue is full, needs to be read!\n\n");
    }
}




// initializes the temperature sensor
void setupTempSensor(){
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));

    ESP_LOGI(TAG, "Enable temperature sensor");
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
}

void startISRTimer(){
    esp_timer_create_args_t timerARGS = { 
        .callback=readTemp, 
        .name="Timer TempReadISR",
        .arg=NULL, 
        // .dispatch_method=ESP_TIMER_ISR,  //couldnt make it work on esp32c3
        .dispatch_method=ESP_TIMER_TASK,
        .skip_unhandled_events=true
    };
    esp_err_t err = esp_timer_create(
                        &timerARGS,
                        &xtimerISR
                    );
    ESP_LOGI(TAG, "Timer create returned: %s", esp_err_to_name(err));

    esp_timer_start_periodic(xtimerISR, ISR_PERIOD);

}

void startQueues(){
    queue1 = xQueueCreate(QUEUE_SIZE, sizeof(float));
    queue2 = xQueueCreate(QUEUE_SIZE, sizeof(float));
}


void app_main(void){
    //welcome message
    printf("Main app running on core %d\n", app_cpu);
    vTaskDelay(5000 / portTICK_PERIOD_MS);


    // initializes the temperature sensor
    setupTempSensor();
    // initialize the queues
    startQueues();

    //create the mutex before tasks
    mutexAVG_t = xSemaphoreCreateMutex();

    xTaskCreate(                            
        taskA,                              // Function to be called
        "TaskA Read Queues",                // Name of task
        2048,                               // Stack size (bytes in ESP32, words in FreeRTOS)
        NULL,                               // Parameters to pass to function
        2,                                  // Task priority (0 to configMAX_PRIORITIES -1)
        &xTaskA                             // Task handle
    );

    xTaskCreate(                            
        taskB,                              // Function to be called
        "TaskB Print Values",               // Name of task
        2048,                               // Stack size (bytes in ESP32, words in FreeRTOS)
        NULL,                               // Parameters to pass to function
        1,                                  // Task priority (0 to configMAX_PRIORITIES -1)
        &xTaskB                             // Task handle
    );
        
    // initialize the ISR timer
    startISRTimer();

    //vTaskDelete(NULL); // no need to call this on app_main, by default this task is eliminated when it returns
    
    //esp_time_stop();
    //esp_timer_delete();
    return;
}


#else

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "example";

void app_main(void)
{
    ESP_LOGI(TAG, "Install temperature sensor, expected temp ranger range: 10~50 ℃");
    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));

    ESP_LOGI(TAG, "Enable temperature sensor");
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

    ESP_LOGI(TAG, "Read temperature");
    int cnt = 20;
    float tsens_value;
    while (cnt--) {
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));
        ESP_LOGI(TAG, "Temperature value %.02f ℃", tsens_value);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif