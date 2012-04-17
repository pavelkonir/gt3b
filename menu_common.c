/*
    menu_mix - handle menus for mix settings
    Copyright (C) 2011 Pavel Semerad

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include <string.h>
#include "menu.h"
#include "config.h"
#include "ppm.h"
#include "input.h"
#include "lcd.h"
#include "buzzer.h"





// variables to be used in CALC task
s8  menu_channel3_8[MAX_CHANNELS - 2];	// values -100..100 for channels >=3
u8  menu_channels_mixed;	// channel with 1 here will not be set from
				//   menu_channel3_8
s8  menu_4WS_mix;		// mix -100..100
_Bool menu_4WS_crab;		// when 1, crab steering
s8  menu_DIG_mix;		// mix -100..100
u8  menu_MP_index;		// index of MultiPosition channel
_Bool menu_brake;		// when 1, full brake is applied



// battery low flag
_Bool menu_battery_low;
// raw battery ADC value for check to battery low
u16 battery_low_raw;




// apply model settings to variables
void apply_model_config(void) {
    u8 i, autorepeat = 0;

    // set number of channels for this model
    ppm_set_channels((u8)(cm.channels + 1));

    // set mixed channels to ignore them from menu_channel3_8
    menu_channels_mixed = 0;
    if (cm.channel_4WS)
	menu_channels_mixed |= (u8)(1 << (u8)(cm.channel_4WS - 1));
    if (cm.channel_DIG)
	menu_channels_mixed |= (u8)(1 << (u8)(cm.channel_DIG - 1));

    // set autorepeat
    for (i = 0; i < 4; i++) {
	if (!ck.et_map[i].is_trim)  continue;  // trim is off, skip
	if (ck.et_map[i].buttons == ETB_AUTORPT)
	    autorepeat |= (u8)((u8)et_buttons[i][0] | (u8)et_buttons[i][1]);
    }
    button_autorepeat(autorepeat);
}


// load model config from eeprom and set model settings
void menu_load_model(void) {
    u8 i;
    // load config
    config_model_read();

    // set values of channels >= 3 to default left state,
    //   for channels mapped to some trims/keys, it will next be set
    //   to corresponding centre/reset value
    for (i = 0; i < MAX_CHANNELS - 2; i++)
	menu_channel3_8[i] = -100;

    // set 4WS, DIG, MP to defaults
    menu_4WS_mix = 0;
    menu_4WS_crab = 0;
    menu_DIG_mix = 0;
    menu_MP_index = 0;
    if (cm.channel_MP)
	menu_channel3_8[cm.channel_MP - 3] = cm.multi_position[0];
    menu_brake = 0;

    // set state of buttons to do initialize
    menu_buttons_initialize();

    // apply config to radio setting
    apply_model_config();
}


// apply global setting to variables
void apply_global_config(void) {
    backlight_set_default(cg.backlight_time);
    backlight_on();
    // compute raw value for battery low voltage
    battery_low_raw = (u16)(((u32)cg.battery_calib * cg.battery_low + 50) / 100);
}


// menu stop - checks low battery
_Bool battery_low_shutup;
void menu_stop(void) {
    static _Bool battery_low_on;
    stop();
    // low_bat is disabled in calibrate, key-test and global menus,
    //   check it by buzzer_running
    if (menu_battery_low && !buzzer_running && !battery_low_shutup)
	battery_low_on = 0;
    if (battery_low_on == menu_battery_low)  return;  // no change

    // battery low status changed
    if (menu_battery_low) {
	// battery low firstly
	battery_low_on = 1;
	lcd_segment(LS_SYM_LOWPWR, LS_ON);
	lcd_segment_blink(LS_SYM_LOWPWR, LB_SPC);
	buzzer_on(40, 160, BUZZER_MAX);
    }
    else {
	// battery low now OK
	battery_low_on = 0;
	lcd_segment(LS_SYM_LOWPWR, LS_OFF);
	buzzer_off();
    }
    lcd_update();
}


// change value based on state of rotate encoder
s16 menu_change_val(s16 val, s16 min, s16 max, u8 amount_fast, u8 rotate) {
    u8 amount = 1;

    if (btn(BTN_ROT_L)) {
	// left
	if (btnl(BTN_ROT_L))  amount = amount_fast;
	val -= amount;
	if (val < min)
	    if (rotate)	 val = max;
	    else         val = min;
    }
    else {
	// right
	if (btnl(BTN_ROT_R))  amount = amount_fast;
	val += amount;
	if (val > max)
	    if (rotate)  val = min;
	    else         val = max;
    }
    return val;
}


// clear all symbols
void menu_clear_symbols(void) {
    lcd_segment(LS_SYM_MODELNO, LS_OFF);
    lcd_segment(LS_SYM_DOT, LS_OFF);
    lcd_segment(LS_SYM_VOLTS, LS_OFF);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
    lcd_segment(LS_SYM_LEFT, LS_OFF);
    lcd_segment(LS_SYM_RIGHT, LS_OFF);
    lcd_segment(LS_SYM_CHANNEL, LS_OFF);
}


// common menu, select item at 7SEG and then set params at CHR3
u8 menu_set;		// menu is in: 0 = menu_id, 1..X = menu setting 1..X
u8 menu_id;		// id of selected menu
_Bool menu_id_set;	// 0 = in menu-id, 1 = in menu-setting
u8 menu_blink;		// what of chars should blink

void menu_common(menu_common_t func, void *params, u8 flags) {
    menu_id_set = 0;		// start at menu-id
    menu_set = 1;		// now in menu_id
    menu_id = 0;		// first menu item
    menu_blink = 0xff;		// bit for each char to blink

    // clear display symbols
    menu_clear_symbols();
    if (flags & MCF_LOWPWR)  lcd_segment(LS_SYM_LOWPWR, LS_OFF);

    // init and show setting
    func(MCA_INIT, params);
    if (menu_id_set) {
	lcd_chars_blink_mask(LB_SPC, menu_blink);
    }
    else {
	if (menu_blink & MCB_7SEG)  lcd_set_blink(L7SEG, LB_SPC);
    }
    lcd_update();

    while (1) {

	// remove button flags and wait for wakeup
	btnra();
	if (flags & MCF_STOP)	stop();
	else			menu_stop();

	// end this menu with defined buttons
	if (btn(BTN_BACK | BTN_END) || btnl(BTN_ENTER))  break;

	// if menu ADC was activated, call func to read for example left-right pos
	if (menu_adc_wakeup)  func(MCA_ADC_PRE, params);

	// rotate encoder changed, change menu-id or value
	if (btn(BTN_ROT_ALL)) {
	    if (menu_id_set) {
		// change selected menu setting
		func(MCA_SET_CHG, params);
		lcd_chars_blink_mask(LB_SPC, menu_blink);
	    }
	    else {
		// change menu-id

		// reset some variables
		menu_adc_wakeup = 0;
		menu_force_value_channel = 0;
		menu_blink = 0xff;		// default to all chars

		// remove possible showed symbols
		menu_clear_symbols();
		if (flags & MCF_LOWPWR)  lcd_segment(LS_SYM_LOWPWR, LS_OFF);

		// select new menu id and show it
		if (flags & MCF_ID_CHG)
		    func(MCA_ID_CHG, params);	// do own change based on BTN_ROT
		else if (btn(BTN_ROT_L))
		    func(MCA_ID_PREV, params);	// previous menu id
		else
		    func(MCA_ID_NEXT, params);	// next menu id

		if (menu_blink & MCB_7SEG)  lcd_set_blink(L7SEG, LB_SPC);
	    }
	    lcd_update();
	}

	// ENTER pressed, switch between menu settings
	else if (btn(BTN_ENTER)) {
	    // switch menu_id/menu-setting1/menu-setting2/...
	    key_beep();
	    if (menu_id_set) {
		// select next menu setting
		func(MCA_SET_NEXT, params);
		if (menu_set != 1) {
		    // some > 1 menu setting
		    lcd_chars_blink_mask(LB_SPC, menu_blink);
		}
		else {
		    // rotated back to setting 1, switch to menu selection
		    menu_id_set = 0;
		    if (menu_blink & MCB_7SEG)  lcd_set_blink(L7SEG, LB_SPC);
		    lcd_chars_blink(LB_OFF);
		}
		lcd_update();
	    }
	    else {
		// switch to first menu setting
		menu_id_set = 1;
		// menu setting values is already showed
		lcd_set_blink(L7SEG, LB_OFF);
		lcd_chars_blink_mask(LB_SPC, menu_blink);
	    }
	}

	// if menu ADC was activated, call func to for example show other
	//   value when left-right position changed
	if (menu_adc_wakeup)  func(MCA_ADC_POST, params);
    }

    // call to select next value which can do some action (such as reset)
    if (menu_id_set)  func(MCA_SET_NEXT, params);

    // cleanup display
    menu_clear_symbols();
    if (flags & MCF_LOWPWR)  lcd_segment(LS_SYM_LOWPWR, LS_OFF);

    // reset variables
    menu_adc_wakeup = 0;
    menu_force_value_channel = 0;
    key_beep();
}


// common list menu, select item at 7SEG and then set params at CHR3
void menu_list(menu_list_t *menu_funcs, u8 menu_nitems, u8 use_stop) {
    u8 id_val = 0;			// now in key_id
    u8 menu_id = 0;
    menu_list_t func = menu_funcs[0];
    u8 chars_blink = 0b111;		// bit for each char to blink

    menu_clear_symbols();
    if (use_stop)  lcd_segment(LS_SYM_LOWPWR, LS_OFF);

    // show first setting for first menu id
    func(1, 0, &chars_blink);
    lcd_set_blink(L7SEG, LB_SPC);
    lcd_update();

    while (1) {
	btnra();
	if (use_stop)  stop();
	else           menu_stop();

	if (btn(BTN_BACK | BTN_END) || btnl(BTN_ENTER))  break;

	if (btn(BTN_ROT_ALL)) {
	    if (id_val) {
		// change selected setting
		func(id_val, 1, &chars_blink);
		lcd_chars_blink_mask(LB_SPC, chars_blink);
		lcd_update();
	    }
	    else {
		// change menu-id
		menu_force_value_channel = 0;
		if (btn(BTN_ROT_L)) {
		    if (menu_id)  menu_id--;
		    else	  menu_id = (u8)(menu_nitems - 1);
		}
		else {
		    if (++menu_id >= menu_nitems)  menu_id = 0;
		}
		func = menu_funcs[menu_id];
		// remove possible showed symbols
		menu_clear_symbols();
		if (use_stop)  lcd_segment(LS_SYM_LOWPWR, LS_OFF);
		chars_blink = 0b111;		// default to all chars
		func(1, 0, &chars_blink);	// show first setting
		lcd_set_blink(L7SEG, LB_SPC);
		lcd_update();
	    }
	}

	else if (btn(BTN_ENTER)) {
	    // switch menu_id/menu-setting1/menu-setting2/...
	    key_beep();
	    if (id_val) {
		// what to do depends on what was selected in this item
		id_val = func(id_val, 2, &chars_blink);
		if (id_val != 1) {
		    lcd_chars_blink_mask(LB_SPC, chars_blink);
		}
		else {
		    // switch to menu selection
		    id_val = 0;
		    lcd_set_blink(L7SEG, LB_SPC);
		    lcd_chars_blink(LB_OFF);
		}
		lcd_update();
	    }
	    else {
		// switch to key settings
		id_val = 1;
		// key setting values is already showed
		lcd_set_blink(L7SEG, LB_OFF);
		lcd_chars_blink_mask(LB_SPC, chars_blink);
	    }
	}
    }

    // call to select next value which can do some action (such as reset)
    if (id_val)  id_val = func(id_val, 2, &chars_blink);
    // cleanup display
    menu_clear_symbols();
    if (use_stop)  lcd_segment(LS_SYM_LOWPWR, LS_OFF);
    menu_force_value_channel = 0;
    key_beep();
}

