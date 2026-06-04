// serial / button debugging snippet for stepper motors
// using step/direction interface of common stepper drivers

#define DISTANCE 3200
#define SPEED_SLOW 1000
#define SPEED_FAST 50

//#define DEBUG 1
#define SIG_REVERSE 1

#ifdef SIG_REVERSE
#define SIG_ON LOW
#define SIG_OFF HIGH
#else
#define SIG_ON HIGH
#define SIG_OFF LOW
#endif

#define P_STEP 9
#define P_DIR 8
#define P_DIR_LEFT SIG_ON
#define P_DIR_RIGHT SIG_OFF

#define P_IN_LEFT_FAST 4
#define P_IN_LEFT_SLOW 5
#define P_IN_RIGHT_SLOW 6
#define P_IN_RIGHT_FAST 7

enum InputCommands {
  CMD_NONE = 0,
  CMD_LEFT_SLOW,
  CMD_LEFT_FAST,
  CMD_RIGHT_SLOW,
  CMD_RIGHT_FAST
};

#include <Arduino.h>

// debugging print helper for serial supporting
//   debugPrint(10, "foo", "bar", 20, <and so on>);
inline void debugPrintln() {
  Serial.println();
}

template <typename T>
inline void debugPrintOne(const T& value) {
  Serial.print(value);
}

template <typename T, typename... Ts>
inline void debugPrintMany(const T& first, const Ts&... rest) {
  Serial.print(first);
  debugPrintMany(rest...);
}

// Overload to terminate recursion
inline void debugPrintMany() {
  // nothing
}

// Public API: print without newline
template <typename... Ts>
inline void debugPrint(const Ts&... args) {
  debugPrintMany(args...);
}

// Public API: print with newline
template <typename... Ts>
inline void debugPrintln(const Ts&... args) {
  debugPrintMany(args...);
  Serial.println();
}

#ifdef DEBUG
  #define debugPrint(...)       debugPrint(__VA_ARGS__)
  #define debugPrintln(...)     debugPrintln(__VA_ARGS__)
  #define debugPrintCmd(...)     do {                     \
                                    debugPrint((int)cmd, " ", __VA_ARGS__); \
                                  } while (0)
#else
  #define debugPrint(...)       do { } while (0)
  #define debugPrintln(...)     do { } while (0)
  #define debugPrintCmd(...)    do { } while (0)
#endif

// Helper to ensure a minimal delay with optional time underrun detection when compiled with DEBUG
struct EnsureDelay {
  unsigned long _expected_endtime;
  EnsureDelay() { _expected_endtime = 0; }
  void EnsureDelay::start(int delay_us) { _expected_endtime = micros() + delay_us; }
  void EnsureDelay::delay() {
    unsigned long now = micros();
    if(_expected_endtime == 0) {
      debugPrintln("ERROR: uninitialized EnsureDelay %s:%d", __FILE__, __LINE__);
    }
    int timediff = now - _expected_endtime;
    if(timediff >= 0) {
      delayMicroseconds(timediff);
      _expected_endtime = 0;
    } else {
      debugPrintln("time deadline not met, expected=%d, now=%d, diff=%d us", _expected_endtime, now, timediff);
    }
  }
};

// global state variable to initialize one count of DISTANCE steps being executed
//  this switches off ui / serial input completely to have jitter free steps
int Stepping = false;
// used internal in step function to count number of steps up to DISTANCE steps
int StepCounter = 0;
// delay used in delayMicroseconds for either fast or slow stepper speed
//  set from ui, read by stepping code
auto ensure_step_end_delay = EnsureDelay::EnsureDelay();
int Delay = SPEED_SLOW;
// ui state variable what input cmd was given by user
//  not strictly needed to be global, just in case and for debugging
char cmd = CMD_NONE;


