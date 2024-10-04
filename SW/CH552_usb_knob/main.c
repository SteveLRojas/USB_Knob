#include "CH552.H"
#include "System.h"
#include "CH552_GPIO.h"
#include "CH552_UART.h"
#include "CH552_TIMER.h"
#include "CH552_HID_CC_PAN_MOUSE.h"
#include "CH552_QUADRATURE_ENCODER.h"
#include "pseudo_random.h"

#define ENABLE_DEBUG 0
#define ENABLE_WIGGLER 1
#define REDUCE_RESOLUTION 1
#define POLL_ENCODER 1

//Pins:
// ENCA  = P32
// ENCB  = P33
// ENCSW = P14
// SW1  = P15
// SW2  = P16
// SW3  = P17
// LED1 = P35
// LED2 = P34
// LED3 = P11
// RXD  = P30
// TXD  = P31
// UDP  = P36
// UDM  = P37

#define KNOB_MODE_SCROLL 0
#define KNOB_MODE_PAN 1
#define KNOB_MODE_VOLUME 2

#if ENABLE_DEBUG
char code test_string[] = "Unicorn\n";
char code qenc_str[] = "qenc count: ";
char code pos_str[] = "Knob pos: ";
char code change_str[] = "Pos change: ";
char hex_str[4];
#endif

//Timers
volatile UINT16 mouse_timer = 0;
volatile UINT16 cc_timer = 0;
volatile UINT8 mouse_timeout = 0;
volatile UINT8 cc_timeout = 0;

//Knob state
UINT8 knob_mode = KNOB_MODE_SCROLL;
signed char scroll_speed = 1;
signed char pan_speed = 1;

#if ENABLE_WIGGLER
//Wiggler state
UINT8 wiggler_active = 0;
volatile UINT8 wiggler_update_vel = 0;
volatile UINT8 wiggler_update_pos = 0;
volatile UINT8 wiggler_update_times = 0;
UINT8 time = 0;
UINT16 run_time = 0;
UINT16 stop_time = 0;
signed char x_velocity;
signed char y_velocity;
signed short x_position;
signed short y_position;
signed short prev_x_position;
signed short prev_y_position;
signed char x_delta;
signed char y_delta;
#endif

void on_timer(void)
{
	++mouse_timer;
	++cc_timer;
	
	if(hid_mouse_idle_rate && ((UINT8)(mouse_timer >> 2) >= hid_mouse_idle_rate))
	{
		mouse_timeout = 1;
		mouse_timer = 0;
	}
	
	if(hid_cc_idle_rate && ((UINT8)(cc_timer >> 2) >= hid_cc_idle_rate))
	{
		cc_timeout = 1;
		cc_timer = 0;
	}

#if ENABLE_WIGGLER
	if((knob_mode == KNOB_MODE_PAN) && wiggler_active)
	{
		if(run_time)
		{
			if(time == 0x00)
			{
				wiggler_update_vel = 1;
			}
			if((time & 0x07) == 0x00)
			{
				wiggler_update_pos = 1;
			}
			--run_time;
		}
		else if(stop_time)
		{
			--stop_time;
		}
		else
		{
			wiggler_update_times = 1;
		}
		++time;
	}
#endif
}

#if ENABLE_DEBUG
void byte_to_hex(UINT8 value, char* buff)
{
	const char table[16] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
	buff[0] = table[(value >> 4) & 0x0f];
	buff[1] = table[(value) & 0x0f];
	buff[2] = '\0';
}
#endif

int main()
{
	UINT8 knob_sw;
	UINT8 knob_sw_prev;
	UINT8 knob_press_event;
	UINT8 knob_release_event;
	
	signed char knob_pos;
	signed char knob_pos_prev;
	signed char knob_pos_change;
	UINT8 knob_inc_event;
	UINT8 knob_dec_event;
	
	CfgFsys();

	gpio_set_mode(GPIO_MODE_INPUT, GPIO_PORT_1, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7); // ENCSW, SW123
	gpio_set_mode(GPIO_MODE_INPUT, GPIO_PORT_3, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3); // RXD, ENCAB
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_3, GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5); // TXD, LED12
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_1, GPIO_PORT_1); // LED3
	
