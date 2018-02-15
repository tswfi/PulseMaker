// Simple pulse generator
// Author: Tatu Wikman
// License: MIT
//
// Very basic pulse generator for
//
// Two different pulses can be fired, and the timing between them can be varied from 1ms => 10sec
//
// Modes of operation:
// One pulse:
// Default mode, single pulse of specific lentgh
//
// Dual pulse:
// Dual pulse mode. pulse1 then idle time and pulse2
//
// Continuos pulsing:
// Pulse stream with variable pulse and idle time
//
// To switch between modes hold down the encoder button to get to the next mode
//
// To edit the time values double click the encoder button to enter edit mode. Double clicking again will
// edit the next value and go back to the pulse mode.
//
// On each mode the pulse(s) will be triggered by clicking the encoder once. For continous mode
// clicking the encoder again will stop the pulsing
//

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>
#include <ClickEncoder.h>
#include <TimerOne.h>
#include <U8glib.h>

// rotary encoder variables
int16_t oldEncPos, encPos;
uint8_t buttonState;

// oled settings
#define OLED_ADDR 0x3C

// oled
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NO_ACK);

// rotary encoder settings
#define pinA A2       // encoder A
#define pinB A1       // encoder B
#define pinSw A0      // encoder switch (click on this will have a small delay)
#define STEPS 2       // encoder step count
#define pinExtraSw A3 // switch 2 (extra for foot switch etc)

int extraButtonLastState = 1;            // tmp for debounce
int extraButtonState = 0;                // state (for checking if we have released also)
unsigned long extraButtonLastMillis = 0; // for debounce
unsigned long extraButtonDebounce = 10;  // debounce time

// our encoder
ClickEncoder encoder(pinA, pinB, pinSw, STEPS);

// output pin
#define OUTPIN 13
// and its state
bool out;

// our different states
// TODO: draw these for easier debugging :)
enum States
{
    // these always go back to their view counterparts
    PULSE_ONE,  // first pulse, goes to PULSE_IDLE or back to VIEW_ONCE
    PULSE_IDLE, // idle time between pulses, goes to PULSE_TWO or PULSE_ONE if viewstate is VIEW_CONTINUOUS
    PULSE_TWO,  // second pulse, goes back to VIEW_TWICE

    // click goes to the pulse counterpart
    // double click goes to edit
    // long click sycles between these
    VIEW_ONCE,      // primed for PULSE_ONCE
    VIEW_TWICE,     // primed for PULSE_TWICE
    VIEW_CONTINOUS, // primed for PULSE_CONTINOUS

    // for view_once only edit_pulse1 is valid
    // for view_twice all of these should be available
    // for view_cont pulse1 and idle should be available
    // double click takes back to te view we came from
    EDIT_PULSE1, // editing value for pulse1
    EDIT_PULSE2, // editing value for pulse2
    EDIT_IDLE,   // editing value for idle time
};
enum States state;
uint8_t viewstate; // hold the view information while doing edits

// pulse1 the length of the first pulse in us
// pulse2 the length of the second pulse in us
// idle idling time between pulses in us
// if continous mode is on pulse1 and idle are used
uint16_t pulse1, idle, pulse2;

// when running the pulsing these hold the values for the pulse and are decremented
// by the timer.
uint16_t pulse1_counter, idle_counter, pulse2_counter;

#define MIN_LENGTH 1

// EEPROM locations for the values
#define P1ADDR 0
#define P2ADDR 2
#define IDLEADDR 4

// are we running the pulsing right now
bool btn_held_handled; // have we handled this btn_held?

