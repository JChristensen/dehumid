#include <TimeLib.h>        // https://github.com/PaulStoffregen/Time

// Sched object used by the Timer class.
// The timer class wants a pointer to an array of Sched objects which define
// daily on and off times.
// The array of Sched objects must be in ascending order by time, else
// undefined behavior will result.
// schedTime is an integer of the form hhmm where hh is 0-23 and mm is 0-59.
// No error checking is done. Invalid values of time will result in undefined behavior.
// schedState defines the timer output at the corresponding time,
// true (or 1) for on, false (or 0) for off.
struct Sched
{
    int schedTime;
    bool schedState;
};

// A timer that uses a fixed daily schedule, as defined by an array of
// Sched objects (see above.)
// The timer will call a callback function whenever a new schedule time
// takes effect, and provide the callback with the current output value.
// The run() method should ideally be called once per minute.
class Timer
{
    public:
        Timer(Sched* sched, int nsched, void (*fcn)(bool))
            : m_sched{sched}, m_nsched{nsched}, timerCallback{fcn} {}
        bool run(time_t t);
        bool toggle();
        void printSchedules();

    private:
        Sched* m_sched;                 // pointer to the schedule array
        int m_nsched;                   // number of schedules
        int m_curSched {-1};            // index to the current schedule
                                        // initial value of -1 ensures the callback is made
                                        // on the first call to run()
        bool m_state;                   // the current state
        void (*timerCallback)(bool);    // the caller's callback function
        // convert a time_t value to an integer of the form hhmm for easy comparisons
        int convertTime(time_t t) {return hour(t)*100 + minute(t);}
};

// check the current time against the timer schedules to determine
// which schedule is in effect. call the callback function when a
// new schedule takes effect. returns the current output state.
bool Timer::run(time_t epoch)
{
    int curSched;
    int t = convertTime(epoch);

    // if current time is less than the earliest schedule, or greater than or equal to
    // the last schedule, then the last schedule is in effect.
    if (t < m_sched->schedTime || t >= (m_sched+m_nsched-1)->schedTime) {
        curSched = m_nsched - 1;
    }
    // else, step through the schedules in reverse order, starting with the
    // next-to-last schedule, to find which schedule is in effect.
    else {
        for (int s=m_nsched-2; s>=0; --s) {
            if (t >= (m_sched+s)->schedTime) {
                curSched = s;
                break;
            }
        }
    }
    Serial << F(" Current schedule ") << (m_sched+curSched)->schedTime << ' ' << (m_sched+curSched)->schedState << endl;

    // do the callback only if the active schedule has changed since the last call to check().
    if (curSched != m_curSched) {
        m_curSched = curSched;
        m_state = (m_sched+m_curSched)->schedState;
        Serial << F("Sending callback: ") << m_state << endl;
        timerCallback(m_state);
    }
    return m_state;
}

// override the current output state by toggling it.
// calls the callback function and returns the new output state.
bool Timer::toggle()
{
    m_state = !m_state;
    timerCallback(m_state);
    Serial << F("Override: ") << m_state << endl;
    return m_state;
}

// print the schedules.
void Timer::printSchedules()
{
    Serial << "There are " << m_nsched << " schedules\n";
    for (int i=0; i<m_nsched; i++) {
        Serial << (m_sched+i)->schedTime << ' ' << (m_sched+i)->schedState << endl;
    }    
}

// ---- Heartbeat LED class ----
// Very simple, just a blinking LED.
class HeartbeatLED
{
    public:
        HeartbeatLED(uint8_t pin, uint32_t interval)
            : m_pin{pin}, m_interval{interval} {}
        void begin();
        void run();

    private:
        uint8_t m_pin;
        uint32_t m_interval;
        uint32_t m_lastChange;
        bool m_state{true};
};

void HeartbeatLED::begin()
{
    pinMode(m_pin, OUTPUT);
    digitalWrite(m_pin, m_state);
    m_lastChange = millis();
}

void HeartbeatLED::run()
{
    if (millis() - m_lastChange >= m_interval) {
        m_lastChange += m_interval;
        digitalWrite(m_pin, m_state = !m_state);
    }
}
