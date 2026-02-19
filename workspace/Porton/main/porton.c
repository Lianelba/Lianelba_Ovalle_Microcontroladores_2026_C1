//Nombre: Lianelba Ovalle Gil
//Matricula: 2023-1758
//Materia: Microcontroladores
//Profesor: Carlos Pichardo

#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/timers.h"

#define Estado_Inicio 0
#define Estado_Abierto 1
#define Estado_Cerrado 2
#define Estado_Abriendo 3
#define Estado_Cerrando 4
#define Estado_Error 6
#define Estado_Stop 7

#define Motor_ON 1
#define Motor_OFF 0
#define Lamp_ON 1
#define Lamp_OFF 0
#define Buzzer_ON 1
#define Buzzer_OFF 0

struct signal {
    unsigned int fca; //final carrera abierto
    unsigned int fcc; //final carrera cerrado
    unsigned int ftc; //fotoresistor
    unsigned int bc;  //boton cerrar
    unsigned int ba;  //boton abrir
    unsigned int bs;  //boton stop
    unsigned int be;      //boton error
    //unsigned int pp; no se que hace
    unsigned int mc;      //motor cerrando
    unsigned int ma;      //motot abriendo
    unsigned int lamp;    //lampara
    unsigned int buzzer;  //buzzer
} io;

int Estado_Actual = Estado_Inicio;
int Estado_Siguiente = Estado_Inicio;
int Estado_Anterior = Estado_Stop;

TimerHandle_t xTimers;
int interval = 100;
int timerId = 1;



esp_err_t set_timer(void);

void vTimerCallback(TimerHandle_t pxTimer)
{
	ESP_LOGI(tag, "Event was called from timer");
}

int Func_Estado_Inicio(void);
int Func_Estado_Abierto(void);
int Func_Estado_Cerrado(void);
int Func_Estado_Abriendo(void);
int Func_Estado_Cerrando(void);
int Func_Estado_Stop(void);
int Func_Estado_Error(void);

esp_err_t set_timer (void)
{
	ESP_LOGI(tag, "Timer init configuration");
	xTimers = xTimerCreate ( "Timer",
								(pdMS_TO_TICKS(interval)),
								pdTRUE,
								(void *)timerId,
								vTimerCallback
							  );
	if (xTimers == NULL)
	{
		ESP_LOGE(tag, "Timer nor created.");
	}
	else
	{
		if (xTimerStart(xTimers, 0) != pdPASS)
		{
			ESP_LOGI(tag, "Timer could not be set into the Active state");
		}
	}
	return ESP_OK;
}

int Func_Estado_Inicio(void) {

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;
    io.buzzer = Buzzer_OFF;

    if (io.fca && io.bc)
        Estado_Siguiente = Estado_Abriendo;
    else if (io.fcc && io.ba)
        Estado_Siguiente = Estado_Cerrando;
    else if (io.fcc && io.fca)
        Estado_Siguiente = Estado_Error;

    return 0;
}

int Func_Estado_Abierto(void) {

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;

    if (io.bc) 
        Estado_Siguiente = Estado_Cerrando;


    return 0;
}

int Func_Estado_Cerrado(void) {

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;

    if (io.ba)
        Estado_Siguiente = Estado_Abriendo;


    return 0;
}

int Func_Estado_Abriendo(void) {

    io.mc = Motor_OFF;
    io.ma = Motor_ON;
    io.lamp = Lamp_ON;

    if (io.fca)
        Estado_Siguiente = Estado_Abierto;
    else if (io.bs)
		Estado_Siguiente = Estado_Stop;
    else if (io.bc)
    	Estado_Siguiente = Estado_Cerrando;


    return 0;
}

int Func_Estado_Cerrando(void) {

    io.mc = Motor_ON;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;

    if (io.ba) {
        Estado_Siguiente = Estado_Abriendo;
    }
    else if (io.bs){
		Estado_Siguiente = Estado_Stop;
	}
	else if (io.fcc){
		Estado_Siguiente = Estado_Cerrado;
	}

    return 0;
}

int Func_Estado_Stop(void) {

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;

    if (io.ba)
        Estado_Siguiente = Estado_Abriendo;
    else if (io.bc)
    	Estado_Siguiente = Estado_Cerrando;


    return 0;
}

static const char *tag = "GARAGE";

int Func_Estado_Error(void) {

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;
    io.buzzer = Buzzer_ON;

    if (io.fcc & io.fca) {
        set_timer();
        if (io.fcc & io.fca & ~io.ftc) {
            Estado_Siguiente = Estado_Cerrando;
        }
    }

    return 0;
}

void app_main(void) {

    ESP_LOGI(tag, "Iniciando sistema de puerta de garage");

    io.fca = 0;
    io.fcc = 0;
    io.ftc = 0;
    io.bc  = 0;
    io.ba  = 0;
    io.bs  = 0;
    io.be  = 0;
    io.mc  = Motor_OFF;
    io.ma  = Motor_OFF;
    io.lamp   = Lamp_OFF;
    io.buzzer = Buzzer_OFF;

    set_timer();

    while (1) {

        Estado_Actual = Estado_Siguiente;

        switch (Estado_Actual) {
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

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

