// serial / button debugging snippet for stepper motors
// using step/direction interface of common stepper drivers

#include <Arduino.h>
#include <Wire.h>
#include <TMCStepper.h>
#include <U8g2lib.h>

#define DISTANCE 3200
#define SPEED_SLOW 1000
#define SPEED_FAST 50

#define DEBUG 1
//#define SIG_REVERSE 1

#ifdef SIG_REVERSE
#define SIG_ON LOW
#define SIG_OFF HIGH
#else
#define SIG_ON HIGH
#define SIG_OFF LOW
#endif

#define P_ENABLE 16
#define P_DIR 8
#define P_STEP 9
#define P_DIAG 10
#define P_DIR_LEFT SIG_ON
#define P_DIR_RIGHT SIG_OFF

#define DRIVER_ADDRESS 0b00   // MS1/MS2 both GND for address 0
#define R_SENSE       0.11f   // BTT TMC2209 sense value

//HardwareSerial & TMC_SERIAL = Serial1;  // 32u4 extra UART
//TMC2209Stepper tmcdriver(&TMC_SERIAL, R_SENSE, DRIVER_ADDRESS);
TMC2209Stepper tmcdriver(&Serial1, R_SENSE, DRIVER_ADDRESS);
//TMC2209Stepper tmcdriver(14, 15, R_SENSE, DRIVER_ADDRESS);

// HW i2c pin D2 / D3
#define P_IN_LEFT_FAST 4
#define P_IN_LEFT_SLOW 5
#define P_IN_RIGHT_SLOW 6
#define P_IN_RIGHT_FAST 7

// SH1106 128x64 OLED over hardware I2C
//U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
U8X8_SH1106_128X64_NONAME_HW_I2C oled(/* reset=*/ U8X8_PIN_NONE);

enum InputCommands {
  CMD_NONE = 0,
  CMD_LEFT_SLOW,
  CMD_LEFT_FAST,
  CMD_RIGHT_SLOW,
  CMD_RIGHT_FAST
};

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