#if ENABLE_DEBUG
	uart0_init(TIMER_1, BAUD_RATE, UART_0_P30_P31);
	timer_init(TIMER_0, NULL);
	timer_set_period(TIMER_0, FREQ_SYS / 1000ul);	//period is 1ms
#endif

	timer_init(TIMER_2, on_timer);
	timer_set_period(TIMER_2, FREQ_SYS / 1000ul);	//period is 1ms
	timer_start(TIMER_2);
	EA = 1;	//enable interupts
	E_DIS = 0;
	
#if ENABLE_DEBUG	
	//Blink LED once
	gpio_clear_pin(GPIO_PORT_1, GPIO_PIN_1);
	timer_long_delay(TIMER_0, 250);
	gpio_set_pin(GPIO_PORT_1, GPIO_PIN_1);
	timer_long_delay(TIMER_0, 250);
	uart_write_string(UART_0, test_string);
	
	byte_to_hex(RESET_KEEP, hex_str);
	hex_str[2] = '\n';
	hex_str[3] = '\0';
	uart_write_string(UART_0, hex_str);
#endif

	hid_init();
#if POLL_ENCODER
	qenc_init(QENC_MODE_POLLED);
#else
	qenc_init(QENC_MODE_INTERRUPT);
#endif
	pseudo_random_seed(0x1337BEEF);
	
	gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_5);	//turn on LED1
	knob_sw = gpio_read_pin(GPIO_PORT_1, GPIO_PIN_4);
	knob_sw_prev = knob_sw;
	while(TRUE)
	{	
		// Check for mode change and set indicators
		if(!gpio_read_pin(GPIO_PORT_1, GPIO_PIN_5))
		{
			knob_mode = KNOB_MODE_SCROLL;
			gpio_set_pin(GPIO_PORT_1, GPIO_PIN_1);	//turn off LED3
			gpio_set_pin(GPIO_PORT_3, GPIO_PIN_4);	//turn off LED2
			gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_5);	//turn on LED1
		}
		else if(!gpio_read_pin(GPIO_PORT_1, GPIO_PIN_6))
		{
			knob_mode = KNOB_MODE_PAN;
			gpio_set_pin(GPIO_PORT_1, GPIO_PIN_1);	//turn off LED3
			gpio_set_pin(GPIO_PORT_3, GPIO_PIN_5);	//turn off LED1
			gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_4);	//turn on LED2
		}
		else if(!gpio_read_pin(GPIO_PORT_1, GPIO_PIN_7))
		{
			knob_mode = KNOB_MODE_VOLUME;
			gpio_set_pin(GPIO_PORT_3, GPIO_PIN_4 | GPIO_PIN_5);	//turn off LED1 and LED2
			gpio_clear_pin(GPIO_PORT_1, GPIO_PIN_1);	//turn on LED3
		}
		
		// Get knob state
		knob_sw = gpio_read_pin(GPIO_PORT_1, GPIO_PIN_4);
		knob_press_event = (knob_sw_prev & ~knob_sw);
		knob_release_event = (knob_sw & ~knob_sw_prev);
		knob_sw_prev = knob_sw;
		
#if POLL_ENCODER
		qenc_poll();
#endif
		knob_pos = (signed char)qenc_count;
		knob_pos_change = knob_pos - knob_pos_prev;
#if REDUCE_RESOLUTION
		if((knob_pos_change < -1 ) || (knob_pos_change > 1))
		{
			knob_inc_event = (knob_pos_change > 0);
			knob_dec_event = (knob_pos_change < 0);
			knob_pos_prev = knob_pos;
		}
		else
		{
			knob_inc_event = 0;
			knob_dec_event = 0;
		}
#else
		knob_inc_event = (knob_pos_change > 0);
		knob_dec_event = (knob_pos_change < 0);
		knob_pos_prev = knob_pos;
#endif		
		
#if ENABLE_DEBUG		
		//debug prints
		if(knob_inc_event | knob_dec_event)
		{
			//print qenc_count
			uart_write_string(UART_0, qenc_str);
			byte_to_hex((UINT8)qenc_count, hex_str);
			hex_str[2] = '\n';
			hex_str[3] = '\0';
			uart_write_string(UART_0, hex_str);
			
			//print knob_pos
			uart_write_string(UART_0, pos_str);
			byte_to_hex(knob_pos, hex_str);
			hex_str[2] = '\n';
			hex_str[3] = '\0';
			uart_write_string(UART_0, hex_str);
			
			//print knob_pos_change
			uart_write_string(UART_0, change_str);
			byte_to_hex(knob_pos_change, hex_str);
			hex_str[2] = '\n';
			hex_str[3] = '\0';
			uart_write_string(UART_0, hex_str);
		}
