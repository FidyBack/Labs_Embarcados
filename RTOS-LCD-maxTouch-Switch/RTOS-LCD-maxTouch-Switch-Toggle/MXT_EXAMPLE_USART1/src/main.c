#include <asf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "conf_board.h"
#include "conf_uart_serial.h"
#include "maxTouch/maxTouch.h"
#include "tfont.h"
#include "digital521.h"
#include "icons/lavagens.h"

/************************************************************************/
/* prototypes                                                           */
/************************************************************************/

/************************************************************************/
/* LCD + TOUCH                                                          */
/************************************************************************/
#define MAX_ENTRIES        10

struct ili9488_opt_t g_ili9488_display_opt;

/************************************************************************/
/* Botoes lcd                                                           */
/************************************************************************/
typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t colorOn;
	uint32_t colorOff;
	uint32_t x;
	uint32_t y;
	uint8_t status;
	void (*callback)(t_but);
} t_but;

/************************************************************************/
/* RTOS                                                                  */
/************************************************************************/
#define TASK_MXT_STACK_SIZE            (2*1024/sizeof(portSTACK_TYPE))
#define TASK_MXT_STACK_PRIORITY        (tskIDLE_PRIORITY)

#define TASK_LCD_STACK_SIZE            (4*1024/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY        (tskIDLE_PRIORITY)

typedef struct {
  uint x;
  uint y;
} touchData;

QueueHandle_t xQueueTouch;

/************************************************************************/
/* handler/callbacks                                                    */
/************************************************************************/

/************************************************************************/
/* RTOS hooks                                                           */
/************************************************************************/

/**
* \brief Called if stack overflow during execution
*/
extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName)
{
  printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
  /* If the parameters have been corrupted then inspect pxCurrentTCB to
  * identify which task has overflowed its stack.
  */
  for (;;) {
  }
}

/**
* \brief This function is called by FreeRTOS idle task
*/
extern void vApplicationIdleHook(void)
{
  pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
}

/**
* \brief This function is called by FreeRTOS each tick
*/
extern void vApplicationTickHook(void)
{
}

