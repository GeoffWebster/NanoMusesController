/* Pre-amp Controller
*********************

Author Geoff Webster

Current Ver  4.0 11 February 2024
- Amended RC coding for backlight so only toggles backlight (leaving source, volume and mute unchanged)

3.0 Date	14 July 2023
- Changed mas6116::mas6116 construct in mas6116.cpp so that MUTE pin is initialized LOW
	Ensures MUTE remains LOW for two seconds after power startup

Ver 2.0 Date	27 April 2022
- Changed mute pin to match new Controller board v2.0 (mutePin = 9). Mute pin used previously was A2 (on Ver 1.0 board)
- Added code to setup() routine which displays the SW version for two seconds at startup

*/

#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <RC5.h>
#include <rotary.h>
#include <Muses72323.h>
//#include "custom.h"

#define VERSION_NUM "0.1" // Current software version number

/******* MACHINE STATES *******/
#define STATE_RUN 0 // normal run state
#define STATE_IO 1	// when user selects input/output
#define STATE_OFF 4 // when power down
#define ON LOW
#define OFF HIGH
#define STANDBY 0 // Standby
#define ACTIVE 1  // Active

#define EEPROM_FIRST_USE 0 // EEPROM location: First use
#define EEPROM_VOLUME 1	   // EEPROM location: volume
#define EEPROM_SOURCE 2	   // EEPROM location: source
#define EEPROM_BALANCE 3   // EEPROM location: balance

#define TIME_EXITSELECT 5 //** Time in seconds to exit I/O select mode when no activity

#define printByte(args) write(args);

/******* TIMING *******/
unsigned long milOnButton;	// Stores last time for switch press
unsigned long milOnAction;	// Stores last time of user input
unsigned long milOnFadeIn;	// LCD fade timing
unsigned long milOnFadeOut; // LCD fade timing

/********* Global Variables *******************/
signed int volume;	 // current volume, between 0 and -447
unsigned char backlight; // current backlight state
int counter = 0;
unsigned char source = 1;	 // current input channel
unsigned char oldsource = 1; // previous input channel
unsigned char oldtoggle;
unsigned char isMuted;	 // current mute status
unsigned char state = 0; // current machine state
unsigned char buttonState;
bool btnstate = 0;
unsigned char oldbtnstate = 0;
unsigned char rotarystate; // current rotary encoder status
unsigned char result = 0;  // current rotary status

int analogPin = A1;

const char *inputName[] = {
	"Phono ",
	"Media ",
	"CD    ",
	"Tuner "}; // Elektor i/p board

char buffer1[20] = "";

// LCD construct
LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display

// define encoder pins
#define encoderPinA 6
#define encoderPinB 5
#define encoderbtn 7
// Rotary construct
Rotary rotary = Rotary(encoderPinA, encoderPinB, encoderbtn);

// define IR input
#define IR_PIN 8
// RC5 construct
RC5 rc5(IR_PIN);

// define preAmp control pins
#define address_Muses 0
#define muses_CS 10
// preAmp construct
Muses72323 Muses(address_Muses, muses_CS);

// Function prototypes
void RC5Update(void);
void setIO();
void volumeUpdate();
void buttonPressed();
void setVolume();
void sourceUpdate();
void mute();
void unMute();
void toggleMute();
void saveIOValues();

// Powerdown Interrupt service routine
ISR(ANALOG_COMP_vect)
{
	saveIOValues();
	backlight = STANDBY;
	lcd.noDisplay();
	lcd.noBacklight(); // Turn off backlight
	mute();			   // mute output
	state = STATE_OFF;
}

void saveIOValues()
{
	EEPROM.update(EEPROM_VOLUME, -volume);
	EEPROM.update(EEPROM_SOURCE, source);
}

void setIO()
{
	digitalWrite(oldsource, LOW);
	digitalWrite(source, HIGH);
	lcd.setCursor(0, 0);
	lcd.print(inputName[source - 1]);
}

void RotaryUpdate()
{
	switch (state)
	{
	case STATE_RUN:
		volumeUpdate();
		break;
	case STATE_IO:
		sourceUpdate();
		if ((millis() - milOnButton) > TIME_EXITSELECT * 1000)
		{
			state = STATE_RUN;
		}
		break;
	default:
		break;
	}
}

void volumeUpdate()
{
	// 0 = no rotation, 10 = clockwise,  20 = counter clockwise
	// result = rotary.process();
	switch (rotary.process())
	{
	case 0:
		// check button
		buttonPressed();
		break;
	case DIR_CW:
		if (volume < 0)
		{
			if (isMuted)
			{
				unMute();
			}
			volume = volume + 1;
			setVolume();
		}
		break;
	case DIR_CCW:
		if (volume > -447)
		{
			if (isMuted)
			{
				unMute();
			}
			volume = volume - 1;
			setVolume();
		}
	default:
		break;
	}
}

void setVolume()
{
	Muses.setVolume(volume, volume);
	double atten = (double(volume) / 4);
	lcd.setCursor(0,2);
	lcd.print("         ");
	lcd.setCursor(0,2);
	lcd.print("Vol: ");
	lcd.print(volume);
	lcd.setCursor(0, 3);
	lcd.print("Att: ");
	lcd.print(atten);
	lcd.print("dB  ");
}

// button pressed routine
void buttonPressed()
{
	if (rotary.buttonPressedReleased(20))
	{
		switch (state)
		{
		case STATE_RUN:
			state = STATE_IO;
			milOnButton = millis();
			break;
		default:
			break;
		}
	}
}

