// dehumidifier controller
// turn off power to the dehumidifier during peak rate period.
// J.Christensen 06Jun2023

#include <JC_Button.h>      // https://github.com/JChristensen/JC_Button
#include <MCP79412RTC.h>    // https://github.com/JChristensen/MCP79412RTC
#include <Streaming.h>      // https://github.com/janelia-arduino/Streaming
#include <TimeLib.h>        // https://github.com/PaulStoffregen/Time
#include <Timezone.h>       // https://github.com/JChristensen/Timezone
#include <Wire.h>           // https://arduino.cc/en/Reference/Wire
#include "Classes.h"

// pin definitions
constexpr uint8_t
    rtcInterrupt {2},
    led1 {6},               // timer output
    led2 {7},               // rfu
    btn1 {8};               // rfu

void timerCallback(bool state);

// schedules must be sorted earliest to latest, else undefined behavior!
Sched sched[] {{1400, 0}, {1900, 1}};
Timer timer(sched, sizeof(sched) / sizeof(sched[0]), timerCallback);

// object instantiations
MCP79412RTC myRTC;
Button override(btn1);

// time zone
TimeChangeRule EDT {"EDT", Second, Sun, Mar, 2, -240};  // Daylight time = UTC - 4 hours
TimeChangeRule EST {"EST", First,  Sun, Nov, 2, -300};  // Standard time = UTC - 5 hours
Timezone Eastern(EDT, EST);
TimeChangeRule *tcr;        // pointer to the time change rule, use to get TZ abbrev
    
void setup()
{
    Serial.begin(115200);
    Serial << F( "\n" __FILE__ "\nCompiled " __DATE__ " " __TIME__ "\n" );
    pinMode(rtcInterrupt, INPUT_PULLUP);
    pinMode(led1, OUTPUT);
    pinMode(led2, OUTPUT);
    override.begin();
    attachInterrupt(digitalPinToInterrupt(rtcInterrupt), incrementTime, FALLING);
    myRTC.begin();
    myRTC.squareWave(MCP79412RTC::SQWAVE_1_HZ);

    time_t utc = getUTC();                  // synchronize with RTC
    while ( utc == getUTC() );              // wait for increment to the next second
    utc = myRTC.get();                      // get the time from the RTC
    setUTC(utc);                            // set our time to the RTC's time
    Serial << "Time set from RTC\n";

    timer.printSchedules();
}
void loop()
{
    static int minLast {99};
    time_t t = getUTC();
    int minNow = minute(t);

    // check the timer once per minute
    if (minNow != minLast) {
        minLast = minNow;
        time_t local = Eastern.toLocal(t, &tcr);
        printDateTime(local, tcr->abbrev);
        timer.run(local);
    }
}

void timerCallback(bool state)
{
    digitalWrite(led1, state);
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
void printDateTime(time_t t, const char *tz)
{
    char buf[32];
    char m[4];    // temporary storage for month string (DateStrings.cpp uses shared buffer)
    strcpy(m, monthShortStr(month(t)));
    sprintf(buf, "%.2d:%.2d:%.2d %s %.2d %s %d %s",
        hour(t), minute(t), second(t), dayShortStr(weekday(t)), day(t), m, year(t), tz);
    Serial.print(buf);
}