#endif
		
		// Handle rotate/click
		switch(knob_mode)
		{
			case KNOB_MODE_SCROLL:
				if(gpio_read_pin(GPIO_PORT_1, GPIO_PIN_5))
				{
					if(knob_press_event)
						hid_mouse_press(HID_MOUSE_BTN_WHEEL);
					if(knob_release_event)
						hid_mouse_release(HID_MOUSE_BTN_WHEEL);
					if(knob_inc_event)
						hid_mouse_scroll(-scroll_speed, 0);
					if(knob_dec_event)
						hid_mouse_scroll(scroll_speed, 0);
				}
				else
				{
					if(knob_inc_event)
						++scroll_speed;
					if(knob_dec_event)
						--scroll_speed;
					if(scroll_speed < 1)
						scroll_speed = 1;
					if(scroll_speed > 10)
						scroll_speed = 10;
				}
				break;
			case KNOB_MODE_PAN:
				if(gpio_read_pin(GPIO_PORT_1, GPIO_PIN_6))
				{
#if ENABLE_WIGGLER
					if(knob_release_event)
						wiggler_active = !wiggler_active;
#else
					if(knob_press_event)
						hid_mouse_press(HID_MOUSE_BTN_WHEEL);
					if(knob_release_event)
						hid_mouse_release(HID_MOUSE_BTN_WHEEL);
#endif
					if(knob_inc_event)
						hid_mouse_scroll(0, pan_speed);
					if(knob_dec_event)
						hid_mouse_scroll(0, -pan_speed);
				}
				else
				{
					if(knob_inc_event)
						++pan_speed;
					if(knob_dec_event)
						--pan_speed;
					if(pan_speed < 1)
						pan_speed = 1;
					if(pan_speed > 10)
						pan_speed = 10;
				}
				break;
			case KNOB_MODE_VOLUME:
				if(knob_inc_event)
				{
					hid_cc_press(HID_CC_BTN_VOL_UP);
					while(hid_cc_report_pending);
					hid_cc_press(HID_CC_BTN_NONE);
				}
				if(knob_dec_event)
				{
					hid_cc_press(HID_CC_BTN_VOL_DOWN);
					while(hid_cc_report_pending);
					hid_cc_press(HID_CC_BTN_NONE);
				}
				
				if(knob_press_event)
					hid_cc_press(HID_CC_BTN_VOL_MUTE);
				if(knob_release_event)
					hid_cc_press(HID_CC_BTN_NONE);
				break;
			default:
				break;
		}

#if ENABLE_WIGGLER
		if(wiggler_update_vel)
		{
			pseudo_random_generate(16);
			x_velocity = pseudo_random_get_byte();
			y_velocity = pseudo_random_get_word() >> 8;
			if(x_velocity & 0x80)
				++x_velocity;
			if(y_velocity & 0x80)
				++y_velocity;
			wiggler_update_vel = 0;
		}

		if(wiggler_update_pos)
		{
			x_position += x_velocity;
			y_position += y_velocity;
			x_delta = (signed char)((x_position >> 4) - (prev_x_position >> 4));
			y_delta = (signed char)((y_position >> 4) - (prev_y_position >> 4));
			prev_x_position = x_position;
			prev_y_position = y_position;
			hid_mouse_move(x_delta, y_delta);
			wiggler_update_pos = 0;
		}
		
		if(wiggler_update_times)
		{
			pseudo_random_generate(16);
			run_time = pseudo_random_get_word();
			pseudo_random_generate(16);
			stop_time = pseudo_random_get_word();
			
			run_time = run_time >> 2;
			stop_time = stop_time >> 2;
			run_time += 1000;
			stop_time += 1000;
			wiggler_update_times = 0;
		}
#endif
		
		if(mouse_timeout)
		{
			hid_mouse_send_report();
			mouse_timeout = 0;
		}
		
		if(cc_timeout)
		{
			hid_cc_send_report();
			cc_timeout = 0;
		}
	}
}