#define serprintf(...) debugPrint(__VA_ARGS__)
#ifdef DEBUG
  #define debugPrint(...)       debugPrint(__VA_ARGS__)
  #define debugPrintln(...)     debugPrintln(__VA_ARGS__)
  #define debugPrintCmd(...)     do {                     \
                                    debugPrintln((int)cmd, " ", __VA_ARGS__); \
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
  void start(int delay_us) { _expected_endtime = (unsigned long) micros() + delay_us; }
  void stop() { _expected_endtime = 0; }
  void delay() {
    unsigned long now = micros();
    if(_expected_endtime == 0) {
      //debugPrintln("ERROR: uninitialized EnsureDelay ", __FUNCTION__, ":", __LINE__);
      // First time coming into this, no delay needed in our special case
      return;
    }
    int timediff = _expected_endtime - now;
    if(timediff >= 0) {
      delayMicroseconds(timediff);
      _expected_endtime = 0;
    } else {
      debugPrintln("time deadline not met, expected=", _expected_endtime, " now=", now, " diff=", timediff, " us");
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
auto ensure_step_end_delay = EnsureDelay();
int Delay = SPEED_SLOW;
// ui state variable what input cmd was given by user
//  not strictly needed to be global, just in case and for debugging
char cmd = CMD_NONE;
// flush serial after one cmd finished executing
int flush_serial = false;
// max value at end of cmd run
int max_sg = 0;
int max_rms = 0;

/*
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define printD(e) debugPrintln("\"", STR(e), "\"", e)
*/

void drawScreen() {
  //oled.clear();
  oled.drawString(0, 0, "DIY StepperCtrl");
  char tmp[17];
  snprintf(tmp, sizeof(tmp)-1, "ms=%3d rms=%4d", tmcdriver.microsteps(), tmcdriver.rms_current());
  oled.drawString(0, 2, tmp);
  snprintf(tmp, sizeof(tmp)-1, "sg=%4d cs=%4d", tmcdriver.SG_RESULT(), tmcdriver.cs2rms(tmcdriver.cs_actual()));
  oled.drawString(0, 4, tmp);
  snprintf(tmp, sizeof(tmp)-1, "sg=%4d cs=%4d", max_sg, max_rms);
  oled.drawString(0, 5, tmp);
}

void setup() {
  // setup oled
  Wire.begin();   // Pro Micro: D2 = SDA, D3 = SCL
  oled.begin();
  oled.setBusClock(400000);
  oled.setPowerSave(0);
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.clear();
  drawScreen();

  // setup ui pins
  pinMode(P_IN_LEFT_SLOW, INPUT);
  pinMode(P_IN_LEFT_FAST, INPUT);
  pinMode(P_IN_RIGHT_SLOW, INPUT);
  pinMode(P_IN_RIGHT_FAST, INPUT);
  pinMode(P_DIAG, INPUT);

  Serial.begin(115200);
  Serial.print("Initializing...");

  pinMode(P_ENABLE, OUTPUT);
  digitalWrite(P_ENABLE, SIG_ON);     // disable driver

  pinMode(P_DIR, OUTPUT);
  pinMode(P_STEP, OUTPUT);
  digitalWrite(P_DIR, SIG_OFF);
  digitalWrite(P_STEP, SIG_OFF);

  //TMC_SERIAL.begin(115200);
  Serial1.begin(115200);              // for hw serial
  //tmcdriver.beginSerial(115200);   // for sw serial
  tmcdriver.begin();                 // init driver over UART
  tmcdriver.toff(4);                 // enable driver (chopper)
  tmcdriver.rms_current(800);        // set motor current (mA)
  tmcdriver.microsteps(16);          // set microstepping
  tmcdriver.en_spreadCycle(false);   // stealthChop
  tmcdriver.pwm_autoscale(true);

  digitalWrite(P_ENABLE, SIG_OFF);      // enable driver
  Serial.println("done");
}

static inline int inc_microsteps(void) {
    int ms = tmcdriver.microsteps();
    int newms = ms > 128 ? 256 : ms << 1;
    tmcdriver.microsteps(newms);
    int newmsd = tmcdriver.microsteps();
    serprintf(F("ms "), ms, F(" -> "), newms, F("/"), newmsd, F("\n"));
    return newms;
}

static inline int dec_microsteps(void) {
    int ms = tmcdriver.microsteps();
    int newms = ms < 2 ? 1 : ms >> 1;
    tmcdriver.microsteps(newms);
    int newmsd = tmcdriver.microsteps();
    serprintf(F("ms "), ms, F(" -> "), newms, F("/"), newmsd, F("\n"));
    return newms;
}

static inline int inc_rms() {
  int rms = tmcdriver.rms_current();
  int newrms = rms > 1800 ? 2000 : rms + 200;
  tmcdriver.rms_current(newrms);
  int newrmsd = tmcdriver.rms_current();
  serprintf(F("rms "), rms, F(" -> "), newrms, F("/"), newrmsd, F("\n"));
  return newrms;
}
static inline int dec_rms() {
  int rms = tmcdriver.rms_current();
  int newrms = rms < 400 ? 200 : rms - 200;
  tmcdriver.rms_current(newrms);
  int newrmsd = tmcdriver.rms_current();
  serprintf(F("rms "), rms, F(" -> "), newrms, F("/"), newrmsd, F("\n"));
  return newrms;
}

void loop() {
  if (Stepping) {
    if(StepCounter == 0) {
      oled.clearLine(7);
      oled.drawString(0, 7, "stepping...");
    } /* else if (StepCounter % 128 == 0) {
      // too slow... time underrun of 2700us!! wtf...
      //oled.drawString(10, 7, String(StepCounter).c_str());
    } */
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
      oled.clearLine(7);
      max_sg = tmcdriver.SG_RESULT();
      max_rms = tmcdriver.cs2rms(tmcdriver.cs_actual());
      char tmp[17];
      snprintf(tmp, sizeof(tmp)-1, "done      %5d", StepCounter);
      oled.drawString(0, 7, tmp);
      //oled.drawString(0, 7, "done");
      //oled.drawString(10, 7, String(StepCounter).c_str());
      StepCounter = 0;
      Stepping = false;
      cmd = CMD_NONE;
      ensure_step_end_delay.stop();
      Serial.println("done");
      drawScreen();
      flush_serial = true;
    }
  } else {
    if(flush_serial) {
      // flush serial from before last executed accel
      while(Serial.available()) {Serial.read();}
      flush_serial = false;
    }
    // read input commands from buttons or serial
    if(digitalRead(P_IN_LEFT_FAST) == HIGH) { cmd = CMD_LEFT_FAST; debugPrintCmd("read left fast"); }
    else if(digitalRead(P_IN_LEFT_SLOW) == HIGH) { cmd = CMD_LEFT_SLOW; debugPrintCmd("read left slow"); }
    else if(digitalRead(P_IN_RIGHT_FAST) == HIGH) { cmd = CMD_RIGHT_FAST; debugPrintCmd("read right fast"); }
    else if(digitalRead(P_IN_RIGHT_SLOW) == HIGH) { cmd = CMD_RIGHT_SLOW; debugPrintCmd("read right slow"); }
    //else { cmd = CMD_NONE; printCmd("no buttons read"); }

    // already received command via buttons, flush serial buffer
    if(cmd != CMD_NONE) {
      flush_serial = true;
      //debugPrint("flushing serial...");
      //while(Serial.available()) {
      //  Serial.read();
      //}
      //debugPrintln("done");
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
          case 'v': {
            Serial.print(F("\nTesting connection..."));
            uint8_t result = tmcdriver.test_connection();
            if (result) {
              Serial.println(F("failed!"));
              Serial.print(F("Likely cause: "));
              switch (result) {
                case 1: Serial.println(F("loose connection")); break;
                case 2: Serial.println(F("no power")); break;
              }
              Serial.println(F("Fix the problem and reset board."));
              //abort();
            } else {
              Serial.println(F("OK"));
            }

            serprintf("ms=", tmcdriver.microsteps());
            serprintf(" rms=", tmcdriver.rms_current(), "\n");
            auto drvstatus = tmcdriver.DRV_STATUS();
            Serial.print(drvstatus, BIN);
            Serial.print(" ");
            Serial.print(tmcdriver.SG_RESULT(), DEC);
            Serial.print(" ");
            Serial.println(tmcdriver.cs2rms(tmcdriver.cs_actual()), DEC);
            serprintf("dialg=", digitalRead(P_DIAG));
            break;
          }
          case '[':
            dec_microsteps();
            break;
          case ']':
            inc_microsteps();
            break;
          case '\'':
            dec_rms();
            break;
          case '\\':
            inc_rms();
            break;
          case '\n':
            break;
          case '\r':
            break;
          case 'h':
            Serial.println(F("commands:"));
            Serial.println(F("a / s - fast / slow left"));
            Serial.println(F("f / d - fast / slow right"));
            Serial.println(F("[ / ] - inc/dec microsteps"));
            Serial.println(F("\' / \\ - inc/dec Motor Amps"));
          default:
            cmd = CMD_NONE;
            //debugPrintCmd("serial, no cmd");
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
      //debugPrintCmd("no cmd, sleeping...");
      //debugPrint(".");
      //delay(1);
      // slow enough to work as delay
      drawScreen();
    }
  }
}
