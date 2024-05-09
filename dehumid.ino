// AC Timer sketch
// https://github.com/JChristensen/dehumid
// Copyright (C) 2023-2024 by Jack Christensen and licensed under
// GNU GPL v3.0, https://www.gnu.org/licenses/gpl.html

#include <JC_Button.h>      // https://github.com/JChristensen/JC_Button
#include <MCP79412RTC.h>    // https://github.com/JChristensen/MCP79412RTC
#include <MCP9800.h>        // https://github.com/JChristensen/MCP9800
#include <movingAvg.h>      // https://github.com/JChristensen/movingAvg
#include <Streaming.h>      // https://github.com/janelia-arduino/Streaming
#include <TimeLib.h>        // https://github.com/PaulStoffregen/Time
#include <Timezone.h>       // https://github.com/JChristensen/Timezone
#include <Wire.h>           // https://arduino.cc/en/Reference/Wire
#include "Classes.h"

// pin definitions
constexpr uint8_t
    rtcInterrupt {2},
    ledIndicator {3},       // timer output indicator, same as ssr control output
    ledManual    {4},       // manual mode indicator
    ledHB        {13},      // heartbeat led
    btn1         {A2},      // override/manual button (not yet implemented)
    ssr          {A3};      // output to ssr control

constexpr uint8_t unusedPins[] {5, 6, 7, 8, 9, 10, 11, 12, A0, A1}; // 0 and 1 used for serial rx/tx

// schedules must be sorted earliest to latest, else undefined behavior!
Sched sched[] {{1400, 0}, {1900, 1}};

// object instantiations
void timerCallback(bool state);     // function prototype
Timer timer(sched, sizeof(sched) / sizeof(sched[0]), timerCallback);
MCP79412RTC myRTC;
Button btnOverride(btn1);
HeartbeatLED hb(ledHB);
MCP9800 tempSensor(0);
movingAvg avgTemp(6);

// time zone
TimeChangeRule EDT {"EDT", Second, Sun, Mar, 2, -240};  // Daylight time = UTC - 4 hours
TimeChangeRule EST {"EST", First,  Sun, Nov, 2, -300};  // Standard time = UTC - 5 hours
Timezone Eastern(EDT, EST);
TimeChangeRule *tcr;        // pointer to the time change rule, use to get TZ abbrev

// global variables
bool hasTempSensor;

void setup()
{
    Serial.begin(115200);
    Serial << F("\nhttps://github.com/JChristensen/dehumid");
    Serial << F( "\n" __FILE__ "\nCompiled " __DATE__ " " __TIME__ "\n" );
    pinMode(rtcInterrupt, INPUT_PULLUP);
    pinMode(ssr, OUTPUT);
    pinMode(ledIndicator, OUTPUT);
    pinMode(ledManual, OUTPUT);

    // enable pullups on unused pins for noise immunity
    for (uint8_t i=0; i<sizeof(unusedPins)/sizeof(unusedPins[0]); i++) {
        pinMode(unusedPins[i], INPUT_PULLUP);
    }

    // start the rtc if it's not running. this enables the square wave
    // output used for timekeeping interrupts.
    myRTC.begin();
    if (!myRTC.isRunning()) {
        Serial << F("RTC is not running, starting it now. Time will be incorrect!\n");
        myRTC.set(myRTC.get());
    }

    attachInterrupt(digitalPinToInterrupt(rtcInterrupt), incrementTime, FALLING);
    myRTC.squareWave(MCP79412RTC::SQWAVE_1_HZ);
    btnOverride.begin();
    avgTemp.begin();
    hb.begin();

    // check for temperature sensor
    Wire.beginTransmission(MCP9800_BASE_ADDR);
    hasTempSensor = (Wire.endTransmission() == 0);
    if (hasTempSensor) {   // take an initial reading
        avgTemp.reading( tempSensor.readTempF10(AMBIENT) );
        Serial << F("Temperature sensor found\n");
    }
    else {
        Serial << F("Temperature sensor not found\n");
    }

    // print rtc id
    uint8_t rtcID[8];
    myRTC.idRead(rtcID);
    Serial << F("RTC ID: ");
    for (int i=0; i<8; ++i) {
        if (rtcID[i] < 16) Serial << '0';
        Serial << _HEX(rtcID[i]);
    }
    Serial << endl;

    // check for rtc eeprom signature indicating calibration value present
    if (myRTC.eepromRead(125) == 0xAA && myRTC.eepromRead(126) == 0x55) {
        // get the calibration value
        int8_t calibValue = static_cast<int8_t>(myRTC.eepromRead(127));
        myRTC.calibWrite(calibValue);   // set calibration register
        Serial << F("RTC calibrated from EEPROM: ") << calibValue << endl;
    }

    time_t utc = getUTC();                  // synchronize with RTC
    while ( utc == getUTC() );              // wait for increment to the next second
    utc = myRTC.get();                      // get the time from the RTC
    setUTC(utc);                            // set our time to the RTC's time
    Serial << "Time set from RTC\n";
    timer.printSchedules();
}
void loop()
{
    time_t t = getUTC();
    time_t local = Eastern.toLocal(t, &tcr);

    // if temperature sensor present, read every 10 seconds.
    if (hasTempSensor) {
        static int secLast {99};
        int secNow = second(t);
        if (secNow != secLast) {
            secLast = secNow;
            if (secNow % 10 == 0) {
                avgTemp.reading( tempSensor.readTempF10(AMBIENT) );
            }
        }
    }

    // check the timer once per minute
    static int minLast {99};
    int minNow = minute(t);
    if (minNow != minLast) {
        minLast = minNow;
        printDateTime(local, tcr->abbrev, hasTempSensor);
        timer.run(local);
    }

    // check for manual override
    btnOverride.read();
    if (btnOverride.wasReleased()) {
        timer.toggle();
    }
    // check for mode change
    else if (btnOverride.pressedFor(1000)) {
        // toggle manual/automatic mode and set the LED accordingly
        digitalWrite(ledManual, timer.toggleMode());
        // wait for button to be released
        while (btnOverride.isPressed()) btnOverride.read();
        // apply the current schedule if auto mode
        printDateTime(local, tcr->abbrev, hasTempSensor);
        timer.run(local);
    }

    // check for input, to set rtc time or calibration
    if (Serial.available()) setRTC();

    hb.run();   // run the heartbeat led
}

