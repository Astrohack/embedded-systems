/*****************************************************************************
 *   Project: Reflex
 *   Description: This program simulates a game to measure the user's 
 *                reaction time based on a visual trigger.
 *
 *   Authors:
 *
 ******************************************************************************/


#include "mcu_regs.h"
#include "type.h"
#include "stdio.h"
#include "timer32.h"
#include "gpio.h"
#include "i2c.h"
#include "ssp.h"

#include "eeprom.h"
#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "light.h"
#include "oled.h"
#include "rgb.h"

#include <stdlib.h>
#include <string.h>

/* Macros for controlling speaker pin */
#define P1_2_HIGH() (LPC_GPIO1->DATA |= ((uint16_t)0x1<<2))
#define P1_2_LOW()  (LPC_GPIO1->DATA &= ~((uint16_t)0x1<<2))

/* I/O direction macros */
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT 0

/* ----- Speaker -----> */
#define MENU_ITEM_COUNT 4

typedef enum {
    MENU_START_GAME = 0,
    MENU_RESET_SCORE,
    MENU_CREDITS,
    MENU_EXIT
} MenuItem;

static const char *menuItems[MENU_ITEM_COUNT] = {
    "Start game",
    "Reset score",
    "Credits",
    "Exit"
};

static int selectedIndex = 0;
/* <---- Menu ------ */

typedef struct {
    int8_t xOffset;
    int8_t yOffset;
    int8_t zOffset;
    int8_t x;
    int8_t y;
    int8_t z;
} TiltState;
static TiltState tilt;

/* Frequencies for musical notes in timer units */
static uint32_t notes[] = {
    2272, // A - 440 Hz
    2024, // B - 494 Hz
    3816, // C - 262 Hz
    3401, // D - 294 Hz
    3215, // D# - 311 Hz
    3030, // E - 330 Hz
    2865, // F - 349 Hz
    2703, // F# - 370 Hz
    2551, // G - 392 Hz
    2146, // A# - 466 Hz
    1136, // a - 880 Hz
    1012, // b - 988 Hz
    1912, // c - 523 Hz
    1703, // d - 587 Hz
    1517, // e - 659 Hz
    1432, // f - 698 Hz
    1275, // g - 784 Hz
};

