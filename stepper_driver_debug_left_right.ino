// serial / button debugging snippet for stepper motors
// using step/direction interface of common stepper drivers

#define DISTANCE 3200
#define SPEED_SLOW 1000
#define SPEED_FAST 200

// #define SIG_REVERSE 1

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

int StepCounter = 0;
int Delay = SPEED_SLOW;
int Stepping = false;

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
  char cmd;

  if (Stepping) {
    // do the steps
    digitalWrite(P_STEP, SIG_ON);
    delayMicroseconds(Delay);
    digitalWrite(P_STEP, SIG_OFF);
    delayMicroseconds(Delay);

    StepCounter = StepCounter + 1;

    if (StepCounter == DISTANCE)
    {
      StepCounter = 0;
      Stepping = false;
      Serial.println("done");
    }
  } else {
    // read input commands from buttons or serial
    if(digitalRead(P_IN_LEFT_FAST) == HIGH) { cmd = CMD_LEFT_FAST; }
    else if(digitalRead(P_IN_LEFT_SLOW) == HIGH) { cmd = CMD_LEFT_SLOW; }
    else if(digitalRead(P_IN_RIGHT_FAST) == HIGH) { cmd = CMD_RIGHT_FAST; }
    else if(digitalRead(P_IN_RIGHT_SLOW) == HIGH) { cmd = CMD_RIGHT_SLOW; }
    else { cmd = CMD_NONE; }

    // already received command via buttons, flush serial buffer
    if(cmd != CMD_NONE) {
      while(Serial.available()) {
        Serial.read();
      }
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
          case 'h':
            Serial.println("commands:");
            Serial.println("a - left fast");
            Serial.println("s - left slow");
            Serial.println("d - right slow");
            Serial.println("f - right fast");
          default:
            cmd = CMD_NONE;
            break;
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
    }
  }
}
