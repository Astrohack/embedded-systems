/*****************************************************************************
 *   Project: Reflex
 *   Description: This program simulates a game to measure the user's reaction time based on a visual trigger.
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

#define P1_2_HIGH() (LPC_GPIO1->DATA |= ((uint16_t)0x1<<2))
#define P1_2_LOW()  (LPC_GPIO1->DATA &= ~((uint16_t)0x1<<2))
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT 0


/*****************************************************************************
** Function name:		waitForJoystickCenterClick
**
** Descriptions:		Waits for the joystick center button to be clicked
**
** parameters:			None
** Returned value:		None
**
*****************************************************************************/
static void waitForJoystickCenterClick(void) {
    uint8_t joyState = joystick_read();
    while ((joyState & JOYSTICK_CENTER) == 0) {
        joyState = joystick_read();
    }
}

static uint32_t notes[] = {
        2272, // A - 440 Hz
        2024, // B - 494 Hz
        3816, // C - 262 Hz
        3401, // D - 294 Hz
        3030, // E - 330 Hz
        2865, // F - 349 Hz
        2551, // G - 392 Hz
        1136, // a - 880 Hz
        1012, // b - 988 Hz
        1912, // c - 523 Hz
        1703, // d - 587 Hz
        1517, // e - 659 Hz
        1432, // f - 698 Hz
        1275, // g - 784 Hz
};

