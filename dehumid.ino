// AC Daily Timer
// Sketch to control a 120VAC appliance via a solid-state relay, according to a daily schedule.
// An override button can be used to change the output state.
// There are two modes, automatic, where the schedule is in effect, and manual,
// where the output state is controlled only by the override button.
// To change between modes, press the button and hold for one second.
// When changing to manual mode, the output will be initially turned off. When changing
// to automatic mode, the current schedule will determine the output state.
// For time keeping, an MCP7941x RTC is used. The RTC can be calibrated automatically
// during setup from a value stored in its EEPROM. A calibration value is assumed to
// be present at address 0x7F if addresses 0x7D and 0x7E contain 0xAA and 0x55 respectively.
// An MCP9802 temperature sensor can optionally be present on the I2C bus. The code will
// automatically detect whether it is installed, and, if so, will report temperature.
// 
// J.Christensen 06Jun2023
// Thanks to Tom Hagen for design input and for testing.

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
    Serial << F( "\n" __FILE__ "\nCompiled " __DATE__ " " __TIME__ "\n" );
    pinMode(rtcInterrupt, INPUT_PULLUP);
    pinMode(ssr, OUTPUT);
    pinMode(ledIndicator, OUTPUT);
    pinMode(ledManual, OUTPUT);

    // enable pullups on unused pins for noise immunity
    for (uint8_t i=0; i<sizeof(unusedPins)/sizeof(unusedPins[0]); i++) {
        pinMode(unusedPins[i], INPUT_PULLUP);
    }
    
    attachInterrupt(digitalPinToInterrupt(rtcInterrupt), incrementTime, FALLING);
    myRTC.begin();
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