void setup() {
  Serial.begin(9600);

  pinMode(P_DIR, OUTPUT);
  pinMode(P_STEP, OUTPUT);
  digitalWrite(P_DIR, SIG_OFF);
  digitalWrite(P_STEP, SIG_OFF);

  pinMode(P_IN_LEFT_SLOW, INPUT);
  pinMode(P_IN_LEFT_FAST, INPUT);
  pinMode(P_IN_RIGHT_SLOW, INPUT);
  pinMode(P_IN_RIGHT_FAST, INPUT);
}


void loop() {
  if (Stepping) {
    // do the steps
    ensure_step_end_delay.delay();
    digitalWrite(P_STEP, SIG_ON);
    delayMicroseconds(Delay);
    digitalWrite(P_STEP, SIG_OFF);
    // optimize end delay and "fold" code coming afterwards into this delay
    //  make sure rest of delay is properly executed at the start of the NEXT loop(), see above.
    ensure_step_end_delay.start(Delay);
    //delayMicroseconds(Delay);

    StepCounter = StepCounter + 1;

    if (StepCounter == DISTANCE)
    {
      StepCounter = 0;
      Stepping = false;
      cmd = CMD_NONE;
      Serial.println("done");
    }
  } else {
    // read input commands from buttons or serial
    if(digitalRead(P_IN_LEFT_FAST) == HIGH) { cmd = CMD_LEFT_FAST; debugPrintCmd("read left fast"); }
    else if(digitalRead(P_IN_LEFT_SLOW) == HIGH) { cmd = CMD_LEFT_SLOW; debugPrintCmd("read left slow"); }
    else if(digitalRead(P_IN_RIGHT_FAST) == HIGH) { cmd = CMD_RIGHT_FAST; debugPrintCmd("read right fast"); }
    else if(digitalRead(P_IN_RIGHT_SLOW) == HIGH) { cmd = CMD_RIGHT_SLOW; debugPrintCmd("read right slow"); }
    //else { cmd = CMD_NONE; printCmd("no buttons read"); }

    // already received command via buttons, flush serial buffer
    if(cmd != CMD_NONE) {
      debugPrint("flushing serial...");
      while(Serial.available()) {
        Serial.read();
      }
      debugPrintln("done");
    } else {
      if(Serial.available()) {
        char c = Serial.read();
        switch(c) {
          case 'a':
            cmd = CMD_LEFT_FAST;
            break;
          case 's':
            cmd = CMD_LEFT_SLOW;
            break;
          case 'd':
            cmd = CMD_RIGHT_SLOW;
            break;
          case 'f':
            cmd = CMD_RIGHT_FAST;
            break;
          case '\n':
          case '\r':
            break;
          case 'h':
            Serial.println("commands:");
            Serial.println("a - left fast");
            Serial.println("s - left slow");
            Serial.println("d - right slow");
            Serial.println("f - right fast");
          default:
            cmd = CMD_NONE;
            debugPrintCmd("serial, no cmd");
            break;
        }
      }
    }

    if(cmd == CMD_LEFT_SLOW) {
        Delay = SPEED_SLOW;
        Serial.print("run left slow... ");
        digitalWrite(P_DIR, P_DIR_LEFT);
        Stepping = true;
    } else if(cmd == CMD_LEFT_FAST) {
        Delay = SPEED_FAST;
        Serial.print("run left fast... ");
        digitalWrite(P_DIR, P_DIR_LEFT);
        Stepping = true;
    } else if(cmd == CMD_RIGHT_SLOW) {
        Delay = SPEED_SLOW;
        Serial.print("run right slow... ");
        digitalWrite(P_DIR, P_DIR_RIGHT);
        Stepping = true;
    } else if(cmd == CMD_RIGHT_FAST)  {
        Delay = SPEED_FAST;
        Serial.print("run right fast... ");
        digitalWrite(P_DIR, P_DIR_RIGHT);
        Stepping = true;
    } else {
      debugPrintCmd("no cmd, sleeping...");
      delay(1000);
    }
  }
}
