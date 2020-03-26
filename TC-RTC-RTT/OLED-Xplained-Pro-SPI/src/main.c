/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

/************************************************************************/
/* DEFINES                                                              */
/************************************************************************/

typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t seccond;
} calendar;

#define LED_PIO_ID0 ID_PIOA
#define LED_PIO0 PIOA
#define LED_PIN0 0
#define LED_IDX_MASK0 (1 << LED_PIN0)

#define LED_PIO_ID1 ID_PIOC
#define LED_PIO1 PIOC
#define LED_PIN1 30
#define LED_IDX_MASK1 (1 << LED_PIN1)

#define LED_PIO_ID2 ID_PIOC
#define LED_PIO2 PIOC
#define LED_PIN2 8
#define LED_IDX_MASK2 (1 << LED_PIN2)

/************************************************************************/
/* VAR globais                                                          */
/************************************************************************/

volatile Bool f_rtt_alarme = false;
volatile char flag_tc = 0;
volatile char flag_rtc = 0;

/************************************************************************/
/* PROTOTYPES                                                           */
/************************************************************************/

void pin_toggle(Pio *pio, uint32_t mask);
void LED_init0(int estado);
void LED_init1(int estado);
void LED_initplaca(int estado);
void pisca_led0(int n, int t);
void pisca_ledplaca(int n, int t);

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type);
static void RTT_init(uint16_t pllPreScale, uint32_t IrqNPulses);
void TC_init(Tc * TC, int ID_TC, int TC_CHANNEL, int freq);

/************************************************************************/
/* Handlers                                                             */
/************************************************************************/

/**
*  Interrupt handler for TC1 interrupt.
*/
void TC1_Handler(void){
	volatile uint32_t ul_dummy;

	/****************************************************************
	* Devemos indicar ao TC que a interrupção foi satisfeita.
	******************************************************************/
	ul_dummy = tc_get_status(TC0, 1);

	/* Avoid compiler warning */
	UNUSED(ul_dummy);

	/** Muda o estado do LED */
	flag_tc = 1;
}

/**
* \brief Interrupt handler for the RTC. Refresh the display.
*/
void RTC_Handler(void)
{
	uint32_t ul_status = rtc_get_status(RTC);

	/*
	*  Verifica por qual motivo entrou
	*  na interrupcao, se foi por segundo
	*  ou Alarm
	*/
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {
		rtc_clear_status(RTC, RTC_SCCR_SECCLR);
	}
	
	/* Time or date alarm */
	if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
			rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
      flag_rtc = 1;
	}
	
	rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
	rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
	rtc_clear_status(RTC, RTC_SCCR_CALCLR);
	rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}


void RTT_Handler(void)
{
  uint32_t ul_status;

  /* Get RTT status - ACK */
  ul_status = rtt_get_status(RTT);

  /* IRQ due to Time has changed */
  if ((ul_status & RTT_SR_RTTINC) == RTT_SR_RTTINC) {  }

  /* IRQ due to Alarm */
  if ((ul_status & RTT_SR_ALMS) == RTT_SR_ALMS) {
      pin_toggle(LED_PIO1, LED_IDX_MASK1);    // BLINK Led
      f_rtt_alarme = true;                  // flag RTT alarme
   }  
}

/************************************************************************/
/* Funcoes                                                              */
/************************************************************************/

/*
 * @Brief Pisca LED placa
 */
void pisca_led0(int n, int t){
  for (int i=0;i<n;i++){
    pio_clear(LED_PIO0, LED_IDX_MASK0);
    delay_ms(t);
    pio_set(LED_PIO0, LED_IDX_MASK0);
    delay_ms(t);
  }
}

void pisca_ledplaca(int n, int t){
	for (int i=0;i<n;i++){
		pio_clear(LED_PIO2, LED_IDX_MASK2);
		delay_ms(t);
		pio_set(LED_PIO2, LED_IDX_MASK2);
		delay_ms(t);
	}
}

void pin_toggle(Pio *pio, uint32_t mask){
	if(pio_get_output_data_status(pio, mask))
	pio_clear(pio, mask);
	else
	pio_set(pio,mask);
}

/**
* @Brief Inicializa o pino do LED
*/
void LED_init0(int estado){
	pmc_enable_periph_clk(LED_PIO_ID0);
	pio_set_output(LED_PIO0, LED_IDX_MASK0, estado, 0, 0);
};

void LED_init1(int estado){
	pmc_enable_periph_clk(LED_PIO_ID1);
	pio_set_output(LED_PIO1, LED_IDX_MASK1, estado, 0, 0);
};

void LED_initplaca(int estado){
	pmc_enable_periph_clk(LED_PIO_ID2);
	pio_set_output(LED_PIO2, LED_IDX_MASK2, estado, 0, 0 );
};


