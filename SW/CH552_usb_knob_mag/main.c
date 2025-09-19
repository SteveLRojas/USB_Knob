#include "CH552.H"
#include "CH552_RCC.h"
#include "CH552_GPIO.h"
#include "CH552_UART.h"
#include "CH552_TIMER.h"
#include "CH552_HID_CC_PAN_MOUSE.h"
#include "CH552_I2C.h"
#include "CH552_AS5600.h"
#include "pseudo_random.h"

#define BAUD_RATE		125000ul
#define ENABLE_DEBUG	0
#define ENABLE_WIGGLER	1
#define SPEED_STEPS		16
#define FINE_SHIFT		7
#define COARSE_HYSTERESIS	128
#define FINE_HYSTERESIS		4
#define DEBOUNCE_SAMPLES	8

//Pins:
// I2C_SDA = P10
// AS_OUT = P11
// I2C_SCL = P14
// SW1 = P15
// SW2 = P16
// SW3 = P17
// RXD = P30
// TXD = P31
// DIR = P32
// LED3 = P33
// LED2 = P34
// LED1 = P35
// UDP = P36
// UDM = P37

#define KNOB_MODE_SCROLL 0
#define KNOB_MODE_PAN 1
#define KNOB_MODE_VOLUME 2

#if ENABLE_DEBUG
char code test_string[] = "Unicorn\n";
char code pos_raw_str[] = "knob_pos_raw: ";
char code pos_coarse_str[] = "knob_pos_coarse: ";
char code pos_fine_str[] = "knob_pos_fine: ";
char code pos_change_str[] = "knob_pos_change: ";
char code str_bad_command[] = "Bad command!\n";
char hex_str[6];
UINT8 datagram[2];
UINT8 temp;
UINT8 count;
UINT16 as_read;
UINT8 reporting_enabled = 0;
#endif

//Timers
volatile UINT16 mouse_timer = 0;
volatile UINT16 cc_timer = 0;
volatile UINT8 mouse_timeout = 0;
volatile UINT8 cc_timeout = 0;

//Knob state
UINT8 knob_mode = KNOB_MODE_SCROLL;
UINT8 knob_mode_prev = KNOB_MODE_SCROLL;
signed char scroll_speed = SPEED_STEPS / 2;
signed char pan_speed = SPEED_STEPS / 2;

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
	if((knob_mode != KNOB_MODE_VOLUME) && wiggler_active)
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
char code hex_table[16] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
void byte_to_hex(UINT8 value, char* buff)
{
	buff[0] = hex_table[(value >> 4) & 0x0F];
	buff[1] = hex_table[(value) & 0x0F];
	buff[2] = '\0';
}
#endif

int main()
{
	UINT8 sw1_state;
	UINT8 sw2_state;
	UINT8 sw3_state;
	UINT8 sw1_count;
	UINT8 sw2_count;
	UINT8 sw3_count;
	UINT8 sw1_press_event;
	UINT8 sw2_press_event;
	UINT8 sw3_press_event;
	UINT8 sw1_release_event;
	UINT8 sw2_release_event;
	UINT8 sw3_release_event;
	
	signed short knob_pos_raw;
	signed short knob_pos_coarse;
	signed short knob_pos_fine;
	signed short knob_pos_change;
	UINT8 knob_inc_event;
	UINT8 knob_dec_event;
	
	signed short pos_accum;
	signed short pos_accum_prev;
	
	rcc_set_clk_freq(RCC_CLK_FREQ_24M);
	
	gpio_set_mode(GPIO_MODE_INPUT, GPIO_PORT_1, GPIO_PIN_1 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);	//AS_OUT, SW1, SW2, SW3
	gpio_set_mode(GPIO_MODE_OD, GPIO_PORT_1, GPIO_PIN_0 | GPIO_PIN_4);	//I2C_SDA, I2C_SCL
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_3, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);	//TXD, DIR, LED3, LED2, LED1
	gpio_set_mode(GPIO_MODE_INPUT, GPIO_PORT_3, GPIO_PIN_0);	//RXD
	gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_2);	//Drive DIR low
	
	i2c_init(GPIO_PORT_1, GPIO_PIN_0, GPIO_PORT_1, GPIO_PIN_4);
	
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
	
	if(rcc_get_rst_typ() == RCC_RST_TYP_WDOG)
	{
		rcc_delay_ms(500);
	}
	