bool pulsing(void)
{
    if (state == PULSE_ONE or state == PULSE_TWO or state == PULSE_IDLE)
    {
        return true;
    }
    else
    {
        return false;
    }
}
bool editing(void)
{
    if (state == EDIT_PULSE1 or state == EDIT_PULSE2 or state == EDIT_IDLE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void handle_pulsing(void)
{
    // always default to not outputting
    out = LOW;
    // check if we are pulsing and what we should do in the pulse
    if (pulsing())
    {
        // PULSE_ONE is going
        if (state == PULSE_ONE)
        {
            // default to HIGH on output
            out = HIGH;
            // is the pulse over?
            if (pulse1_counter >= pulse1)
            {
                // pulse has ended, where should we go
                // either back to view_once or idle
                if (viewstate == VIEW_ONCE)
                {
                    state = VIEW_ONCE;

                    out = LOW;
                }
                else
                {
                    state = PULSE_IDLE;
                }
            }
        }

        // pulse idle is going
        if (state == PULSE_IDLE)
        {
            // default to LOW output
            out = LOW;
            // are we over the idle time
            if (idle_counter >= idle)
            {
                if (viewstate == VIEW_CONTINOUS)
                {

                    // back to PULSE_ONE and reset counters
                    pulse1_counter = 0;
                    pulse2_counter = 0;
                    idle_counter = 0;
                    state = PULSE_ONE;
                }
                else
                {
                    state = PULSE_TWO;
                }
            }
        }

        if (state == PULSE_TWO)
        {
            // default to output HIGH
            out = HIGH;
            if (pulse2_counter >= pulse2)
            {
                // always back to view twice from here
                state = VIEW_TWICE;
            }
        }
    }
    // write out our pin state, TODO: would probably be better to
    // only call this if the out output state really changed...
    digitalWrite(OUTPIN, out);
}

void handle_extra_button(void)
{
    // read and debounce the button
    int val = digitalRead(pinExtraSw);
    if (val != extraButtonLastState)
    {
        extraButtonLastMillis = millis();
    }
    // we got a stable button state
    if ((millis() - extraButtonLastMillis) > extraButtonDebounce)
    {
        // if the state has changed
        if (val != extraButtonState)
        {
            extraButtonState = val;
            // if the button is down
            if (extraButtonState == LOW)
            {
                if (not pulsing() and not editing())
                {
                    // start pulsing
                    pulse1_counter = 0;
                    pulse2_counter = 0;
                    idle_counter = 0;
                    state = PULSE_ONE;
                }
                // if we are pulsing in continuous mode
                else if (pulsing() and viewstate == VIEW_CONTINOUS)
                {
                    // stop pulsing
                    state = VIEW_CONTINOUS;
                }
            }
        }
    }
    extraButtonLastState = val;
}

// timer interrupt 1000hz
void timerIsr()
{

    handle_pulsing();

    // update pulse_counters if we are in that state
    if (state == PULSE_ONE)
    {
        pulse1_counter++;
    }
    if (state == PULSE_IDLE)
    {
        idle_counter++;
    }
    if (state == PULSE_TWO)
    {
        pulse2_counter++;
    }

    // update encoder
    encoder.service();
}

// eeprom read write
void EEPROMWriteInt16(int address, uint16_t value)
{
    byte two = (value & 0xFF);
    byte one = ((value >> 8) & 0xFF);
    EEPROM.write(address, two);
    EEPROM.write(address + 1, one);
}
int16_t EEPROMReadInt16(long address)
{
    uint16_t two = EEPROM.read(address);
    uint16_t one = EEPROM.read(address + 1);
    return ((two << 0) & 0xFF) + ((one << 8) & 0xFFFF);
}

void draw_pulse_once(void)
{
    u8g.drawStr(0, 20, "MODE: Single");
    u8g.drawStr(0, 40, "Pulse1: ");
    u8g.setPrintPos(60, 40);
    u8g.print(pulse1);
}

void draw_pulse_twice(void)
{
    u8g.drawStr(0, 20, "MODE: Double");
    u8g.drawStr(0, 40, "p1 / idle / p2");
    u8g.setPrintPos(0, 60);
    u8g.print(pulse1);
    u8g.setPrintPos(50, 60);
    u8g.print(idle);
    u8g.setPrintPos(100, 60);
    u8g.print(pulse2);
}

void draw_pulse_continous(void)
{
    u8g.drawStr(0, 20, "MODE: Continous");
    u8g.drawStr(0, 40, "Pulse1: ");
    u8g.setPrintPos(60, 40);
    u8g.print(pulse1);
    u8g.drawStr(0, 60, "Idle: ");
    u8g.setPrintPos(60, 60);
    u8g.print(idle);
}

void draw_edit_pulse1(void)
{
    u8g.drawStr(0, 20, "EDIT");
    u8g.drawStr(0, 40, "Pulse1: ");
    u8g.setPrintPos(60, 40);
    u8g.print(pulse1);
}
void draw_edit_pulse2(void)
{
    u8g.drawStr(0, 20, "EDIT");
    u8g.drawStr(0, 40, "Pulse2: ");
    u8g.setPrintPos(60, 40);
    u8g.print(pulse2);
}
void draw_edit_idle(void)
{
    u8g.drawStr(0, 20, "EDIT");
    u8g.drawStr(0, 40, "Idle: ");
    u8g.setPrintPos(60, 40);
    u8g.print(idle);
}

void draw_pulsing(void)
{
    // invert by first drawing a box and then changing to black pixels
    u8g.drawBox(0, 0, 128, 64);
    u8g.setColorIndex(0);
    if (viewstate == VIEW_ONCE)
    {
        u8g.drawStr(20, 30, "PULSING ONCE");
    }
    if (viewstate == VIEW_TWICE)
    {
        u8g.drawStr(20, 30, "PULSING TWICE");
    }
    if (viewstate == VIEW_CONTINOUS)
    {
        u8g.drawStr(20, 30, "PULSING");
    }
    if (state == PULSE_ONE)
    {
        u8g.drawStr(20, 60, "ONE");
    }
    if (state == PULSE_IDLE)
    {
        u8g.drawStr(20, 60, "IDLE");
    }
    if (state == PULSE_TWO)
    {
        u8g.drawStr(20, 60, "TWO");
    }
    u8g.setColorIndex(1);
}

void draw(void)
{
    u8g.setFont(u8g_font_unifont);

    // views
    if (state == VIEW_ONCE)
    {
        draw_pulse_once();
    }
    if (state == VIEW_TWICE)
    {
        draw_pulse_twice();
    }
    if (state == VIEW_CONTINOUS)
    {
        draw_pulse_continous();
    }

    // edits
    if (state == EDIT_PULSE1)
    {
        draw_edit_pulse1();
    }
    if (state == EDIT_PULSE2)
    {
        draw_edit_pulse2();
    }
    if (state == EDIT_IDLE)
    {
        draw_edit_idle();
    }

    // doing the pulses
    if (state == PULSE_ONE)
    {
        draw_pulsing();
    }
    if (state == PULSE_IDLE)
    {
        draw_pulsing();
    }
    if (state == PULSE_TWO)
    {
        draw_pulsing();
    }
}

void setup()
{
    Serial.begin(9600);

    // setup timer for encoder
    Timer1.initialize(1000); // in us => 1000hz
    Timer1.attachInterrupt(timerIsr);

    // much nicer to use :)
    encoder.setAccelerationEnabled(true);

    // init
    oldEncPos = -1;

    pulse1 = EEPROMReadInt16(P1ADDR);
    if (pulse1 < MIN_LENGTH)
    {
        pulse1 = 50;
        EEPROMWriteInt16(P1ADDR, pulse1);
    }
    pulse2 = EEPROMReadInt16(P2ADDR);
    if (pulse2 < MIN_LENGTH)
    {
        pulse2 = 200;
        EEPROMWriteInt16(P2ADDR, pulse2);
    }
    idle = EEPROMReadInt16(IDLEADDR);
    if (idle < MIN_LENGTH)
    {
        idle = 50;
        EEPROMWriteInt16(IDLEADDR, idle);
    }

    btn_held_handled = 0;
    state = VIEW_ONCE;
    viewstate = VIEW_ONCE;

    pinMode(pinExtraSw, INPUT_PULLUP);
    pinMode(OUTPIN, OUTPUT);
}

// handle encoder rotating
void handle_encoder_rotate(void)
{
    if (state == EDIT_PULSE1)
    {
        encPos = pulse1;
    }
    if (state == EDIT_PULSE2)
    {
        encPos = pulse2;
    }
    if (state == EDIT_IDLE)
    {
        encPos = idle;
    }
    encPos += encoder.getValue();
    // limits
    if (encPos <= MIN_LENGTH)
    {
        encPos = MIN_LENGTH;
    }
    if (encPos >= 65535)
    {
        encPos = 65535;
    }
    if (encPos != oldEncPos)
    {
        oldEncPos = encPos;
    }
    if (state == EDIT_PULSE1)
    {
        pulse1 = encPos;
    }
    if (state == EDIT_PULSE2)
    {
        pulse2 = encPos;
    }
    if (state == EDIT_IDLE)
    {
        idle = encPos;
    }
}

void handle_encoder_button(void)
{
    // handle button presses
    buttonState = encoder.getButton();
    if (buttonState != 0)
    {
        switch (buttonState)
        {
        case ClickEncoder::Open: //0
            break;

        case ClickEncoder::Closed: //1
            break;

        case ClickEncoder::Pressed: //2
            break;

        case ClickEncoder::Held: //3
            // rotate between once, twice and continous
            if (!btn_held_handled and not pulsing())
            {
                // from once to twice
                if (state == VIEW_ONCE)
                {
                    state = VIEW_TWICE;
                    viewstate = VIEW_TWICE;
                }
                // from twice to cont
                else if (state == VIEW_TWICE)
                {
                    state = VIEW_CONTINOUS;
                    viewstate = VIEW_CONTINOUS;
                }
                // from cont to once
                else if (state == VIEW_CONTINOUS)
                {
                    state = VIEW_ONCE;
                    viewstate = VIEW_ONCE;
                }
                // wait for the next time
                btn_held_handled = 1;
            }
            break;

        case ClickEncoder::Released: //4
            // reset handled state and wait for next held
            btn_held_handled = 0;
            break;

        case ClickEncoder::Clicked: //5
            // the first pulsing state is always PULSE_ONE, depending on the
            // viewstate it then either advances to PULSE_IDLE or PULSE_TWICE or
            // back to viewstate

            // if we are not in pulsing or editing start pulsing
            if (not pulsing() and not editing())
            {
                // reset our counters
                pulse1_counter = 0;
                pulse2_counter = 0;
                idle_counter = 0;
                // and start pulsing
                state = PULSE_ONE;
            }
            // if we are pulsing and viewstate is continous go back to view and
            // end pulsing
            else if (pulsing() and viewstate == VIEW_CONTINOUS)
            {
                // end our pulsing and go back to view
                state = VIEW_CONTINOUS;
            }

            // otherwise do nothing

            break;

        case ClickEncoder::DoubleClicked: //6
            // go to edit mode and back with double click
            if (not pulsing())
            {
                switch (state)
                {
                    // default case is to go to edit pulse 1
                case VIEW_ONCE:
                    state = EDIT_PULSE1;
                    break;
                case VIEW_TWICE:
                    state = EDIT_PULSE1;
                    break;
                case VIEW_CONTINOUS:
                    state = EDIT_PULSE1;
                    break;

                case EDIT_PULSE1:
                    // depending on the view case
                    if (viewstate == VIEW_ONCE)
                    {
                        // save the value
                        EEPROMWriteInt16(P1ADDR, pulse1);
                        // back to view
                        state = VIEW_ONCE;
                    }
                    if (viewstate == VIEW_TWICE)
                    {
                        state = EDIT_IDLE;
                    }
                    if (viewstate == VIEW_CONTINOUS)
                    {
                        state = EDIT_IDLE;
                    }
                    break;
                case EDIT_IDLE:
                    // depending on the view case
                    if (viewstate == VIEW_TWICE)
                    {
                        state = EDIT_PULSE2;
                    }
                    if (viewstate == VIEW_CONTINOUS)
                    {
                        // need to save both, they both might have changed
                        EEPROMWriteInt16(P1ADDR, pulse1);
                        EEPROMWriteInt16(IDLEADDR, idle);
                        state = VIEW_CONTINOUS;
                    }
                    break;
                case EDIT_PULSE2:
                    // only way we got here was from VIEW_TWICE
                    // need to save both, they both might have changed
                    EEPROMWriteInt16(P1ADDR, pulse1);
                    EEPROMWriteInt16(P2ADDR, pulse2);
                    EEPROMWriteInt16(IDLEADDR, idle);
                    state = VIEW_TWICE;
                    break;
                case PULSE_ONE:
                    break;
                case PULSE_TWO:
                    break;
                case PULSE_IDLE:
                    break;
                }
            }
            break;
        }
    }
}

void loop()
{

    handle_encoder_rotate();
    handle_encoder_button();
    handle_extra_button();

    // update our display
    u8g.firstPage();
    do
    {
        draw();
    } while (u8g.nextPage());
}