extern void vApplicationMallocFailedHook(void)
{
  /* Called if a call to pvPortMalloc() fails because there is insufficient
  free memory available in the FreeRTOS heap.  pvPortMalloc() is called
  internally by FreeRTOS API functions that create tasks, queues, software
  timers, and semaphores.  The size of the FreeRTOS heap is set by the
  configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */

  /* Force an assert. */
  configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* init                                                                 */
/************************************************************************/

static void configure_lcd(void){
  /* Initialize display parameter */
  g_ili9488_display_opt.ul_width = ILI9488_LCD_WIDTH;
  g_ili9488_display_opt.ul_height = ILI9488_LCD_HEIGHT;
  g_ili9488_display_opt.foreground_color = COLOR_CONVERT(COLOR_WHITE);
  g_ili9488_display_opt.background_color = COLOR_CONVERT(COLOR_WHITE);

  /* Initialize LCD */
  ili9488_init(&g_ili9488_display_opt);
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

void draw_screen(void) {
  ili9488_set_foreground_color(COLOR_CONVERT(COLOR_WHITE));
  ili9488_draw_filled_rectangle(0, 0, ILI9488_LCD_WIDTH-1, ILI9488_LCD_HEIGHT-1);
}

void draw_button_new(t_but but){
	uint32_t color;
	if(but.status)
		color = but.colorOn;
	else
		color = but.colorOff;

	ili9488_set_foreground_color(COLOR_CONVERT(color));
	ili9488_draw_filled_rectangle(but.x-but.width/2, but.y-but.height/2,
	but.x+but.width/2, but.y+but.height/2);
}

void draw_image_lavanda(t_but but){
	if(but.status) {
		ili9488_set_foreground_color(COLOR_CONVERT(COLOR_WHITE));
		ili9488_draw_filled_rectangle(but.x + but.width/2 + 10, but.y - but.height/2,
			but.x + but.width/2 + 50 + lavagens.width, but.y - but.height/2 + lavagens.height);
	}
	else {
		ili9488_draw_pixmap(but.x + but.width/2 + 30, but.y - but.height/2, 
			lavagens.width, lavagens.height, lavagens.data);
	}
}

uint32_t convert_axis_system_x(uint32_t touch_y) {
  // entrada: 4096 - 0 (sistema de coordenadas atual)
  // saida: 0 - 320
  return ILI9488_LCD_WIDTH - ILI9488_LCD_WIDTH*touch_y/4096;
}

uint32_t convert_axis_system_y(uint32_t touch_x) {
  // entrada: 0 - 4096 (sistema de coordenadas atual)
  // saida: 0 - 320
  return ILI9488_LCD_HEIGHT*touch_x/4096;
}

void font_draw_text(tFont *font, const char *text, int x, int y, int spacing) {
  char *p = text;
  while(*p != NULL) {
    char letter = *p;
    int letter_offset = letter - font->start_char;
    if(letter <= font->end_char) {
      tChar *current_char = font->chars + letter_offset;
      ili9488_draw_pixmap(x, y, current_char->image->width, current_char->image->height, current_char->image->data);
      x += current_char->image->width + spacing;
    }
    p++;
  }
}

void mxt_handler(struct mxt_device *device, uint *x, uint *y)
{
  /* USART tx buffer initialized to 0 */
  uint8_t i = 0; /* Iterator */

  /* Temporary touch event data struct */
  struct mxt_touch_event touch_event;
  
  /* first touch only */
  uint first = 0;

  /* Collect touch events and put the data in a string,
  * maximum 2 events at the time */
  do {

    /* Read next next touch event in the queue, discard if read fails */
    if (mxt_read_touch_event(device, &touch_event) != STATUS_OK) {
      continue;
    }
    
    /************************************************************************/
    /* Envia dados via fila RTOS                                            */
    /************************************************************************/
    if(first == 0 ){
      *x = convert_axis_system_x(touch_event.y);
      *y = convert_axis_system_y(touch_event.x);
      first = 1;
    }
    
    i++;

    /* Check if there is still messages in the queue and
    * if we have reached the maximum numbers of events */
  } while ((mxt_is_message_pending(device)) & (i < MAX_ENTRIES));
}

int process_touch(t_but botoes[], touchData touch, uint32_t n){
	for(int but = 0; but < n; but++) {
		if (touch.x > botoes[but].x - botoes[but].width/2 && touch.x < botoes[but].x + botoes[but].width/2 &&
		touch.y > botoes[but].y - botoes[but].height/2 && touch.y < botoes[but].y + botoes[but].height/2) {
			return but;
		}
	}
	return -1;
}

void but_callback(t_but *but){
	but->status = !but->status;
	draw_button_new(*but);
	draw_image_lavanda(*but);
}

/************************************************************************/
/* tasks                                                                */
/************************************************************************/

void task_mxt(void){
	struct mxt_device device; /* Device data container */
	mxt_init(&device);       	/* Initialize the mXT touch device */
	touchData touch;          /* touch queue data type*/

	while (true) {
		/* Check for any pending messages and run message handler if any
		* message is found in the queue */
		if (mxt_is_message_pending(&device)) {
			mxt_handler(&device, &touch.x, &touch.y);
			xQueueSend( xQueueTouch, &touch, 0);           /* send mesage to queue */
			vTaskDelay(200);

			 // limpa touch
			while (mxt_is_message_pending(&device)){
			mxt_handler(&device, NULL, NULL);
			vTaskDelay(50);
			}
		}
		vTaskDelay(300);
	}
}

void task_lcd(void){
	xQueueTouch = xQueueCreate( 10, sizeof( touchData ) );
	configure_lcd();
	draw_screen();

	// Cria Bot�es
	t_but but0 = {.width = 120, .height = 75, .colorOn = COLOR_TOMATO,
		.colorOff = COLOR_BLACK, .x = ILI9488_LCD_WIDTH/4, .y = 40, .status = 1, .callback = &but_callback };
		
	t_but but1 = {.width = 120, .height = 75, .colorOn = COLOR_INDIGO,
		.colorOff = COLOR_BLACK, .x = ILI9488_LCD_WIDTH/4, .y = 140, .status = 1, .callback = &but_callback };
		
	t_but but2 = {.width = 120, .height = 75, .colorOn = COLOR_GOLD,
		.colorOff = COLOR_BLACK, .x = ILI9488_LCD_WIDTH/4, .y = 240, .status = 1, .callback = &but_callback };
		
	t_but but3 = {.width = 120, .height = 75, .colorOn = COLOR_NAVY,
		.colorOff = COLOR_BLACK, .x = ILI9488_LCD_WIDTH/4, .y = 340, .status = 1, .callback = &but_callback };
		
	t_but but4 = {.width = 120, .height = 75, .colorOn = COLOR_LIGHTBLUE,
		.colorOff = COLOR_BLACK, .x = ILI9488_LCD_WIDTH/4, .y = 440, .status = 1, .callback = &but_callback };
		
	t_but botoes[] = {but0, but1, but2, but3, but4};
	
	for(int but = 0; but < sizeof(botoes)/sizeof(botoes[0]); but++) {
		draw_button_new(botoes[but]);
	}

	// strut local para armazenar msg enviada pela task do mxt
	touchData touch;
  
	while (true) {
		if (xQueueReceive( xQueueTouch, &(touch), ( TickType_t )  500 / portTICK_PERIOD_MS)) {
			int b = process_touch(botoes, touch, sizeof(botoes)/sizeof(botoes[0]));
			if(b >= 0){
				botoes[b].callback(&botoes[b]);
			}
			printf("x:%d y:%d\n", touch.x, touch.y);
			printf("botao: %d\n", b);
		}
	}
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/

int main(void)
{
  /* Initialize the USART configuration struct */
  const usart_serial_options_t usart_serial_options = {
    .baudrate     = USART_SERIAL_EXAMPLE_BAUDRATE,
    .charlength   = USART_SERIAL_CHAR_LENGTH,
    .paritytype   = USART_SERIAL_PARITY,
    .stopbits     = USART_SERIAL_STOP_BIT
  };

  sysclk_init(); /* Initialize system clocks */
  board_init();  /* Initialize board */
  
  /* Initialize stdio on USART */
  stdio_serial_init(USART_SERIAL_EXAMPLE, &usart_serial_options);
  
  /* Create task to handler touch */
  if (xTaskCreate(task_mxt, "mxt", TASK_MXT_STACK_SIZE, NULL, TASK_MXT_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create test led task\r\n");
  }
  
  /* Create task to handler LCD */
  if (xTaskCreate(task_lcd, "lcd", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create test led task\r\n");
  }
  
  /* Start the scheduler. */
  vTaskStartScheduler();

  while(1){

  }


  return 0;
}