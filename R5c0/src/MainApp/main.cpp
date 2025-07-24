#include "FreeRTOS.h"
#include "task.h"

#include "xil_printf.h"
#include "xparameters.h"
#include "xgpio.h"
#include "xstatus.h"

#define DELAY_5s	5000UL
#define DELAY_1s	1000UL


//Thread implementation
static void myTask1(void *);
static void myTask2(void *);


//Task Handle
static TaskHandle_t th_myTask1;
static TaskHandle_t th_myTask2;

XGpio_Config *led1_cfg;
XGpio led1_io;
uint8_t led_status = 0;


int main( void )
{

	xil_printf( "My first FreeRTOS app\n" );



    led1_cfg = XGpio_LookupConfig(XPAR_AXI_GPIO_0_BASEADDR);
	int status = XGpio_CfgInitialize(&led1_io, led1_cfg, led1_cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XGpio_SetDataDirection(&led1_io, 1, 0x00);


	xTaskCreate(myTask1,
			    (const char *) "myTask1",
				configMINIMAL_STACK_SIZE,
				NULL,
				tskIDLE_PRIORITY,
				&th_myTask1);

	xTaskCreate(myTask2,
				(const char *) "myTask2",
				configMINIMAL_STACK_SIZE,
				NULL,
				tskIDLE_PRIORITY,
				&th_myTask2);

    // Start the Task
	vTaskStartScheduler();


	while(true);
}




static void myTask1(void* pvParameters)
{
  unsigned int count = 0;

  while(1)
  {
	  vTaskDelay(pdMS_TO_TICKS(DELAY_1s));
	  
      led_status = led_status == 1? 0:1;
      XGpio_DiscreteWrite(&led1_io, 1, led_status);
  }

}


static void myTask2(void* pvParameters)
{
  unsigned int count = 0;

  while(1)
  {
	  vTaskDelay(pdMS_TO_TICKS(DELAY_5s));
	  count++;
	  xil_printf("Thread 2 counter value: %u \r\n", count);
  }

}