/*****************************************************************************
** Function name:		playNote
**
** Descriptions:		Plays a given note for a given duration
**
** parameters:			note - the frequency of the note, durationMs - the play duration in milliseconds
** Returned value:		None
**
*****************************************************************************/
static void playNote(uint32_t note, uint32_t durationMs) {

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

static oled_color_t fontColor;
static oled_color_t backgroundColor;

/*****************************************************************************
** Function name:       adjustColors
**
** Descriptions:        Changes color style based on readings from light sensor
**
** parameters:          None
** Returned value:      1 if style has changed else 0 
**
*****************************************************************************/
static uint32_t adjustColors(void) {
    uint32_t reading = light_read();
    uint32_t limit = 250;
    uint32_t prev_fontColor = fontColor;
    if (reading < 125) {
        fontColor = OLED_COLOR_WHITE;
        backgroundColor = OLED_COLOR_BLACK;
    } else {
        fontColor = OLED_COLOR_BLACK;
        backgroundColor = OLED_COLOR_WHITE;
    }
    return fontColor != prev_fontColor;
}

/*****************************************************************************
** Function name:       resetHighScore
**
** Descriptions:        Resets eeprom high score value
**
** parameters:          value to be set as high score
** Returned value:      None
**
*****************************************************************************/
void resetHighScore(uint16_t value) {
	uint8_t buf[2];
	buf[0]= (value & (uint16_t)0xFF00) >> 8;
	buf[1]= (value & (uint16_t)0x00FF);
	eeprom_write(buf,0,2);
}

static void oled_putStringHorizontallyCentered(uint8_t y, const char text[]) {
    int textLength = strlen(text);
    oled_putString((OLED_DISPLAY_WIDTH - (textLength * 5)) / 2, y, (const uint8_t *)text, fontColor, backgroundColor);
}

static void startingScreenOled(void) {
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
}

/**
* Animacja startowa: prosty "loading" z kropkami
*/
static void playStartupAnimation(void) {
	const char *frames[] = {".", "..", "..."};
	const char *quotes[] = {"Heating up systems", "Thrusters online", "Firing lasers"};
	for (int i = 0; i < 3; i++) {
		const char *str = quotes[i];
		for (int f = 0; f < 3; f++) {
			oled_clearScreen(backgroundColor);
			oled_putStringHorizontallyCentered(24, str);
			oled_putStringHorizontallyCentered(36, frames[f]);
			delay32Ms(0, 300);
		}
	}
}


int main(void) {
    // 1) Inicjalizacja GPIO (konieczna do działania wielu peryferiów)
    GPIOInit();

    // 2) Timer tylko do opóźnień lub jako część wymagań checkpointa

    /******************************************************************************
     * TIMER (Timer/Counter) - Overview
     *
     * Timers in the LPC1343 family are 32-bit Timer/Counter peripherals used for:
     *   • Measuring time intervals
     *   • Generating periodic interrupts
     *   • Creating PWM signals (basic)
     *   • External event counting via CAPx pins
     *
     * Each timer (TIMER0, TIMER1, TIMER2, TIMER3) can run independently.
     *
     * =============================================================================
     * Operating Modes:
     * -----------------------------------------------------------------------------
     *  • Timer Mode     : Counts based on PCLK (Peripheral Clock)
     *  • Counter Mode   : Counts external events on CAPx.x pin (rising/falling/both edges)
     *
     * =============================================================================
     * Key Features:
     * -----------------------------------------------------------------------------
     *  • Match Control:
     *     - Up to 4 match registers (MR0–MR3)
     *     - Actions on match: interrupt, reset, stop timer
     *
     *  • Capture Control:
     *     - Up to 4 capture channels (CAP0.0–CAP0.3)
     *     - Capture current TC value on external signal edge
     *
     *  • Interrupts:
     *     - Match interrupts (MR0–MR3)
     *     - Capture interrupts (CAP0–CAP3)
     *
     * =============================================================================
     * TIMERx Registers (Base Address: TIMER0 = 0x40004000)
     * -----------------------------------------------------------------------------
     *  Register Name    | Offset | Description
     *  -----------------|--------|-----------------------------------------------
     *  IR               | 0x00   | Interrupt Register (match/capture flags)
     *  TCR              | 0x04   | Timer Control Register (enable, reset)
     *  TC               | 0x08   | Timer Counter (counts time/ticks)
     *  PR               | 0x0C   | Prescale Register (divide PCLK)
     *  PC               | 0x10   | Prescale Counter (increases until PR match)
     *  MCR              | 0x14   | Match Control Register (actions on match)
     *  MR0–MR3          | 0x18–0x24 | Match Registers (compare against TC)
     *  CCR              | 0x28   | Capture Control Register
     *  CR0–CR3          | 0x2C–0x38 | Capture Registers
     *  EMR              | 0x3C   | External Match Register (control EMx pins)
     *  CTCR             | 0x70   | Count Control Register (Timer/Counter mode select)
     *
     * =============================================================================
     * Notes:
     * -----------------------------------------------------------------------------
     *  • Timers are driven by PCLK (Peripheral Clock)
     *    - PCLK_TIMERx = CCLK / divider (configurable in PCLKSELx)
     *
     *  • To use:
     *    1. Enable timer peripheral via PCONP (bit 1 = TIMER0, bit 2 = TIMER1, etc.)
     *    2. Configure match/capture registers and control bits
     *    3. Enable timer by setting TCR |= (1 << 0)
     *
     *  • Match example:
     *      - Set MR0 = desired_ticks
     *      - Configure MCR to generate interrupt or reset TC on match
     *
     *  • Capture example:
     *      - Enable capture on rising/falling edge of CAPx.x pin
     *      - Value of TC is latched into CRx on external event
     *
     ******************************************************************************/
    init_timer32(0, 10);

    // 3) Inicjalizacja magistrali I2C i SSP (OLED wymaga SSP)
    /******************************************************************************
     * I2C (Inter-Integrated Circuit) - Overview
     *
     *  - Synchronous, multi-master, multi-slave serial communication protocol
     *  - Only 2 lines required:
     *      - SDA: Serial Data Line
     *      - SCL: Serial Clock Line
     *  - Supports multiple devices on the same bus (identified by 7-bit addresses)
     *
     *  Communication basics:
     *      - Master initiates communication
     *      - Start condition -> Address + R/W bit -> ACK/NACK -> Data transfer -> Stop condition
     *      - Each data transfer is 8 bits, followed by an ACK/NACK bit
     *
     *  Use cases:
     *      - EEPROMs, light sensor, ADCs, DACs, etc.
     *
     *  =============================================================================
     *  LPC I2C0 Registers (Base address: 0x4001C000)
     *  -----------------------------------------------------------------------------
     *  Register Name    | Address Offset | Description
     *  -----------------|----------------|------------------------------------------
     *  I2CONSET         | 0x000          | Control Set Register (start, enable, etc.)
     *  I2STAT           | 0x004          | Status Register (I2C state machine codes)
     *  I2DAT            | 0x008          | Data Register (read/write data)
     *  I2ADR0           | 0x00C          | Slave Address Register 0
     *  I2SCLH           | 0x010          | SCL Duty Cycle High Half Word
     *  I2SCLL           | 0x014          | SCL Duty Cycle Low Half Word
     *  I2CONCLR         | 0x018          | Control Clear Register (clear flags)
     *  I2MMCTRL         | 0x01C          | Monitor mode control (optional)
     *  I2ADR1           | 0x020          | Slave Address Register 1
     *  I2ADR2           | 0x024          | Slave Address Register 2
     *  I2ADR3           | 0x028          | Slave Address Register 3
     *  I2DATA_BUFFER    | 0x02C          | Data buffer register (monitor mode)
     *
     *  To use I2C:
     *   1. Power up I2C peripheral and set PCLK
     *   2. Configure pins (e.g., P0.27 = SDA, P0.28 = SCL via IOCON)
     *   3. Set SCLH/SCLL for desired clock speed
     *   4. Set I2EN bit in I2CONSET to enable I2C
     *   5. Use state codes in I2STAT to drive the I2C master/slave logic
     ******************************************************************************/
    I2CInit((uint32_t)I2CMASTER, 0);

    /******************************************************************************
     * SPI (Serial Peripheral Interface) - Overview
     *
     * SPI is a full-duplex, synchronous serial communication protocol used to
     * transfer data between electronic devices. It follows a master-slave architecture.
     *
     * Signal Lines:
     *   • SCK  (Serial Clock)       : Clock signal generated by the master to sync data transfer
     *   • MOSI (Master Out, Slave In): Data line from master to slave
     *   • MISO (Master In, Slave Out): Data line from slave to master
     *   • SS   (Slave Select)        : Optional line used by the master to activate a specific slave
     *
     * How SPI Works:
     *   • The master initiates communication by:
     *       - Generating the SCK signal
     *       - Pulling the SS line low (if used) to select the target slave
     *
     *   • Data Transmission:
     *       - The master sends data bit-by-bit over MOSI
     *       - The slave simultaneously sends data over MISO
     *       - Data is clocked on one edge of SCK (configurable via CPHA)
     *
     *   • End of Communication:
     *       - The master pulls SS high (if used), ending the session
     *
     * Clock Configuration:
     *   • CPOL (Clock Polarity): Sets idle level of SCK (0 = low, 1 = high)
     *   • CPHA (Clock Phase)   : Defines which SCK edge is used for sampling
     *
     * Data Format:
     *   • Usually 8 bits per frame, but 4–16 bits supported by some peripherals
     *   • Most Significant Bit (MSB) first, configurable
     *
     * =============================================================================
     * LPC1343 SPI0 Hardware Registers (Base Address: 0x40020000)
     * -----------------------------------------------------------------------------
     *  Register Name    | Address Offset | Description
     *  -----------------|----------------|------------------------------------------
     *  SPCR             | 0x000          | SPI Control Register (enable, CPOL, CPHA, bits)
     *  SPSR             | 0x004          | SPI Status Register (SPIF, WCOL, etc.)
     *  SPDR             | 0x008          | SPI Data Register (read/write)
     *  SPCCR            | 0x00C          | SPI Clock Counter Register (must be even ≥ 8)
     *  SPINT            | 0x01C          | SPI Interrupt Register (status flag)
     *
     *  Example Init Steps for Master Mode:
     *   1. Power up SSP [LPC_SYSCON->PRESETCTRL |= (0x1<<0)] peripheral [LPC_SYSCON->SYSAHBCLKCTRL |= (1<<11)] and configure PCLK
     *   2. Set IOCON for correct pin functions (e.g., P0.8, P0.9, P0.18)
     *   3. Set data format and size in CR0 (e.g., 8-bit SPI mode)
     *   4. Configure clock prescaler in CPSR
     * 
     * Notes:
     *   • Clock speed is derived from: SPI_Clock = PCLK / SPCCR
     *
     ******************************************************************************/
    SSPInit();

    // 4) Inicjalizacja modułu wyświetlacza i czujnika światła
    oled_init();

    // Init and enable light sensor ISL29003 
    light_init();
    light_enable();

    // 5) Inicjalizacja EEPROM
    eeprom_init();
    resetHighScore(2137);

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
    adjustColors();

    // 7) Clear screen and show welcome screen with high score
    playStartupAnimation();
    startingScreenOled();

    // 8) Set random generator seed to reading from light sensor
    srand(light_read());

    playNote(notes[3],200);

    // Pętla główna
    while (1) {
        // Na tym etapie nic więcej nie robimy.
        // Główna logika gry pojawi się w finalnej wersji.

		if (adjustColors()) {
			startingScreenOled();
			if (fontColor == OLED_COLOR_WHITE) {
				playNote(notes[0],200);
			}else {
				playNote(notes[4],200);
			}
		}
		delay32Ms(0, 300);
    }
}