#if ENABLE_DEBUG
	//Blink LED once
	gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
	timer_long_delay(TIMER_0, 250);
	gpio_set_pin(GPIO_PORT_3, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
	timer_long_delay(TIMER_0, 250);
	uart_write_string(UART_0, test_string);
	
	byte_to_hex(RESET_KEEP, hex_str);
	hex_str[2] = '\n';
	hex_str[3] = '\0';
	uart_write_string(UART_0, hex_str);
#endif
	
	hid_init();
	pseudo_random_seed(0xDEADBEEF);
	rcc_reload_wdog(0x00);
	rcc_set_wdog_rst_en(RCC_WDOG_ENABLED);
	
	gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_5);	//turn on LED1
	sw1_state = gpio_read_pin(GPIO_PORT_1, GPIO_PIN_5);
	sw2_state = gpio_read_pin(GPIO_PORT_1, GPIO_PIN_6);
	sw3_state = gpio_read_pin(GPIO_PORT_1, GPIO_PIN_7);
	sw1_count = 0;
	sw2_count = 0;
	sw3_count = 0;
	knob_pos_coarse = as5600_read_word(AS_REG_RAW_ANGLE) << 4;
	knob_pos_fine = knob_pos_coarse;
	pos_accum = 0;
	pos_accum_prev = 0;
	while(TRUE)
	{
		//Debounce buttons
		if(gpio_read_pin(GPIO_PORT_1, GPIO_PIN_5))
			sw1_count += (sw1_count < DEBOUNCE_SAMPLES);
		else
			sw1_count -= (sw1_count != 0);
		
		if(gpio_read_pin(GPIO_PORT_1, GPIO_PIN_6))
			sw2_count += (sw2_count < DEBOUNCE_SAMPLES);
		else
			sw2_count -= (sw2_count != 0);
		
		if(gpio_read_pin(GPIO_PORT_1, GPIO_PIN_7))
			sw3_count += (sw3_count < DEBOUNCE_SAMPLES);
		else
			sw3_count -= (sw3_count != 0);
		
		sw1_press_event = (sw1_state && !sw1_count);
		sw2_press_event = (sw2_state && !sw2_count);
		sw3_press_event = (sw3_state && !sw3_count);
		
		sw1_release_event = (!sw1_state && (sw1_count == DEBOUNCE_SAMPLES));
		sw2_release_event = (!sw2_state && (sw2_count == DEBOUNCE_SAMPLES));
		sw3_release_event = (!sw3_state && (sw3_count == DEBOUNCE_SAMPLES));
		
		sw1_state = (sw1_state && sw1_count) || (sw1_count == DEBOUNCE_SAMPLES);
		sw2_state = (sw2_state && sw2_count) || (sw2_count == DEBOUNCE_SAMPLES);
		sw3_state = (sw3_state && sw3_count) || (sw3_count == DEBOUNCE_SAMPLES);
			
		//Update encoder position and events
		knob_pos_raw = as5600_read_word(AS_REG_RAW_ANGLE) << 4;
		knob_pos_change = knob_pos_raw - knob_pos_coarse;
		if((knob_pos_change < -(signed short)(COARSE_HYSTERESIS << 4)) || (knob_pos_change > (signed short)(COARSE_HYSTERESIS << 4)))
		{
			knob_inc_event = (knob_pos_change > 0);
			knob_dec_event = (knob_pos_change < 0);
			knob_pos_coarse = knob_pos_raw;
		}
		else
		{
			knob_inc_event = 0;
			knob_dec_event = 0;
		}
		
		knob_pos_change = (knob_pos_raw - knob_pos_fine) >> 4;	//HINT: Behavior of right shift on a signed operand is compiler specific... The intent is dividing by 16.
		if((knob_pos_change < -(signed short)FINE_HYSTERESIS) || (knob_pos_change > (signed short)FINE_HYSTERESIS))
			knob_pos_fine = knob_pos_raw;
		else
			knob_pos_change = 0;
		
#if ENABLE_DEBUG		
		//debug prints
		if(knob_inc_event | knob_dec_event)
		{
			//print knob_pos_raw
			uart_write_string(UART_0, pos_raw_str);
			byte_to_hex((UINT8)(knob_pos_raw >> 8), hex_str);
			byte_to_hex((UINT8)(knob_pos_raw), hex_str + 2);
			hex_str[4] = '\n';
			hex_str[5] = '\0';
			uart_write_string(UART_0, hex_str);
			
			//print knob_pos_coarse
			uart_write_string(UART_0, pos_coarse_str);
			byte_to_hex((UINT8)(knob_pos_coarse >> 8), hex_str);
			byte_to_hex((UINT8)(knob_pos_coarse), hex_str + 2);
			hex_str[4] = '\n';
			hex_str[5] = '\0';
			uart_write_string(UART_0, hex_str);
			
			//print knob_pos_fine
			uart_write_string(UART_0, pos_fine_str);
			byte_to_hex((UINT8)(knob_pos_fine >> 8), hex_str);
			byte_to_hex((UINT8)(knob_pos_fine), hex_str + 2);
			hex_str[4] = '\n';
			hex_str[5] = '\0';
			uart_write_string(UART_0, hex_str);
			
			//print knob_pos_change
			uart_write_string(UART_0, pos_change_str);
			byte_to_hex((UINT8)(knob_pos_change >> 8), hex_str);
			byte_to_hex((UINT8)(knob_pos_change), hex_str + 2);
			hex_str[4] = '\n';
			hex_str[5] = '\0';
			uart_write_string(UART_0, hex_str);
		}
#endif
		
		//Update knob_mode and LEDs
		if(sw2_state)	//change the mode only while sw2 is not pressed
		{
			if(sw1_press_event)
				knob_mode -= 1;
			if(sw3_press_event)
				knob_mode += 1;
			
			if(knob_mode & 0x80)	//if knob_mode is "negative"
				knob_mode = KNOB_MODE_VOLUME;
			else if(knob_mode > KNOB_MODE_VOLUME)
				knob_mode = KNOB_MODE_SCROLL;
			
			if(knob_mode != knob_mode_prev)
			{
				gpio_set_pin(GPIO_PORT_3, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
				gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_5 >> knob_mode);
				knob_mode_prev = knob_mode;
			}
		}
		
		//Handle knob and button events
		switch(knob_mode)
		{
			case KNOB_MODE_SCROLL:
				if(sw2_state)
				{
					if(knob_pos_change)
					{
						pos_accum += knob_pos_change * scroll_speed;
						knob_pos_change = (pos_accum_prev >> FINE_SHIFT) - (pos_accum >> FINE_SHIFT);
						pos_accum_prev = pos_accum;
						hid_mouse_scroll((signed char)knob_pos_change, 0);
					}
				}
				else
				{
#if ENABLE_WIGGLER
					if(sw3_press_event)
					{
						wiggler_active = !wiggler_active;
						stop_time = 0;	//start moving immediately when activated
					}
					if(sw1_press_event)
						hid_mouse_press(HID_MOUSE_BTN_WHEEL);
#else
					if(sw1_press_event | sw3_press_event)
						hid_mouse_press(HID_MOUSE_BTN_WHEEL);
#endif
					if(knob_inc_event)
						++scroll_speed;
					if(knob_dec_event)
						--scroll_speed;
					if(scroll_speed < 1)
						scroll_speed = 1;
					if(scroll_speed > (UINT8)SPEED_STEPS)
						scroll_speed = (UINT8)SPEED_STEPS;
				}
				if(sw1_release_event | sw3_release_event)	//Make sure to release the mouse button even if sw2 is released first.
					hid_mouse_release(HID_MOUSE_BTN_WHEEL);
				break;
			case KNOB_MODE_PAN:
				if(sw2_state)
				{
					if(knob_pos_change)
					{
						pos_accum += knob_pos_change * pan_speed;
						knob_pos_change = (pos_accum_prev >> FINE_SHIFT) - (pos_accum >> FINE_SHIFT);
						pos_accum_prev = pos_accum;
						hid_mouse_scroll(0, (signed char)knob_pos_change);
					}
				}
				else
				{
#if ENABLE_WIGGLER
					if(sw3_press_event)
					{
						wiggler_active = !wiggler_active;
						stop_time = 0;	//start moving immediately when activated
					}
					if(sw1_press_event)
						hid_mouse_press(HID_MOUSE_BTN_WHEEL);
#else
					if(sw1_press_event | sw3_press_event)
						hid_mouse_press(HID_MOUSE_BTN_WHEEL);
#endif
					if(knob_inc_event)
						++pan_speed;
					if(knob_dec_event)
						--pan_speed;
					if(pan_speed < 1)
						pan_speed = 1;
					if(pan_speed > (UINT8)SPEED_STEPS)
						pan_speed = (UINT8)SPEED_STEPS;
				}
				if(sw1_release_event | sw3_release_event)	//Make sure to release the mouse button even if sw2 is released first.
					hid_mouse_release(HID_MOUSE_BTN_WHEEL);
				break;
			case KNOB_MODE_VOLUME:
				if(sw2_state)
				{
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
				}
				else
				{
					if(knob_inc_event)
					{
						hid_cc_press(HID_CC_BTN_MEDIA_NEXT);
						while(hid_cc_report_pending);
						hid_cc_press(HID_CC_BTN_NONE);
					}
					if(knob_dec_event)
					{
						hid_cc_press(HID_CC_BTN_MEDIA_PREV);
						while(hid_cc_report_pending);
						hid_cc_press(HID_CC_BTN_NONE);
					}
					
					if(sw1_press_event)
						hid_cc_press(HID_CC_BTN_VOL_MUTE);
					if(sw3_press_event)
						hid_cc_press(HID_CC_BTN_PLAY_PAUSE);
				}
				if(sw1_release_event | sw3_release_event)
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
#if ENABLE_DEBUG
		if(uart_bytes_available(UART_0) >= 2)
		{
			temp = uart_peek(UART_0);
			if((temp == '\r') || (temp == '\n'))
			{
				(void)uart_read_byte(UART_0);
				continue;
			}
			
			uart_read_bytes(UART_0, datagram, 2);
			for(count = 0; count < 2; ++count)
			{
				temp = datagram[count];
				if(temp >= '0' && temp <= '9')  //convert numbers
					temp = temp - '0';
				else if(temp >= 'A' && temp <= 'F')   //convert uppercase letters
					temp = temp - 'A' + 10;
				else if(temp >= 'a' && temp <= 'f')   //convert the annoying lowercase letters
					temp = temp - 'a' + 10;
				else
					continue;

				RESET_KEEP = RESET_KEEP << 4;
				RESET_KEEP = RESET_KEEP | temp;
			}
			
			switch(RESET_KEEP)
			{
				case 0x00:
					as_read = as5600_read_word(AS_REG_CONF);
					byte_to_hex((UINT8)(as_read >> 8), hex_str);
					byte_to_hex((UINT8)as_read, hex_str + 2);
					hex_str[4] = '\n';
					hex_str[5] = '\0';
					uart_write_string(UART_0, hex_str);
					break;
				case 0x01:
					as_read = as5600_read_word(AS_REG_RAW_ANGLE);
					byte_to_hex((UINT8)(as_read >> 8), hex_str);
					byte_to_hex((UINT8)as_read, hex_str + 2);
					hex_str[4] = '\n';
					hex_str[5] = '\0';
					uart_write_string(UART_0, hex_str);
					break;
				case 0x02:
					as_read = as5600_read_word(AS_REG_ANGLE);
					byte_to_hex((UINT8)(as_read >> 8), hex_str);
					byte_to_hex((UINT8)as_read, hex_str + 2);
					hex_str[4] = '\n';
					hex_str[5] = '\0';
					uart_write_string(UART_0, hex_str);
					break;
				case 0x03:
					temp = as5600_read_byte(AS_REG_STATUS);
					byte_to_hex(temp, hex_str);
					hex_str[2] = '\n';
					hex_str[3] = '\0';
					uart_write_string(UART_0, hex_str);
					break;
				case 0x04:
					temp = as5600_read_byte(AS_REG_AGC);
					byte_to_hex(temp, hex_str);
					hex_str[2] = '\n';
					hex_str[3] = '\0';
					uart_write_string(UART_0, hex_str);
					break;
				case 0x05:
					as_read = as5600_read_word(AS_REG_MAGNITUDE);
					byte_to_hex((UINT8)(as_read >> 8), hex_str);
					byte_to_hex((UINT8)as_read, hex_str + 2);
					hex_str[4] = '\n';
					hex_str[5] = '\0';
					uart_write_string(UART_0, hex_str);
					break;
				case 0x06:
					reporting_enabled = 1;
					break;
				case 0x07:
					reporting_enabled = 0;
					break;
				default:
					uart_write_string(UART_0, str_bad_command);
					break;
			}
		}
		
		if(reporting_enabled && (timer_overflow_counts[TIMER_2] >= 200))
		{
			timer_overflow_counts[TIMER_2] = 0;
			as_read = as5600_read_word(AS_REG_RAW_ANGLE);
			byte_to_hex((UINT8)(as_read >> 8), hex_str);
			byte_to_hex((UINT8)as_read, hex_str + 2);
			hex_str[4] = '\n';
			hex_str[5] = '\0';
			uart_write_string(UART_0, hex_str);
		}
#endif
		rcc_reload_wdog(0x00);
	}
}

