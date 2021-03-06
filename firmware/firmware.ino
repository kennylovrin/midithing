// Copyright 2014 Sergio Retamero.
//
// Author: Sergio Retamero (sergio.retamero@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//

//#define PRINTDEBUG 1// if defined, send debug info to the serial interface
//#define USETIMER  // if defined, use timer functions
#define CALIBRATION 1
#define STARTSTOPCONT // if defined, handle start/stop/continue

#include <Bounce2.h> // Button debouncer
#include <EEPROM.h>
#include <MIDI.h> // MIDI library
#include <Wire.h> //  I2C Comm
#include <mcp4728.h> // MCP4728 library
//#include "EEPROMAnything.h"
#include "Blinker.h"
#include "MIDIClass.h"
#include "MultiPointConv.h"
#include <avr/eeprom.h>

// Input / output definitions
// DAC Ports
#define PITCHCV 0
#define VELOC 1
#define MODUL 2
#define BEND 3
// In/Out pins
#define PINGATE 2
#define PINGATE2 3
#define PINGATE3 4
#define PINGATE4 5
#define PINCLOCK 6
#define PINLEARN 7
#define PINLED2 9
#define PINSTARTSTOP 10
#define PINLED 13

// Learn Mode
#define NORMALMODE 0
#define ENTERLEARN 1
#define ENTERCAL 2

// MIDI Modes
#define MONOMIDI 1
#define DUALMIDI 2
#define QUADMIDI 4
#define PERCTRIG 5
#define PERCGATE 6

//
#define TRIGPERCUSSION 60 // Width of trigger for percussions
#define TRIGCLOCK 60 // Width of trigger for Clock
#define TRIGSTART 60// Width of trigger for Start/Continue

//////////////////////////////////////////////
// Function declaration
// DAC
void  sendvaltoDAC(unsigned int port, unsigned int val);
//MIDI Handles
void HandleNoteOn(byte channel, byte pitch, byte velocity);
void HandleNoteOff(byte channel, byte pitch, byte velocity);
void HandlePitchBend(byte channel, int bend);
void HandleControlChange(byte channel, byte number, byte value);
#ifdef STARTSTOPCONT
void HandleStart(void);
void HandleContinue(void);
void HandleStop(void);
#endif
void HandleClock(void);
//MIDI
int CheckActiveMIDI(byte channel, byte pitch = 0);
void AllNotesOff(void);
int ReadMIDIeeprom(void);
void WriteMIDIeeprom(void);
void SetModeMIDI(int mode);
//void playNote(byte note, byte plvelocity);
//void playNoteOff(void);
// Learn mode
void DoLearnCycle(void);
void EnterLearnMode(void);
// Calibration mode
void DoCalCycle(void);
void EnterCalMode(void);
void EndCalMode(void);
byte CalProcessNote(byte channel, byte pitch, byte velocity);

// Timer functions
#ifdef USETIMER
void SetupTimer(unsigned int compTimer1);
#endif

//////////////////////////////////////////////
//Variables
//MIDI
MIDICV ChanMIDI[4]; // Define up to four MIDI channels
int MAXNumMIDI = 0; // Number of MIDI channels in use
byte MIDIRun = 0; // Set to 0 to init in stop condition
int MIDImode = QUADMIDI; // MIDI mode Set to quad mode by default
Blinker gates[10]; // Gates triggers
int countCLOCK = 1;
int ppqnCLOCK = 24;

// Var I2C DAC
mcp4728 dac = mcp4728(0); // instantiate mcp4728 object, Device ID = 0
MultiPointConv DACConv[4];

// Var Learn Mode
byte LearnMode = NORMALMODE;
unsigned long LearnInitTime;
byte LearnStep = 0;

// Var Blinker
Blinker blink((byte)PINLED);

Bounce bouncer = Bounce(); // will be configured in setup()
unsigned long bouncerLastTime = 0;

struct MIDISettings : public midi::DefaultSettings {
  static const bool UseRunningStatus = false;
  static const bool Use1ByteParsing = false;
};

MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI, MIDISettings);

