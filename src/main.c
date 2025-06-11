/*****************************************************************************
 *   Project: Reflex
 *   Description: This program simulates a game to measure the user's
 *                reaction time based on a visual trigger.
 *
 *   Authors: Patryk Krawczyk
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
#include "led7seg.h"

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

/* Menu system constants */
#define MENU_ITEM_COUNT 5

/*****************************************************************************
 * Enumeration: MenuItem
 * Description: Defines the available menu options in the main menu
 *****************************************************************************/
typedef enum {
    MENU_START_GAME = 0,
    MENU_RESET_SCORE,
	SHOW_HIGH_SCORE,
    MENU_CREDITS,
    MENU_EXIT
} MenuItem;

/* Menu item text strings */
static const char *menuItems[MENU_ITEM_COUNT] = {
    "Start game",
    "Reset score",
	"High score",
    "Credits",
    "Exit"
};

/* Currently selected menu index */
static int selectedIndex = 0;

/*****************************************************************************
 * Structure: TiltState
 * Description: Holds accelerometer calibration offsets and current readings
 *              for tilt detection functionality
 *****************************************************************************/
typedef struct {
    int8_t xOffset;     // X-axis calibration offset
    int8_t yOffset;     // Y-axis calibration offset
    int8_t zOffset;     // Z-axis calibration offset
    int8_t x;           // Current X-axis reading
    int8_t y;           // Current Y-axis reading
    int8_t z;           // Current Z-axis reading
} TiltState;
static TiltState tilt;

/* Frequencies for musical notes in timer units (µs periods) */
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
    1136, // a - 880 Hz (octave higher)
    1012, // b - 988 Hz (octave higher)
    1912, // c - 523 Hz (octave higher)
    1703, // d - 587 Hz (octave higher)
    1517, // e - 659 Hz (octave higher)
    1432, // f - 698 Hz (octave higher)
    1275, // g - 784 Hz (octave higher)
};

/* OLED color variables for foreground/background theme switching */
static oled_color_t fontColor;
static oled_color_t backgroundColor;

/*****************************************************************************
** Function name:       set_led_bar_position
**
** Description:         Sets the LED bar to show a single LED at the specified
**                      position. Used to indicate game progress/round number.
**
** Parameters:          pos - LED position (0-15, where 0 is rightmost)
** Returned value:      None
*****************************************************************************/
static void set_led_bar_position(uint8_t pos)
{
    uint16_t ledOn;
    ledOn = (uint16_t)0x01 << pos;  // Create bitmask for single LED
    pca9532_setLeds(ledOn, 0xffff); // Turn on specified LED, turn off all others
}

/*****************************************************************************
** Function name:       clear_led_bar
**
** Description:         Turn off led bars.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void clear_led_bar(void)
{
    pca9532_setLeds(0, 0xffff); // Turn off all leds
}

/*****************************************************************************
** Function name:       draw_menu
**
** Description:         Renders the menu on OLED display with selection
**                      highlight. Shows arrow indicator next to selected item.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void draw_menu(void) {
    oled_clearScreen(backgroundColor);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        uint8_t y = 2 + i * 12;  // Calculate vertical position for each menu item

        // Add arrow indicator to selected item, space for others
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%c %s", (i == selectedIndex ? '>' : ' '), menuItems[i]);

        oled_putString(4, y, (uint8_t *)buffer, fontColor, backgroundColor);
    }
}


/*****************************************************************************
** Function name:       is_board_tilted
**
** Description:         Determines if the board is significantly tilted from
**                      its calibrated neutral position. Uses threshold of
**                      ±30 units on X and Y axes.
**
** Parameters:          None
** Returned value:      true if board is tilted beyond threshold, false otherwise
*****************************************************************************/
 uint32_t is_board_tilted(void) {
    acc_read(&tilt.x, &tilt.y, &tilt.z);

    // Apply calibration offsets
    tilt.x += tilt.xOffset;
    tilt.y += tilt.yOffset;
    tilt.z += tilt.zOffset;

    // Check if tilt exceeds threshold on either axis
    if ((tilt.x > 30) || (tilt.x < -30) || (tilt.y > 30) || (tilt.y < -30)) {
        return 1;
    }

    return 0;
}

 /*****************************************************************************
 ** Function name:       adjust_theme
 **
 ** Description:         Reads ambient light sensor and dynamically adjusts
 **                      display theme between dark mode (low light) and light
 **                      mode (bright light). Provides audio feedback on changes.
 **
 ** Parameters:          None
 ** Returned value:      None
 *****************************************************************************/
 uint8_t adjust_theme(void) {
     uint32_t reading = light_read();
     uint32_t prev_fontColor = fontColor;

     // Threshold-based theme switching
     if (reading < 125) {
         // Dark environment
         fontColor = OLED_COLOR_WHITE;
         backgroundColor = OLED_COLOR_BLACK;
     } else {
         // Bright environment
         fontColor = OLED_COLOR_BLACK;
         backgroundColor = OLED_COLOR_WHITE;
     }

     // Audio feedback when theme changes
     if (fontColor == prev_fontColor) return 0;

     play_note(notes[0], 200);
     return 1;
 }