/**
* Configura TimerCounter (TC) para gerar uma interrupcao no canal (ID_TC e TC_CHANNEL)
* na taxa de especificada em freq.
*/
void TC_init(Tc * TC, int ID_TC, int TC_CHANNEL, int freq){
	uint32_t ul_div;
	uint32_t ul_tcclks;
	uint32_t ul_sysclk = sysclk_get_cpu_hz();

	/* Configura o PMC */
	/* O TimerCounter é meio confuso
	o uC possui 3 TCs, cada TC possui 3 canais
	TC0 : ID_TC0, ID_TC1, ID_TC2
	TC1 : ID_TC3, ID_TC4, ID_TC5
	TC2 : ID_TC6, ID_TC7, ID_TC8
	*/
	pmc_enable_periph_clk(ID_TC);

	/** Configura o TC para operar em  4Mhz e interrupçcão no RC compare */
	tc_find_mck_divisor(freq, ul_sysclk, &ul_div, &ul_tcclks, ul_sysclk);
	tc_init(TC, TC_CHANNEL, ul_tcclks | TC_CMR_CPCTRG);
	tc_write_rc(TC, TC_CHANNEL, (ul_sysclk / ul_div) / freq);

	/* Configura e ativa interrupçcão no TC canal 0 */
	/* Interrupção no C */
	NVIC_EnableIRQ((IRQn_Type) ID_TC);
	tc_enable_interrupt(TC, TC_CHANNEL, TC_IER_CPCS);

	/* Inicializa o canal 0 do TC */
	tc_start(TC, TC_CHANNEL);
}

static void RTT_init(uint16_t pllPreScale, uint32_t IrqNPulses)
{
  uint32_t ul_previous_time;

  /* Configure RTT for a 1 second tick interrupt */
  rtt_sel_source(RTT, false);
  rtt_init(RTT, pllPreScale);
  
  ul_previous_time = rtt_read_timer_value(RTT);
  while (ul_previous_time == rtt_read_timer_value(RTT));
  
  rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);

  /* Enable RTT interrupt */
  NVIC_DisableIRQ(RTT_IRQn);
  NVIC_ClearPendingIRQ(RTT_IRQn);
  NVIC_SetPriority(RTT_IRQn, 0);
  NVIC_EnableIRQ(RTT_IRQn);
  rtt_enable_interrupt(RTT, RTT_MR_ALMIEN);
}

/**
* Configura o RTC para funcionar com interrupcao de alarme
*/
void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type){
	/* Configura o PMC */
	pmc_enable_periph_clk(ID_RTC);

	/* Default RTC configuration, 24-hour mode */
	rtc_set_hour_mode(rtc, 0);

	/* Configura data e hora manualmente */
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.seccond);

	/* Configure RTC interrupts */
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 0);
	NVIC_EnableIRQ(id_rtc);

	/* Ativa interrupcao via alarme */
	rtc_enable_interrupt(rtc,  irq_type);
}

/************************************************************************/
/* Main Code	                                                        */
/************************************************************************/

int main (void)
{
	board_init();
	sysclk_init();
	delay_init();
	LED_init0(0);
	LED_init1(0);
	LED_initplaca(0);
	WDT->WDT_MR = WDT_MR_WDDIS;
	
	/** Configura timer TC0, canal 1 */
	TC_init(TC0, ID_TC1, 1, 4);
	
	// Inicializa RTT com IRQ no alarme.
	f_rtt_alarme = true;
	
	/** Configura RTC */
	calendar rtc_initial = {2018, 3, 19, 12, 15, 45 ,1};
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_ALREN);
	
	/* configura alarme do RTC */
	rtc_set_date_alarm(RTC, 1, rtc_initial.month, 1, rtc_initial.day);
	rtc_set_time_alarm(RTC, 1, rtc_initial.hour, 1, rtc_initial.minute, 1, rtc_initial.seccond + 20);

	//// Init OLED
	//gfx_mono_ssd1306_init();
	//// Escreve na tela um circulo e um texto
	//gfx_mono_draw_filled_circle(20, 16, 16, GFX_PIXEL_SET, GFX_WHOLE);
	//gfx_mono_draw_string("mundo", 50,16, &sysfont);

	/* Insert application code here, after the board has been initialized. */
	while(1) {
		if(flag_tc){
			pisca_led0(1,10);
			flag_tc = 0;
		}
		
		if (f_rtt_alarme){
		  /*
		   * IRQ apos 4s -> 8*0.5
		   */
		  uint16_t pllPreScale = (int) (((float) 32768) / 4.0);
		  uint32_t irqRTTvalue = 8;
      
		  // reinicia RTT para gerar um novo IRQ
		  RTT_init(pllPreScale, irqRTTvalue);         
      
		  f_rtt_alarme = false;
		}
		
		if(flag_rtc){
			pisca_ledplaca(5, 200);
			flag_rtc = 0;
		}
		
		pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
	}
}
