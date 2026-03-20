/*
* SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "mqtt5_example";
static const char *tag = "GARAGE";

#define Estado_Inicio   0
#define Estado_Abierto  1
#define Estado_Cerrado  2
#define Estado_Abriendo 3
#define Estado_Cerrando 4
#define Estado_Stop     5
#define Estado_Error    6

#define Motor_ON   1
#define Motor_OFF  0
#define Lamp_ON    1
#define Lamp_OFF   0
#define Buzzer_ON  1
#define Buzzer_OFF 0
#define Enable_ON  1
#define Enable_OFF 0

#define fca_pin    17
#define fcc_pin    18
#define ftc_pin    16
#define bc_pin     32
#define ba_pin     33
#define bs_pin     25
#define be_pin     26

#define mc_pin     27
#define ma_pin     23
#define lamp_pin   12
#define buzzer_pin 13
#define enable_pin 15

struct signal {
    unsigned int fca;
    unsigned int fcc;
    unsigned int ftc;
    unsigned int bc;
    unsigned int ba;
    unsigned int bs;
    unsigned int be;

    unsigned int mc;
    unsigned int ma;
    unsigned int lamp;
    unsigned int buzzer;
    unsigned int enable;
} io;

int Estado_Actual    = Estado_Inicio;
int Estado_Siguiente = Estado_Inicio;
int recuperando      = 0;

TimerHandle_t xTimers;
int interval = 5000;
int timerId  = 1;

static bool          timer_iniciado = 0;
static volatile bool timer_expirado = 0;

esp_mqtt_client_handle_t mqtt_client = NULL;

/* ── MQTT ─────────────────────────────────────────────────────────────── */

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/garage/comando", 1);
        ESP_LOGI(TAG, "Suscrito a /garage/comando, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);

        if (strncmp(event->data, "abrir", event->data_len) == 0)
            Estado_Siguiente = Estado_Abriendo;
        else if (strncmp(event->data, "cerrar", event->data_len) == 0)
            Estado_Siguiente = Estado_Cerrando;
        else if (strncmp(event->data, "stop", event->data_len) == 0)
            Estado_Siguiente = Estado_Stop;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt5_app_start(void)
{
    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = true,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* ── MAQUINA DE ESTADOS ───────────────────────────────────────────────── */

esp_err_t set_timer(void);

void vTimerCallback(TimerHandle_t pxTimer)
{
    ESP_LOGI(tag, "Event was called from timer");
    timer_expirado = true;
}

esp_err_t set_timer(void)
{
    ESP_LOGI(tag, "Timer init configuration");

    xTimers = xTimerCreate(
        "Timer",
        pdMS_TO_TICKS(interval),
        pdFALSE,
        (void *)timerId,
        vTimerCallback);

    if (xTimers == NULL)
    {
        ESP_LOGE(tag, "Timer not created.");
    }
    else
    {
        if (xTimerStart(xTimers, 0) != pdPASS)
        {
            ESP_LOGE(tag, "Timer could not start");
        }
    }

    return ESP_OK;
}

int Func_Estado_Inicio(void)
{
    io.mc     = Motor_OFF;
    io.ma     = Motor_OFF;
    io.enable = Enable_OFF;
    io.lamp   = Lamp_OFF;
    io.buzzer = Buzzer_OFF;

    if (io.fca)
        Estado_Siguiente = Estado_Abierto;
    else if (io.fcc)
        Estado_Siguiente = Estado_Cerrado;

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Inicio", 0, 1, 0);
    printf("Estado actual:Inicio\n");
    return 0;
}

int Func_Estado_Abierto(void)
{
    io.mc     = Motor_OFF;
    io.ma     = Motor_OFF;
    io.enable = Enable_OFF;
    io.lamp   = Lamp_OFF;

    if (io.bc)
        Estado_Siguiente = Estado_Cerrando;

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Abierto", 0, 1, 0);
    printf("Estado actual:Abierto\n");
    return 0;
}

int Func_Estado_Cerrado(void)
{
    io.mc     = Motor_OFF;
    io.ma     = Motor_OFF;
    io.enable = Enable_OFF;
    io.lamp   = Lamp_OFF;

    if (io.ba)
        Estado_Siguiente = Estado_Abriendo;

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Cerrado", 0, 1, 0);
    printf("Estado actual:Cerrado\n");
    return 0;
}

int Func_Estado_Abriendo(void)
{
    io.mc     = Motor_OFF;
    io.ma     = Motor_ON;
    io.enable = Enable_ON;
    io.lamp   = Lamp_ON;

    if (io.fca)
        Estado_Siguiente = Estado_Abierto;
    else if (io.bs)
        Estado_Siguiente = Estado_Stop;
    else if (io.bc)
        Estado_Siguiente = Estado_Cerrando;

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Abriendo", 0, 1, 0);
    printf("Estado actual: Abriendo\n");
    return 0;
}

int Func_Estado_Cerrando(void)
{
    io.mc     = Motor_ON;
    io.ma     = Motor_OFF;
    io.enable = Enable_ON;
    io.lamp   = Lamp_ON;

    if (io.fcc)
    {
        Estado_Siguiente = Estado_Cerrado;
        recuperando      = 0;
    }
    else if (io.ba)
        Estado_Siguiente = Estado_Abriendo;
    else if (io.bs)
        Estado_Siguiente = Estado_Stop;

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Cerrando", 0, 1, 0);
    printf("Estado actual:Cerrando\n");
    return 0;
}

int Func_Estado_Stop(void)
{
    io.mc     = Motor_OFF;
    io.ma     = Motor_OFF;
    io.enable = Enable_OFF;
    io.lamp   = Lamp_ON;

    if (io.ba)
        Estado_Siguiente = Estado_Abriendo;
    else if (io.bc)
        Estado_Siguiente = Estado_Cerrando;

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Stop", 0, 1, 0);
    printf("Estado actual:Stop\n");
    return 0;
}

// Logica del timer en Error:
// - Al entrar, arranca el timer
// - Si fca o fcc se sueltan antes de que expire → va a Cerrando
// - Si el timer expira y siguen ambos activos → se queda en Error
int Func_Estado_Error(void)
{
    io.mc     = Motor_OFF;
    io.ma     = Motor_OFF;
    io.enable = Enable_OFF;
    io.lamp   = Lamp_ON;
    io.buzzer = Buzzer_ON;

    if (!timer_iniciado)
    {
        timer_expirado = false;
        set_timer();
        timer_iniciado = true;
    }

    if (!io.fcc || !io.fca)
    {
        if (timer_iniciado && xTimers != NULL)
        {
            xTimerStop(xTimers, 0);
            timer_iniciado = false;
            timer_expirado = false;
        }
        Estado_Siguiente = Estado_Cerrando;
        recuperando      = 1;
    }

    esp_mqtt_client_publish(mqtt_client, "/garage/estado", "Error", 0, 1, 0);
    printf("Estado actual:Error\n");
    return 0;
}

/* ── APP MAIN ─────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = "Campus_ITLA",
            .password = "",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    vTaskDelay(pdMS_TO_TICKS(5000));

    mqtt5_app_start();

    ESP_LOGI(tag, "Iniciando sistema de puerta de garage");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << fca_pin) |
                        (1ULL << fcc_pin) |
                        (1ULL << ftc_pin) |
                        (1ULL << bc_pin)  |
                        (1ULL << ba_pin)  |
                        (1ULL << bs_pin)  |
                        (1ULL << be_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_direction(mc_pin,     GPIO_MODE_OUTPUT);
    gpio_set_direction(ma_pin,     GPIO_MODE_OUTPUT);
    gpio_set_direction(lamp_pin,   GPIO_MODE_OUTPUT);
    gpio_set_direction(buzzer_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);

    while (1)
    {
        io.fca = gpio_get_level(fca_pin);
        io.fcc = gpio_get_level(fcc_pin);
        io.ftc = gpio_get_level(ftc_pin);
        io.bc  = gpio_get_level(bc_pin);
        io.ba  = gpio_get_level(ba_pin);
        io.bs  = gpio_get_level(bs_pin);
        io.be  = gpio_get_level(be_pin);

        Estado_Actual = Estado_Siguiente;

        if (io.fca && io.fcc && !recuperando)
            Estado_Siguiente = Estado_Error;

        switch (Estado_Actual)
        {
        case Estado_Inicio:
            Func_Estado_Inicio();
            break;

        case Estado_Abierto:
            Func_Estado_Abierto();
            break;

        case Estado_Cerrado:
            Func_Estado_Cerrado();
            break;

        case Estado_Abriendo:
            Func_Estado_Abriendo();
            break;

        case Estado_Cerrando:
            Func_Estado_Cerrando();
            break;

        case Estado_Stop:
            Func_Estado_Stop();
            break;

        case Estado_Error:
            Func_Estado_Error();
            break;

        default:
            ESP_LOGE(tag, "Estado desconocido: %d", Estado_Actual);
            Estado_Siguiente = Estado_Inicio;
            break;
        }

        gpio_set_level(mc_pin,     io.mc);
        gpio_set_level(ma_pin,     io.ma);
        gpio_set_level(lamp_pin,   io.lamp);
        gpio_set_level(buzzer_pin, io.buzzer);
        gpio_set_level(enable_pin, io.enable);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