/*****************************************************************************
** Function name:       play_star_wars_theme
**
** Description:         Plays the iconic Star Wars main theme melody using
**                      predefined musical notes. Used for credits screen.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void play_star_wars_theme(void) {
    // Main theme opening phrase
    // "Dah dah dah dah-dah-dah daaah"
    
    // First three notes (G G G)
    play_note(notes[8], 500);  // G
    play_note(notes[8], 500);  // G
    play_note(notes[8], 500);  // G
    
    // Lower octave phrase (C G F E D C G)
    play_note(notes[2], 350);  // C
    play_note(notes[16], 150); // g
    play_note(notes[8], 500);  // G
    play_note(notes[6], 350);  // F
    play_note(notes[4], 150);  // E
    play_note(notes[3], 150);  // D
    play_note(notes[2], 350);  // C
    play_note(notes[16], 150); // g
    play_note(notes[8], 1000); // G
    
    // Second phrase
    play_note(notes[6], 350);  // F
    play_note(notes[4], 150);  // E
    play_note(notes[3], 150);  // D
    play_note(notes[2], 350);  // C
    play_note(notes[16], 150); // g
    play_note(notes[8], 1000); // G
}

/*****************************************************************************
** Function name:       init_tilt_calibration
**
** Description:         Calibrates the accelerometer by reading current
**                      position and calculating offset values. Should be
**                      called when device is in neutral position.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void init_tilt_calibration(void) {
    acc_read(&tilt.x, &tilt.y, &tilt.z);

    tilt.xOffset = -tilt.x;
    tilt.yOffset = -tilt.y;
    tilt.zOffset = 64 - tilt.z;
}

/*****************************************************************************
** Function name:       wait_for_joystick_center_click
**
** Description:         Blocking function that waits until the joystick center
**                      button is pressed. Used for user confirmation prompts.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void wait_for_joystick_center_click(void) {
	while ((joystick_read() & JOYSTICK_CENTER) == 0) {
		delay32Ms(0, 1);
	}
}

/*****************************************************************************
** Function name:       draw_circle
**
** Description:         Draws a circle outline on the OLED display
**
** Parameters:          x0 - center X coordinate
**                      y0 - center Y coordinate
**                      radius - circle radius in pixels
**                      color - pixel color to draw with
** Returned value:      None
*****************************************************************************/
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

/*****************************************************************************
** Function name:       fill_circle
**
** Description:         Fills a circular area on the OLED display by checking
**                      distance from center for each pixel in bounding box.
**
** Parameters:          x0 - center X coordinate
**                      y0 - center Y coordinate
**                      radius - circle radius in pixels
**                      color - pixel color to fill with
** Returned value:      None
*****************************************************************************/
void fill_circle(uint8_t x0, uint8_t y0, uint8_t radius, oled_color_t color) {
    // Iterate through bounding box and fill pixels within radius
    for (int16_t y = -radius; y <= radius; y++) {
        for (int16_t x = -radius; x <= radius; x++) {
            // Use distance formula: if x²+y² ≤ r², point is inside circle
            if (x * x + y * y <= radius * radius) {
                oled_putPixel(x0 + x, y0 + y, color);
            }
        }
    }
}

/*****************************************************************************
** Function name:       play_note
**
** Description:         Generates a square wave audio signal of specified
**                      frequency and duration on the speaker pin. Uses
**                      bit-banging to create PWM-like output.
**
** Parameters:          note - frequency period in microseconds
**                      durationMs - duration to play the note in milliseconds
** Returned value:      None
*****************************************************************************/
void play_note(uint32_t note, uint32_t durationMs) {
    if (note > (uint32_t)0) {
        uint32_t t = 0;
        // Generate square wave for specified duration
        while (t < (durationMs * (uint32_t)1000)) {
            P1_2_HIGH();                        // Set speaker pin high
            delay32Us(0, note / (uint32_t)4);   // Half period delay
            P1_2_LOW();                         // Set speaker pin low
            delay32Us(0, note / (uint32_t)4);   // Half period delay
            t += note;                          // Track elapsed time
        }
    }
    else {
        delay32Ms(0, durationMs);
    }
}

