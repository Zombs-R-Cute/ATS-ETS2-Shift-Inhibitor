//
// ShiftingBlock.cpp(ino)
// Created by shauna on 3/30/26.
// Sample of the setup for ATS/ETS Shift inhibitor
//
#include <Arduino.h>
#include <EEPROM.h>
#define SERIAL_BAUDRATE 115200
#define RANGE_L_H 16
#define SPLIT_H_L 15
#define SHIFTER_VIBRATE 10
#define VIBRATE_AMOUNT 100
#define INHIBIT 3
#define PERMIT 2
#define AIN2 INHIBIT
#define AIN1 PERMIT


/**
 * Motor Truth Table
 * Ain1 0 0 1
 * Ain2 0 1 0
 *      | | \Forward - Allow
 *      | \Reverse - Block
 *      \Stop
 */


/**
 * The motor is a 6v 1,000 rpm, driven at 9v
 * these timings are what works for me.
 */
struct EepromSettings {
    uint16_t motorTimeoutInhibit = 0.008 * 62500 - 1; // 11ms @6v 1mm gear module.
    uint16_t vibrateAmount = 80;
    uint16_t motorTimeoutPermit = 0.004 * 62500 - 1;// 8ms @6v
} eeprom_settings;


bool hshifter;
bool grinding;
bool SwitchingPosition = false;
bool vibrateOn = false;
bool configuration = false;
bool inhibited = true;
uint16_t motorTimeout;


void OneShotTimer_3(uint16_t timeout) {
    OCR3A = timeout;
    TCNT3 = 0; //reset counter
    TCCR3A = 0;
    TIMSK3 = (1 << OCIE3A); //enable compare match interrupt
    TCCR3B = (1 << CS32) | (1 << WGM32); //256pre //ctc
}


ISR(TIMER3_COMPA_vect) {
    TCCR3B = 0; //stop timer
    SwitchingPosition = false;
    digitalWrite(INHIBIT, 0);
    digitalWrite(PERMIT, 0);
}


void InitPins() {
    pinMode(RANGE_L_H, INPUT_PULLUP);
    pinMode(SPLIT_H_L, INPUT_PULLUP);
    pinMode(SHIFTER_VIBRATE, OUTPUT);
    pinMode(INHIBIT, OUTPUT);
    pinMode(PERMIT, OUTPUT);
}


void EEPROM_Save() {
    EEPROM.put(0, eeprom_settings);
}


void EEPROM_Load() {
    EEPROM.get(0, eeprom_settings);
}


void MoveGate(byte direction) {
    SwitchingPosition = true;
    digitalWrite(direction, 1);
    inhibited = direction == INHIBIT;
    OneShotTimer_3(inhibited ? eeprom_settings.motorTimeoutInhibit : eeprom_settings.motorTimeoutPermit);
}


void PrintMotorTimeout() {
    Serial.print(F("\nMotor Inhibit Timeout: "));
    Serial.print(eeprom_settings.motorTimeoutInhibit);
    Serial.print(F(", "));
    Serial.print((eeprom_settings.motorTimeoutInhibit + 1) / 62500.0, 4);
    Serial.println(F("s"));

    Serial.print(F("\nMotor Permit Timeout: "));
    Serial.print(eeprom_settings.motorTimeoutPermit);
    Serial.print(F(", "));
    Serial.print((eeprom_settings.motorTimeoutPermit + 1) / 62500.0, 4);
    Serial.println(F("s"));
}

void PrintVibrateAmount() {
    Serial.print(F("\nVibrate Amount: "));
    Serial.println(eeprom_settings.vibrateAmount);
}
void setup() {
    delay(200);
    EEPROM_Load();
    delay(200);
    InitPins();
    motorTimeout = eeprom_settings.motorTimeoutPermit/2;

    for (int i = 0; i < 2; ++i) {
        MoveGate(PERMIT);
        delay(200);
    }
    Serial.begin(SERIAL_BAUDRATE);
    Serial.setTimeout(50);


    analogWrite(SHIFTER_VIBRATE, VIBRATE_AMOUNT);
    delay(500);
    analogWrite(SHIFTER_VIBRATE, 0);
    MoveGate(INHIBIT);
}

char buffer[10];

byte count = 0;

bool passingFloat = false;

bool passingInt = false;

float f = 0;

uint16_t i = 0;

char valueToUpdate;


void ProcessConfiguration(byte b) {
    if ((passingFloat || passingInt) && b != 0x0A) {
        buffer[count++] = b;
        if (count == 10) {
            Serial.println(F("Error, buffer exceeded!  Exiting Configuration"));
        }
        return;
    }

    if (b == 0x0A) {
        buffer[count] = 0;
        b = valueToUpdate;

        if (passingFloat) {
            f = atof(buffer);
        }

        if (passingInt) {
            i = atoi(buffer);
        }

        count = 0;
        passingFloat = false;
        passingInt = false;
    }

    valueToUpdate = b;
    switch (b) {
        case 'I': //motor
            if (f == 0) {
                passingFloat = true;
                break;
            }

        eeprom_settings.motorTimeoutInhibit = 62500 * f - 1;
        f = 0;
        PrintMotorTimeout();
        break;

        case 'M': //motor
            if (f == 0) {
                passingFloat = true;
                break;
            }

        eeprom_settings.motorTimeoutPermit = 62500 * f - 1;
        f = 0;
        PrintMotorTimeout();
        break;

        case 'V':
            if (i == 0) {
                passingInt = true;
                break;
            }
        eeprom_settings.vibrateAmount = i;
        i = 0;
        break;

        case 'S':
            Serial.println(F("Saved"));
        EEPROM_Save();
        break;

        case 'Q':
            Serial.println(F("Exit"));
        configuration = false;
        break;

        case 'P':
            PrintMotorTimeout();
        PrintVibrateAmount();
        break;

        case 'T':
            if (inhibited) {
                MoveGate(PERMIT);
                break;
            }
        MoveGate(INHIBIT);
        break;
    }
}


void ProcessSerial() {
    if (Serial.available()) {
        byte b = Serial.read();

        if (configuration) {
            ProcessConfiguration(b);
            return;
        }

        if (b == 'C') {
            Serial.println(F("\nConfiguration:"));

            configuration = true;
            memset(buffer, 0, sizeof(buffer));

            return;
        }

        if (b == 'G') {
            //The plugin has connected
            MoveGate(PERMIT);
            delay(200);
            MoveGate(INHIBIT);
            return;
        }

        grinding = (b >> 5) & 1; //PC is sending the change to grinding
        hshifter = b & 0x1F;
        analogWrite(SHIFTER_VIBRATE, eeprom_settings.vibrateAmount * grinding);
    }
}


void ProcessGrinding() {
    if (SwitchingPosition || configuration)
        return;

    if ((hshifter == 0) && !inhibited) {
        MoveGate(INHIBIT);
        return;
    }

    if (!grinding && inhibited && (hshifter != 0))
        MoveGate(PERMIT);
}


void loop() {
    ProcessSerial();
    ProcessGrinding();
}