/*****************************************************************************
** Function name:       drawMenu
**
** Description:         Renders the menu on OLED with selection highlight
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
static void draw_menu(void) {
    oled_clearScreen(backgroundColor);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        uint8_t y = 2 + i * 12;

        // Add arrow to selected item
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%c %s", (i == selectedIndex ? '>' : ' '), menuItems[i]);

        oled_putString(4, y, (const uint8_t *)buffer, fontColor, backgroundColor);
    }
}

/*****************************************************************************
** Function name:       handleMenu
**
** Description:         Runs the menu system and handles joystick input
**
** Parameters:          None
** Returned value:      Selected item index (0-based)
*****************************************************************************/
static int handle_menu(void) {
    uint8_t previous_joy = 0xFF;
    while (1) {
        uint8_t joy = joystick_read();

        if ((joy & JOYSTICK_DOWN) && !(previous_joy & JOYSTICK_DOWN)) {
            selectedIndex = (selectedIndex + 1) % MENU_ITEM_COUNT;
            draw_menu();
        } else if ((joy & JOYSTICK_UP) && !(previous_joy & JOYSTICK_UP)) {
            selectedIndex = (selectedIndex - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
            draw_menu();
        } else if ((joy & JOYSTICK_CENTER) && !(previous_joy & JOYSTICK_CENTER)) {
            return selectedIndex;
        }

        previous_joy = joy;

        if (is_board_tilted()) {
            resetHighScore(9999);
            oled_putStringHorizontallyCentered((OLED_DISPLAY_HEIGHT / 2) + 16, "Reset HS");
            delay32Ms(0, 500);
        }
        adjust_theme();
        delay32Ms(0, 100);
    }
}

static void show_main_menu(void) {
    while(1) {
        draw_menu();
        MenuItem selection = handle_menu();

        switch (selection) {
            case MENU_START_GAME:
                play_note(notes[0], 200);
                start_game();
                break;

            case MENU_RESET_SCORE:
                resetHighScore(9999);
                oled_putStringHorizontallyCentered((OLED_DISPLAY_HEIGHT / 2) + 16, "Reset HS");
                play_note(notes[1], 200);
                break;

            case MENU_CREDITS:
                oled_clearScreen(backgroundColor);
                oled_putStringHorizontallyCentered(20, "by Patryk Krawczyk");
                play_star_wars_theme();
                break;

            case MENU_EXIT:
                oled_clearScreen(backgroundColor);
                oled_putStringHorizontallyCentered(20, "Exiting...");
                play_note(notes[10], 200);
                play_note(notes[5], 200);
                play_note(notes[1], 200);
                delay32Ms(0, 400);
                return;
        }
    }
}

/*****************************************************************************
** Function name:       play_star_wars_theme
**
** Descriptions:        Plays a star wars theme melody consisting of predefined notes
**
** parameters:          None
** Returned value:      None
*****************************************************************************/
void play_star_wars_theme(void) {
    play_note(notes[9], 200); 
    play_note(notes[9], 200); 
    play_note(notes[4], 150);
    play_note(notes[9], 200); 
    play_note(notes[11], 200);
    play_note(notes[9], 200); 
    play_note(notes[4], 150);
    play_note(notes[11], 200);
    play_note(notes[9], 200); 

    delay32Ms(0, 200);

    play_note(notes[4], 150);
    play_note(notes[4], 150);
    play_note(notes[3], 200); 
    play_note(notes[9], 200); 
    play_note(notes[4], 150);
    play_note(notes[11], 200);
    play_note(notes[9], 250); 

    delay32Ms(0, 200);

    play_note(notes[17], 150);
    play_note(notes[17], 150);
    play_note(notes[17], 150);
    play_note(notes[4], 150);
    play_note(notes[11], 200);
    play_note(notes[9], 300); 
}


static void init_tilt_calibration(void) {
    acc_read(&tilt.x, &tilt.y, &tilt.z);
    tilt.xOffset = -tilt.x;
    tilt.yOffset = -tilt.y;
    tilt.zOffset = 64 - tilt.z;
}

static bool is_board_tilted(void) {
    acc_read(&tilt.x, &tilt.y, &tilt.z);

    tilt.x += tilt.xOffset;
    tilt.y += tilt.yOffset;
    tilt.z += tilt.zOffset;

    if ((tilt.x > 30) || (tilt.x < -30) || (tilt.y > 30) || (tilt.y < -30)) {
        return true;
    }

    return false;
}

/*****************************************************************************
** Function name:       wait_for_joystick_center_click
**
** Description:         Blocks until the joystick center button is pressed.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
static void wait_for_joystick_center_click(void) {
    while ((joystick_read() & JOYSTICK_CENTER) == 0) {
        delay32Ms(0, 100);
    }
}

void draw_circle(uint8_t x0, uint8_t y0, uint8_t radius, oled_color_t color) {
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        oled_putPixel(x0 + x, y0 + y, color);
        oled_putPixel(x0 + y, y0 + x, color);
        oled_putPixel(x0 - y, y0 + x, color);
        oled_putPixel(x0 - x, y0 + y, color);
        oled_putPixel(x0 - x, y0 - y, color);
        oled_putPixel(x0 - y, y0 - x, color);
        oled_putPixel(x0 + y, y0 - x, color);
        oled_putPixel(x0 + x, y0 - y, color);

        y += 1;
        if (err <= 0) {
            err += 2 * y + 1;
        } 
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void fill_circle(uint8_t x0, uint8_t y0, uint8_t radius, oled_color_t color) {
    for (int16_t y = -radius; y <= radius; y++) {
        for (int16_t x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                oled_putPixel(x0 + x, y0 + y, color);
            }
        }
    }
}

/*****************************************************************************
** Function name:       play_note
**
** Description:         Generates a square wave of the given frequency
**                      for a specified duration (in ms) on speaker pin.
**
** Parameters:          note - frequency period in µs
**                      durationMs - duration to play the note
** Returned value:      None
*****************************************************************************/
static void play_note(uint32_t note, uint32_t durationMs) {
	uint32_t t = 0;
    if (note > (uint32_t)0) {
        while (t < (durationMs * (uint32_t)1000)) {
            P1_2_HIGH();
            delay32Us(0, note / (uint32_t)4);
            P1_2_LOW();
            delay32Us(0, note / (uint32_t)4);
            t += note;
        }
    }
    else {
        delay32Ms(0, durationMs);
    }
}

/* OLED color variables for foreground/background */
static oled_color_t fontColor;
static oled_color_t backgroundColor;

/*****************************************************************************
** Function name:       adjust_theme
**
** Description:         Reads ambient light sensor and adjusts display
**                      theme (dark/light mode) accordingly.
**
** Parameters:          None
** Returned value:      1 if color theme changed, 0 otherwise
*****************************************************************************/
static void adjust_theme(void) {
    uint32_t reading = light_read();
    uint32_t prev_fontColor = fontColor;
    if (reading < 125) {
        fontColor = OLED_COLOR_WHITE;
        backgroundColor = OLED_COLOR_BLACK;
    } else {
        fontColor = OLED_COLOR_BLACK;
        backgroundColor = OLED_COLOR_WHITE;
    }
    if (fontColor != prev_fontColor) {
        play_note(notes[0],200);
    } else {
        play_note(notes[4],200);
    }
}

/*****************************************************************************
** Function name:		moveBar
**
** Descriptions:		Moves the LED bar by one position
**
** parameters:			None
** Returned value:		None
**
*****************************************************************************/
static void set_led_bar_position(uint8_t pos)
{
    uint16_t ledOn;
    ledOn = (uint16_t)0x01 << pos;
    pca9532_setLeds(ledOn, 0xffff);
}


/*****************************************************************************
** Function name:       set_high_score
**
** Description:         Stores new high score value into EEPROM.
**
** Parameters:          value - new high score (ms)
** Returned value:      None
*****************************************************************************/
void set_high_score(uint16_t value) {
	uint8_t buf[2];
	buf[0]= (value & (uint16_t)0xFF00) >> 8;
	buf[1]= (value & (uint16_t)0x00FF);
	eeprom_write(buf,0,2);
}

uint16_t read_high_score() {
	uint8_t readBuf[2];
    eeprom_read(readBuf, 0, 2);
	return ((uint16_t)readBuf[0] << 8) | (uint16_t)readBuf[1];
}

/*****************************************************************************
** Function name:		measure_reaction_time
**
** Descriptions:		Measures the reaction time of the user in milliseconds
**
** parameters:			None
** Returned value:		None
**
*****************************************************************************/
static int measure_reaction_time(void) {
    init_timer32(1, 72000);
    LPC_TMR32B1->TCR = 0x02;
    uint32_t prescalerValue = ((SystemFrequency/LPC_SYSCON->SYSAHBCLKDIV) / 1000) - 1;
    LPC_TMR32B1->PR = prescalerValue;
    LPC_TMR32B1->MCR = 0x00;
    LPC_TMR32B1->TCR = 0x01;

    wait_for_joystick_center_click();

    uint32_t reactionTimeMs = LPC_TMR32B1->TC;
    char reactionTimeMsString[5];
    int n = sprintf(reactionTimeMsString, "%u", reactionTimeMs);
    return n
}

/*****************************************************************************
** Function name:		oled_putStringHorizontallyCentered
**
** Descriptions:		Puts a horizontally centered string at a given vertical coordinate of the OLED display
**
** parameters:			y - vertical coordinate, text - the text to display
** Returned value:		None
*****************************************************************************/
static void oled_putStringHorizontallyCentered(uint8_t y, const char text[]) {
    int textLength = strlen(text);
    oled_putString((OLED_DISPLAY_WIDTH - (textLength * 5)) / 2, y, (const uint8_t *)text, fontColor, backgroundColor);
}

/* Displays the initial splash screen with high score */
static void show_welcome_screen(void) {
    oled_clearScreen(backgroundColor);
    oled_putStringHorizontallyCentered(2, "Witaj w grze");
    oled_putStringHorizontallyCentered(12, "REFLEKS");
    oled_putStringHorizontallyCentered(32, "High score:");

    // get current high score from EEPROM
    uint8_t readBuf[2];
    eeprom_read(readBuf, 0, 2);
    uint16_t highScoreMs = ((uint16_t)readBuf[0] << 8) | (uint16_t)readBuf[1];

    // print on screen
    char highScoreMsString[8];
    sprintf(highScoreMsString, "%u ms", highScoreMs);
    oled_putStringHorizontallyCentered(42, highScoreMsString);
    wait_for_joystick_center_click();
}


/*****************************************************************************
** Function name:       play_startup_animation
**
** Description:         Shows a short "boot up" animation on OLED with
**                      themed quotes and loading dots.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
static void play_startup_animation(void) {
    play_note(notes[3], 200);
	const char *frames[] = {".", "..", "..."};
	const char *quotes[] = {"Heat up...", "Thrust OK", "Fire laser", "Weapons armed", "AI synced", "Engines online", "Core stable", "Warp ready", "Scanning...",};
	for (int i = 0; i < 3; i++) {
		const char *str = quotes[i];
		for (int f = 0; f < 3; f++) {
			oled_clearScreen(backgroundColor);
			oled_putStringHorizontallyCentered(24, str);
			oled_putStringHorizontallyCentered(36, frames[f]);
			delay32Ms(0, 300);
		}
	}
    init_tilt_calibration();
}


static void start_game(void) {
    uint8_t round = 0;
    while (round < 5) {
        set_led_bar_position(round);
        adjust_theme();
        oled_clearScreen(backgroundColor);
        draw_circle(OLED_DISPLAY_HEIGHT / 2, OLED_DISPLAY_WIDTH / 2, 24, fontColor);
        oled_putStringCentered("Czekaj...");
        play_note(notes[2], 250);

        uint32_t randomDelay = (rand() % 3000) + 500;
        delay32Ms(1, randomDelay);
        fill_circle(OLED_DISPLAY_HEIGHT / 2, OLED_DISPLAY_WIDTH / 2, 24, fontColor);

        measure_reaction_time();
        oled_clearScreen(backgroundColor);
        char * str = strcat(reactionTimeMsString, " ms");
        oled_putStringHorizontallyCentered(OLED_DISPLAY_HEIGHT / 2, reactionTimeMsString);

        // Update high score
        uint16_t highScoreMs = getHighScore();
        if ((reactionTimeMs < highScoreMs) || (highScoreMs == (uint16_t)0)) {
            set_high_score(reactionTimeMs);
        }
        round++;
        wait_for_joystick_center_click();
    }
}

/*****************************************************************************
** Function name:       main
**
** Description:         Entry point of the program. Initializes peripherals,
**                      displays intro screen, and enters idle loop that
**                      monitors for color mode change.
*****************************************************************************/
int main(void) {
    // 1) Inicjalizacja GPIO (konieczna do działania wielu peryferiów)
    GPIOInit();

    init_timer32(0, 10);
    init_timer32(1, 10);

    I2CInit((uint32_t)I2CMASTER, 0);

    SSPInit();

    // 4) Inicjalizacja modułu wyświetlacza i czujnika światła
    oled_init();

    // Init and enable light sensor ISL29003 
    light_init();
    light_enable();

    // 5) Inicjalizacja EEPROM
    eeprom_init();

    /* ---- Speaker ------> */

    // Init PWM Low Pass Filter 
    GPIOSetDir( PORT1, 2, OUTPUT );
    LPC_IOCON->JTAG_nTRST_PIO1_2 = (LPC_IOCON->JTAG_nTRST_PIO1_2 & ~0x7) | 0x01; // set this pin in PWM mode

    // Init LM4811 Analog Amplifier
    GPIOSetDir( PORT3, 0, OUTPUT );
    GPIOSetDir( PORT3, 1, OUTPUT );
    GPIOSetDir( PORT3, 2, OUTPUT );

    GPIOSetValue( PORT3, 0, LOW );  //LM4811-clk
    GPIOSetValue( PORT3, 1, LOW );  //LM4811-up/dn
    GPIOSetValue( PORT3, 2, LOW );  //LM4811-shutdn

    /* <---- Speaker ------ */

    // 6) Ustawienie kolorów (day/night mode)
    adjust_theme();

    // 7) Clear screen and show welcome screen with high score
    play_startup_animation();
    show_welcome_screen();

    // 8) Set random generator seed to reading from light sensor
    srand(light_read());

    show_main_menu();
}