/*****************************************************************************
** Function name:       set_high_score
**
** Description:         Stores new high score value into EEPROM for persistent
**                      storage. Converts 16-bit value to byte array format.
**
** Parameters:          value - new high score in milliseconds
** Returned value:      None
*****************************************************************************/
void set_high_score(uint16_t value) {
    uint8_t buf[2];
    // Split 16-bit value into two bytes (big-endian format)
    buf[0] = (value & (uint16_t)0xFF00) >> 8;  // High byte
    buf[1] = (value & (uint16_t)0x00FF);       // Low byte
    eeprom_write(buf, 8, 2);                   // Write to EEPROM address 0
}

/*****************************************************************************
** Function name:       read_high_score
**
** Description:         Retrieves high score value from EEPROM and converts
**                      from byte array back to 16-bit integer.
**
** Parameters:          None
** Returned value:      High score value in milliseconds
*****************************************************************************/
static uint16_t read_high_score(void) {
    uint8_t readBuf[2];
    eeprom_read(readBuf, 8, 2);  // Read 2 bytes from EEPROM address 8
    // Reconstruct 16-bit value from bytes (big-endian format)
    return ((uint16_t)readBuf[0] << 8) | (uint16_t)readBuf[1];
}

/*****************************************************************************
** Function name:       measure_reaction_time
**
** Description:         Measures user reaction time from visual stimulus to
**                      joystick button press. Configures Timer32 for millisecond
**                      precision timing measurement.
**
** Parameters:          None
** Returned value:      Reaction time in milliseconds (incomplete implementation)
*****************************************************************************/
uint32_t measure_reaction_time(void) {
    // Configure Timer32_1 for millisecond counting
    init_timer32(1, 72000);
    LPC_TMR32B1->TCR = 0x02;  // Reset timer

    // Calculate prescaler for 1ms resolution
    uint32_t prescalerValue = ((SystemFrequency/LPC_SYSCON->SYSAHBCLKDIV) / 1000) - 1;
    LPC_TMR32B1->PR = prescalerValue;
    LPC_TMR32B1->MCR = 0x00;  // No match control
    LPC_TMR32B1->TCR = 0x01;  // Start timer

    wait_for_joystick_center_click();

    uint32_t reactionTimeMs = LPC_TMR32B1->TC;  // Read timer value
    return reactionTimeMs;
}

/*****************************************************************************
** Function name:       oled_putStringHorizontallyCentered
**
** Description:         Displays text horizontally centered on the OLED at
**                      specified vertical position. Calculates center position
**                      based on string length and character width.
**
** Parameters:          y - vertical coordinate for text placement
**                      text - null-terminated string to display
** Returned value:      None
*****************************************************************************/
void oled_putStringHorizontallyCentered(uint8_t y, const char text[]) {
    int textLength = strlen(text);
    // Calculate horizontal center position (assuming 5 pixels per character)
    uint8_t x = (OLED_DISPLAY_WIDTH - (textLength * 5)) / 2;
    oled_putString(x, y, (uint8_t *)text, fontColor, backgroundColor);
}