void timerCallback(bool state)
{
    digitalWrite(ledIndicator, state);
    digitalWrite(ssr, state);
}

// functions to manage the RTC time and the 1Hz interrupt.
volatile time_t isrUTC;         // ISR's copy of current time in UTC

// return current time
time_t getUTC()
{
    noInterrupts();
    time_t utc = isrUTC;
    interrupts();
    return utc;
}

// set the current time
void setUTC(time_t utc)
{
    noInterrupts();
    isrUTC = utc;
    interrupts();
}

// 1Hz RTC interrupt handler increments the current time
void incrementTime()
{
    ++isrUTC;
}

// format and print a time_t value, with a time zone appended.
void printDateTime(time_t t, const char *tz, bool hasTempSensor)
{
    char buf[32];
    sprintf(buf, "%.2d:%.2d:%.2d %.4d-%.2d-%.2d %s",
        hour(t), minute(t), second(t), year(t), month(t), day(t), tz);
    Serial.print(buf);
    if (hasTempSensor) {
        Serial << ' ' << _FLOAT(avgTemp.getAvg() / 10.0, 1) << F("°F");
    }
}

void setRTC()
{
    // first character is a command, "S" to set date/time, or "C" to set the calibration register
    int cmdChar = Serial.read();

    switch (cmdChar) {
        case 'S':
        case 's':
            delay(25);  // wait for all the input to arrive
            // check for input to set the RTC, minimum length is 13, i.e. yy,m,d,h,m,s<nl>
            if (Serial.available() < 13) {
                while (Serial.available()) Serial.read();  // dump extraneous input
                Serial << F("Input error or timeout, try again.\n");
            }
            else {
                // note that the tmElements_t Year member is an offset from 1970,
                // but the RTC wants the last two digits of the calendar year.
                // use the convenience macros from TimeLib.h to do the conversions.
                int y = Serial.parseInt();
                if (y >= 100 && y < 1000)
                    Serial << F("Error: Year must be two digits or four digits!\n");
                else {
                    tmElements_t tm;
                    if (y >= 1000)
                        tm.Year = CalendarYrToTm(y);
                    else    //(y < 100)
                        tm.Year = y2kYearToTm(y);
                    tm.Month = Serial.parseInt();
                    tm.Day = Serial.parseInt();
                    tm.Hour = Serial.parseInt();
                    tm.Minute = Serial.parseInt();
                    tm.Second = Serial.parseInt();
                    if (tm.Month == 0 || tm.Day == 0) {
                        while (Serial.available()) Serial.read();  // dump extraneous input
                        Serial << F("Input error or timeout, try again.\n");
                    }
                    else {
                        time_t t = makeTime(tm);
                        myRTC.set(t);               // use the time_t value to ensure correct weekday is set
                        time_t utc = myRTC.get();   // get the time from the RTC
                        setUTC(utc);                // set our time to the RTC's time
                        Serial << F("RTC set to UTC: ");
                        printTime(t);
                    }
                }
            }
            break;

        case 'C':
        case 'c':
            delay(25);  // wait for all the input to arrive
            if (Serial.available() < 2) {   // minimum valid input at this point is 2 chars
                while (Serial.available()) Serial.read();  // dump extraneous input
                Serial << F("Input error or timeout, try again.\n");
            }
            else {
                int newCal = Serial.parseInt();
                int oldCal = myRTC.calibRead();
                myRTC.calibWrite(newCal);
                Serial << F("Calibration changed from ") << oldCal << F(" to ") << myRTC.calibRead() << endl;
            }
            break;

        default:
            Serial << endl << F("Unrecognized command: ") << (char)cmdChar << endl;
            break;
    }

    // dump any extraneous input
    while (Serial.available()) Serial.read();
}

// format and print a time_t value
void printTime(const time_t t)
{
    char buf[25];
    char m[4];    // temporary storage for month string (DateStrings.cpp uses shared buffer)
    strcpy(m, monthShortStr(month(t)));
    sprintf(buf, "%.2d:%.2d:%.2d %s %.2d %s %d",
        hour(t), minute(t), second(t), dayShortStr(weekday(t)), day(t), m, year(t));
    Serial.println(buf);
}