void sourceUpdate()
{
	// 0 = do nothing, 10 = clockwise,  20 = counter clockwise
	// result = rotary.process();
	switch (rotary.process())
	{
	case DIR_CW:
		oldsource = source;
		milOnButton = millis();
		if (oldsource < 4)
		{
			source++;
		}
		else
		{
			source = 1;
		}
		setIO();
		break;
	case DIR_CCW:
		oldsource = source;
		milOnButton = millis();
		if (source > 1)
		{
			source--;
		}
		else
		{
			source = 4;
		}
		setIO();
		break;
	default:
		break;
	}
}

void RC5Update()
{
	/*
	System addresses and codes used here match RC-5 infra-red codes for amplifiers (and CDs)
	*/
	unsigned char toggle;
	unsigned char address;
	unsigned char command;
	// Poll for new RC5 command
	if (rc5.read(&toggle, &address, &command))
	{
		if (address == 0x10) // standard system address for preamplifier
		{
			switch (command)
			{
			case 1:
				// Phono
				if ((oldtoggle != toggle))
				{
					if (!backlight)
					{
						unMute(); // unmute output
					}
					oldsource = source;
					source = 1;
					setIO();
				}
				break;
			case 3:
				// Tuner
				if ((oldtoggle != toggle))
				{
					if (!backlight)
					{
						unMute(); // unmute output
					}
					oldsource = source;
					source = 4;
					setIO();
				}
				break;
			case 7:
				// CD
				if ((oldtoggle != toggle))
				{
					if (!backlight)
					{
						unMute(); // unmute output
					}
					oldsource = source;
					source = 3;
					setIO();
				}
				break;
			case 8:
				// Media
				if ((oldtoggle != toggle))
				{
					if (!backlight)
					{
						unMute(); // unmute output
					}
					oldsource = source;
					source = 2;
					setIO();
				}
				break;
			case 13:
				// Mute
				if ((oldtoggle != toggle))
				{
					toggleMute();
				}
				break;
			case 16:
				// Increase Vol / reduce attenuation
				if (isMuted)
				{
					unMute();
				}
				if (volume < 0)
				{
					volume = volume + 1;
					setVolume();
				}
				break;
			case 17:
				// Reduce Vol / increase attenuation
				if (isMuted)
				{
					unMute();
				}
				if (volume > -447)
				{
					volume = volume - 1;
					setVolume();
				}
				break;
			case 59:
				// Display Toggle
				if ((oldtoggle != toggle))
				{
					lcd.setCursor(0, 2);
					if (backlight)
					{
						backlight = STANDBY;
						lcd.noBacklight(); // Turn off backlight
						mute();			   // mute output
					}
					else
					{
						backlight = ACTIVE;
						lcd.backlight(); // Turn on backlight
						unMute();		 // unmute output
					}
				}
				break;
			}
		}
		else if (address == 0x14) // system address for CD
		{
			if ((oldtoggle != toggle))
			{
				if (command == 53) // Play
				{
					oldsource = source;
					source = 3;
					setIO();
				}
			}
		}
		oldtoggle = toggle;
	}
}

void unMute()
{
	if (!backlight)
	{
		backlight = ACTIVE;
		lcd.backlight(); // Turn on backlight
	}
	isMuted = 0;
	setVolume();
	lcd.setCursor(0, 1);
	lcd.print("      ");
}

void mute()
{
	isMuted = 1;
	Muses.mute();
	lcd.setCursor(0, 1);
	lcd.print("Muted ");
}

void toggleMute()
{
	if (isMuted)
	{
		unMute();
	}
	else
	{
		mute();
	}
}

void setup()
{
	for (size_t pinOut = 1; pinOut < 5; pinOut++)
	{
		pinMode(pinOut, OUTPUT);
		digitalWrite(pinOut, LOW);
	}
	lcd.init();		 // initialize the lcd
	lcd.backlight(); // turn on LCD backlight
	backlight = 1;
	lcd.home(); // LCD cursor to home position

	// show software version briefly in display
	lcd.setCursor(0, 3);
	sprintf(buffer1, "SW ver  " VERSION_NUM);
	lcd.printstr(buffer1);
	delay(2000);
	sprintf(buffer1, "              ");
	lcd.home();

	// test for first use settings completed. If not, carry out
	if (EEPROM.read(EEPROM_FIRST_USE))
	{
		// Set saved source for first time
		EEPROM.write(EEPROM_SOURCE, 1);
		EEPROM.write(EEPROM_FIRST_USE, 0x00);
	}

	// Load source, volume, balance values
	//volume = -EEPROM.read(EEPROM_VOLUME);
	volume = -447;
	source = EEPROM.read(EEPROM_SOURCE);

	// AVR native C code for power-down interrupt setup
	// Setup Analog Compare Interrupt
	ADCSRB = 0x40;									   // Analog Comparator Multiplexer Enable
	ADCSRA = 0x00;									   // ADC Disabled
	ADMUX = 0x01;									   // Arduino pin A1
	ACSR |= (1 << ACBG) | (1 << ACIS1) | (1 << ACIS0); // Analog Comparator Bandgap Select, Interrupt on rising edge
	ACSR |= (1 << ACIE);							   // Analog Comparator Interrupt enable

	// Initialize muses (SPI, pin modes)...
	Muses.begin();
	Muses.setExternalClock(false); // must be set!
	Muses.setZeroCrossingOn(true);
	Muses.mute();
	isMuted = 0;
	// Load saved settings (source)
	// set startup volume
	setVolume();
	// set source
	setIO();
}
void loop()
{
	RC5Update();
	RotaryUpdate();
}