/*****************************************************************************
** Function name:       show_welcome_screen
**
** Description:         Displays initial splash screen with game title and
**                      current high score retrieved from EEPROM. Waits for
**                      user confirmation before proceeding.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void show_welcome_screen(void) {
    oled_clearScreen(backgroundColor);
    oled_putStringHorizontallyCentered(2, "Welcome");
    oled_putStringHorizontallyCentered(12, "REFLEKS");
    oled_putStringHorizontallyCentered(32, "High score:");

    uint16_t highScoreMs = read_high_score();

    char highScoreMsString[8];
    sprintf(highScoreMsString, "%u ms", highScoreMs);
    oled_putStringHorizontallyCentered(42, highScoreMsString);

    wait_for_joystick_center_click();  // Wait for user to continue
}

/*****************************************************************************
** Function name:       play_startup_animation
**
** Description:         Shows animated "boot up" sequence with sci-fi themed
**                      loading messages and animated dots. Includes audio
**                      feedback and accelerometer calibration.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void play_startup_animation(void) {
    play_note(notes[3], 200);

    const char *frames[] = {".", "..", "..."};
    const char *quotes[] = {
        "Heat up...", "Thrust OK", "Fire laser", "Weapons armed",
        "AI synced", "Engines online", "Core stable", "Warp ready", "Scanning"
    };

    // Display 3 different startup messages with animated dots
    for (int i = 0; i < 9; i++) {
        const char *str = quotes[i];
        for (int f = 0; f < 3; f++) {
            oled_clearScreen(backgroundColor);
            oled_putStringHorizontallyCentered(24, str);
            oled_putStringHorizontallyCentered(36, frames[f]);
            delay32Ms(0, 300);
        }
    }

    init_tilt_calibration();  // Calibrate accelerometer during startup
}


/*****************************************************************************
** Function name:       start_game
**
** Description:         Main game loop that runs 5 rounds of reaction time
**                      testing. Each round shows visual stimulus after random
**                      delay and measures response time. Updates high score
**                      and provides visual/audio feedback.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void start_game(void) {
    uint8_t round = 0;
    uint32_t totalTime = 0;
    uint16_t highScoreMs = read_high_score();

    while (round < 5) {
        set_led_bar_position(round);  // Show progress on LED bar
        led7seg_setChar('0' + round, FALSE);
        adjust_theme();

        // Display waiting screen with circle outline
        oled_clearScreen(backgroundColor);
        draw_circle(OLED_DISPLAY_WIDTH / 2, OLED_DISPLAY_HEIGHT / 2, 28, fontColor);
        oled_putStringHorizontallyCentered(OLED_DISPLAY_HEIGHT / 2 - 4, "WAIT...");
        play_note(notes[2], 250);

        // Random delay before stimulus (0.5-3.5 seconds)
        uint32_t randomDelay = (rand() % 3000) + 500;
        delay32Ms(0, randomDelay);

        fill_circle(OLED_DISPLAY_WIDTH / 2, OLED_DISPLAY_HEIGHT / 2, 28, fontColor);

        // Measure reaction time
        uint32_t reactionTimeMs = measure_reaction_time();
        totalTime += reactionTimeMs;

        // Display results
        char reactionTimeMsString[5];
        sprintf(reactionTimeMsString, "%u", reactionTimeMs);
        oled_clearScreen(backgroundColor);
        strcat(reactionTimeMsString, " ms");
        oled_clearScreen(backgroundColor);
        oled_putStringHorizontallyCentered(OLED_DISPLAY_HEIGHT / 2, reactionTimeMsString);

        // Update high score if new record achieved
        if ((reactionTimeMs < highScoreMs) || (highScoreMs == (uint16_t)0)) {
            oled_putStringHorizontallyCentered(OLED_DISPLAY_HEIGHT / 2 + 12, "NEW RECORD!");
            set_high_score(reactionTimeMs);
            highScoreMs = reactionTimeMs;
            play_note(notes[0], 100);
            play_note(notes[5], 200);
            play_note(notes[10], 400);
        }

        round++;
        delay32Ms(0, 600);
        wait_for_joystick_center_click();
    }
    oled_clearScreen(backgroundColor);
    oled_putStringHorizontallyCentered(10, "Game Complete!");

    char avgTimeStr[16];
    snprintf(avgTimeStr, sizeof(avgTimeStr), "Avg: %u ms", totalTime / 5);
    oled_putStringHorizontallyCentered(25, avgTimeStr);

    char bestTimeStr[16];
    snprintf(bestTimeStr, sizeof(bestTimeStr), "Best: %u ms", read_high_score());
    oled_putStringHorizontallyCentered(40, bestTimeStr);
    clear_led_bar();
    led7seg_setChar('0', FALSE);
    delay32Ms(0, 1000);
    wait_for_joystick_center_click();
}

/*****************************************************************************
** Function name:       handle_menu
**
** Description:         Processes menu navigation using joystick input.
**                      Handles up/down navigation and center button selection.
**                      Also monitors for board tilt to reset high score.
**
** Parameters:          None
** Returned value:      Selected menu item index (MenuItem enum value)
*****************************************************************************/
int handle_menu(void) {
    uint8_t previous_joy = 0xFF;  // Store previous joystick state for edge detection

    while (1) {
        uint8_t joy = joystick_read();

        // Navigate down (with edge detection to prevent rapid scrolling)
        if ((joy & JOYSTICK_DOWN) && !(previous_joy & JOYSTICK_DOWN)) {
            selectedIndex = (selectedIndex + 1) % MENU_ITEM_COUNT;
            draw_menu();
        }
        // Navigate up (with wraparound)
        else if ((joy & JOYSTICK_UP) && !(previous_joy & JOYSTICK_UP)) {
            selectedIndex = (selectedIndex - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
            draw_menu();
        }
        // Select current item
        else if ((joy & JOYSTICK_CENTER) && !(previous_joy & JOYSTICK_CENTER)) {
            return selectedIndex;
        }

        previous_joy = joy;

        // Easter egg: Reset high score when board is tilted
        if (is_board_tilted()) {
            set_high_score(9999);
            oled_putStringHorizontallyCentered((OLED_DISPLAY_HEIGHT / 2) + 16, "Reset HS");
            delay32Ms(0, 500);
        }

        if (adjust_theme()) {
        	draw_menu();
        }
        delay32Ms(0, 50);
    }
}

/*****************************************************************************
** Function name:       show_main_menu
**
** Description:         Main menu loop that displays menu and handles user
**                      selections. Dispatches to appropriate functions based
**                      on menu choice.
**
** Parameters:          None
** Returned value:      None
*****************************************************************************/
void show_main_menu(void) {
    while(1) {
        draw_menu();
        MenuItem selection = handle_menu();
        oled_clearScreen(backgroundColor);
        play_note(notes[0], 200);

        switch (selection) {
            case MENU_START_GAME:
                start_game();
                break;

            case MENU_RESET_SCORE:
                set_high_score(9999);
                oled_putStringHorizontallyCentered((OLED_DISPLAY_HEIGHT / 2) + 16, "Reset HS");
                delay32Ms(0, 800);
                break;

			case SHOW_HIGH_SCORE:
				oled_putStringHorizontallyCentered(32, "High score:");
				uint16_t highScoreMs = read_high_score();
				char highScoreMsString[8];
				sprintf(highScoreMsString, "%u ms", highScoreMs);
				oled_putStringHorizontallyCentered(42, highScoreMsString);
				delay32Ms(0, 1000);
				wait_for_joystick_center_click();
				break;

            case MENU_CREDITS:
                oled_putStringHorizontallyCentered(20, "by");
                oled_putStringHorizontallyCentered(32, "group");
                oled_putStringHorizontallyCentered(44, "G02 :D");
                play_star_wars_theme();
                break;

            case MENU_EXIT:
                oled_putStringHorizontallyCentered(20, "Exiting...");
                play_note(notes[10], 200);
                play_note(notes[5], 200);
                play_note(notes[1], 200);
                delay32Ms(0, 400);
                oled_clearScreen(backgroundColor);
                return;
        }
    }
}

/*****************************************************************************
** Function name:       main
**
** Description:         Program entry point. Initializes all peripherals,
**                      configures hardware modules, displays startup sequence,
**                      and enters main menu loop. Sets up GPIO, timers, I2C,
**                      SPI, OLED, sensors, and audio subsystem.
**
** Parameters:          None
** Returned value:      Integer
*****************************************************************************/
int main(void) {
    // Initialize GPIO subsystem (required for most peripherals)
    GPIOInit();

    // Initialize 32-bit timers for timing functions
    init_timer32(0, 10);  // Timer 0

    // Initialize I2C bus as master for sensor communication
    I2CInit((uint32_t)I2CMASTER, 0);

    // Initialize SPI (SSP) for OLED communication
    SSPInit();

    // Initialize OLED display module
    oled_init();
    
    // Initialize and enable ambient light sensor (ISL29003)
    light_init();
    light_enable();
    led7seg_init();

    // Seed random number generator with light sensor reading
	srand(light_read());
    pca9532_init();
    clear_led_bar();
    eeprom_init();
    acc_init();
    joystick_init();

    /* ---- Speaker Hardware Setup ---- */

    // Configure PWM pin for speaker output with low-pass filter
    GPIOSetDir(PORT1, 2, OUTPUT);
    // Set pin to PWM mode via IOCON register
    LPC_IOCON->JTAG_nTRST_PIO1_2 = (LPC_IOCON->JTAG_nTRST_PIO1_2 & ~0x7) | 0x01;

    // Initialize LM4811 analog audio amplifier control pins
    GPIOSetDir(PORT3, 0, OUTPUT);  // Clock pin
    GPIOSetDir(PORT3, 1, OUTPUT);  // Up/Down pin
    GPIOSetDir(PORT3, 2, OUTPUT);  // Shutdown pin

    // Configure amplifier initial state (disabled)
    GPIOSetValue(PORT3, 0, LOW);   // LM4811-clk
    GPIOSetValue(PORT3, 1, LOW);   // LM4811-up/dn
    GPIOSetValue(PORT3, 2, LOW);   // LM4811-shutdn

    /* ---- End Speaker Setup ---- */

    // Initialize display theme based on ambient light
    adjust_theme();

    // Show startup animation and calibrate sensors
    play_startup_animation();

    // Display welcome screen with high score
    show_welcome_screen();

    // Enter main menu loop
    show_main_menu();

    return 0;
}