//////////////////////////////////////////////
// Initialization
void setup()
{
  // Configure digital outputs
  pinMode(PINLED, OUTPUT);
  digitalWrite(PINLED, LOW); // Set internal pull down resistor
  pinMode(PINLED2, OUTPUT);
  digitalWrite(PINLED2, LOW); // Set internal pull down resistor
  pinMode(PINGATE, OUTPUT);
  digitalWrite(PINGATE, LOW); // Set internal pull down resistor
  pinMode(PINGATE2, OUTPUT);
  digitalWrite(PINGATE2, LOW); // Set internal pull down resistor
  pinMode(PINGATE3, OUTPUT);
  digitalWrite(PINGATE3, LOW); // Set internal pull down resistor
  pinMode(PINGATE4, OUTPUT);
  digitalWrite(PINGATE4, LOW); // Set internal pull down resistor
  pinMode(PINSTARTSTOP, OUTPUT);
  digitalWrite(PINSTARTSTOP, LOW); // Set internal pull down resistor
  pinMode(PINCLOCK, OUTPUT);
  digitalWrite(PINCLOCK, LOW); // Set internal pull down resistor

  pinMode(PINLEARN, INPUT); // maybe INPUT_PULLUP?
  // Attach the Bounce object with a 50 millisecond debounce time
  bouncer.attach(PINLEARN);
  bouncer.interval(50);
  //digitalWrite(PINLEARN, LOW); // Set internal pull down resistor

  // DAC MCP4728 init
  dac.begin();  // initialize i2c interface
  dac.vdd(5000); // set VDD(mV) of MCP4728 for correct conversion between LSB and Vout
  dac.setVref(1, 1, 1, 1); // Use internal vref
  dac.setGain(1, 1, 1, 1); // Use gain x2
  for (int i = 0; i < 4; i++) {
    DACConv[i].DACnum = i;
  }

  // Init triggers gates
  gates[0].pinLED = PITCHCV + 128;
  gates[1].pinLED = VELOC + 128;
  gates[2].pinLED = MODUL + 128;
  gates[3].pinLED = BEND + 128;
  gates[4].pinLED = PINCLOCK;
  gates[5].pinLED = PINGATE;
  gates[6].pinLED = PINGATE2;
  gates[7].pinLED = PINGATE3;
  gates[8].pinLED = PINGATE4;
  gates[9].pinLED = PINSTARTSTOP;

  //  Init MIDI:
  //Serial.begin(115200);
  MIDI.turnThruOff(); // Thru off disconnected
  MIDI.setHandleNoteOn(HandleNoteOn); // Handle for NoteOn
  MIDI.setHandleNoteOff(HandleNoteOff); // Handle for NoteOff
  MIDI.setHandlePitchBend(HandlePitchBend); // Handle for Pitch Bend
  MIDI.setHandleControlChange(HandleControlChange); // Handle for CC
#ifdef STARTSTOPCONT
  MIDI.setHandleStart(HandleStart); // Handle Start, Continue, Stop
  MIDI.setHandleContinue(HandleContinue);
  MIDI.setHandleStop(HandleStop);
#endif
  MIDI.setHandleClock(HandleClock);
  MIDI.begin(MIDI_CHANNEL_OMNI); // Listen to all channels

#ifdef PRINTDEBUG
  Serial.println("Init MIDItoCV...");
#endif

  // Read MIDI Channels from EEPROM and store
  if (ReadMIDIeeprom() == -1) {
#ifdef PRINTDEBUG
    Serial.println("No EEPROM Read");
#endif
    // Set Mode manually
    //SetModeMIDI(MONOMIDI);
    //SetModeMIDI(DUALMIDI);
    SetModeMIDI(QUADMIDI);
    //SetModeMIDI(PERCTRIG);
  }

  // LDAC pin must be grounded for normal operation.
  // Reset DAC values to 0 after reset
  delay(50);
  dac.analogWrite(0, 0, 0, 0);

#ifdef CALIBRATION
  analogReference(INTERNAL);
#endif
}

//////////////////////////////////////////////
// Main Loop
void loop()
{
  // put your main code here, to run repeatedly:

  MIDI.read();

  // handle blinks
  blink.playBlink();
  for (int i = 0; i < 10; i++) {
    gates[i].playBlink();
  }

  // Cal/Learn Button
  if (bouncer.update()) { // button state changed
    unsigned long now = millis();
    unsigned long bouncerDuration = now - bouncerLastTime;

    bouncerLastTime = now;

    // Check for learn/cal mode signal
    if (bouncer.fell()) {
      // If button pressed during calbration, end calibration
      if (LearnMode == ENTERCAL) {
        EndCalMode();
      }
      // Enter Calmode after 5 secs button press
      else if (bouncerDuration > 5000) {
        EnterCalMode();
      }
      // More than one second: Learn Mode
      else if (bouncerDuration > 1000) {
        EnterLearnMode();
      }
      // Panic
      else if (bouncerDuration > 100) {
        AllNotesOff();
      }
    }
  } else { // no change to the button state
    // In Learn mode, enter learn cycle
    if (LearnMode == ENTERLEARN) {
      DoLearnCycle();
    } else if (LearnMode == ENTERCAL) {
      DoCalCycle();
    }
  }
}

//////////////////////////////////////////////
// DAC function definition
// Send value val to DAC port
void  sendvaltoDAC(unsigned int port, unsigned int val)
{
  dac.analogWrite(port, val); // write to input register of a DAC. Channel 0-3, Value 0-4095

#ifdef PRINTDEBUG
  Serial.print(port);
  Serial.print(" DAC = ");
  Serial.println(val);
#endif
}
