/* Gait cycle mimicking code for stepper motors
  Since the gear ratio of these motors is actually 63.68395. Each stepper takes about 240mA of 
  current at 5V in ALL operating conditions.
  
  ------------------------------------------------------------------------------------------------------------
  |    BAC 1    |     BAC 2     |    BAC 3    |    BAC 4    |   BAC 5   |   BAC 6   |   BAC 7   |   BAC 8    |
  | 1st Contact | Load Response | Mid  Stance | Term Stance | Pre Swing | 1st Swing | Mid Swing | Term Swing |
  ------------------------------------------------------------------------------------------------------------

  Notes:
  - pin assignment for Int1, Int2, Int3, Int4 is 1,3,2,4 in order on MCU.
  - HALF4WIRE is 0.09deg accuracy -> 4096 steps/rev EDIT: 4076!
  - FULL4WIRE is 0.18deg accuracy -> 2048 steps/rev EDIT: 2038!

*/
#include "AccelStepper.h"
#include "Wire.h"

#define DELAY_SPEED 3

// Custom stepper positions, all reference the CENTER_POS.
// 2038/4 = 509.5
// 2038/2 = 1019
// Note, no decimal though!!
#define CALIBRATION_POS 1019
#define CENTER_POS 480 //509

AccelStepper knee_stepper(AccelStepper::FULL4WIRE, 8, 10, 9, 11); // 8 - 11
AccelStepper hip_stepper(AccelStepper::FULL4WIRE, 0, 2, 1, 3); // 0 - 3
AccelStepper trigger_stepper(AccelStepper::FULL4WIRE, 4, 6, 5, 7); // 4 - 7

int home_pin = 13;
const int strides = 3;
long knee_ = 0;
uint8_t gait_stage = 0, stride_num = 0;

void setup() {
  // Begin as slave address 9:
  Wire.begin(9);
  Wire.setSDA(18);
  Wire.setSCL(19);
//  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  
  hip_stepper.setMaxSpeed(500.0);
  knee_stepper.setMaxSpeed(500.0);
  trigger_stepper.setMaxSpeed(1000.0);

  hip_stepper.setAcceleration(200.0);
  knee_stepper.setAcceleration(200.0);
  trigger_stepper.setAcceleration(200.0);

  Serial.begin(9600);
  delay(100);
  Serial.println(
    "Stepper Motor Gait System"
    "\nMimicks the full gait cycle of one leg from stance, stride, swing"
    "\nBAC phases are tracked and recorded over Serial.\n"
    "\nPress the start button to begin...\n"
  );
  
  pinMode(home_pin, INPUT); 
  while(digitalRead(home_pin) == LOW);
  
  delay(100);
  Serial.println("Recalibration");
  
  setCalibrationPosition();
  triggerCalibration();

  Serial.println("Calibrated.\n");
  delay(500);
}

void loop() {
  Serial.println("Running Trial...");
  runTrial(); 
  Serial.println("\tTrial Done."); 
  while(1);
}

// Function for whenever the Master T3.6 calls the TLC over I2C
// Sends 4 Bytes
void requestEvent() {
  // long conversion to 16 bit integer
  int16_t val = (int16_t)(knee_);

  // Encode the data packet
  byte bytes[4];  
  bytes[0] = (val >> 8) & 0xFF;
  bytes[1] = val & 0xFF;
  bytes[2] = gait_stage;
  bytes[3] = stride_num;

  Wire.write(bytes, sizeof(bytes));
}

// Calibration "Homing" procedure
// Moves both arms into a mechanical stop, and sets position as zero.
// Then moves both arms to the center.

// Notes:
// - runToPosition() is dodgy, replace with run() & while loops
// - use 1024 as center if using HALF4WIRE mode, 512 for FULL4WIRE
void setCalibrationPosition() {
  // Set hip joint parameters for calibration position
  Serial.print("Moving thigh...");
  hip_stepper.setSpeed(1000);
  hip_stepper.moveTo(1024);
  while(hip_stepper.currentPosition() != 1024) {
    hip_stepper.run();
    delay(4);
  }
  Serial.println("\t\tDone");
  delay(500);

  // Set knee joint parameters for calibration position
  Serial.print("Moving shank..."); 
  knee_stepper.setSpeed(1000);
  knee_stepper.moveTo(1024);
  while(knee_stepper.currentPosition() != 1024) {
    knee_stepper.run();
    delay(4);
  }
  Serial.println("\t\tDone");

  // Resest current hip and knee positions as zero
  knee_stepper.setCurrentPosition(CALIBRATION_POS);
  knee_stepper.setMaxSpeed(1000.0);
  knee_stepper.setAcceleration(400.0);
  hip_stepper.setCurrentPosition(CALIBRATION_POS);
  hip_stepper.setMaxSpeed(1000.0);
  hip_stepper.setAcceleration(400.0);
  delay(500);

  // Move to 0
  Serial.print("Resetting both to 0...");
  knee_stepper.setSpeed(200);
  knee_stepper.moveTo(CENTER_POS);
  hip_stepper.setSpeed(200);
  hip_stepper.moveTo(CENTER_POS);
  while(knee_stepper.currentPosition() != CENTER_POS && hip_stepper.currentPosition() != CENTER_POS) {
    knee_stepper.run();
    knee_ = knee_stepper.currentPosition();
    delay(5);
    hip_stepper.run();
    delay(5);
  }
  Serial.println("\tDone");
  delay(500);
}

void triggerCalibration() {
  Serial.print("Calibrating Trigger...");
  trigger_stepper.setSpeed(1000);
  trigger_stepper.moveTo(-2100);
  while(trigger_stepper.currentPosition() != -2100) {
    trigger_stepper.run();
    delay(3);
  }
  Serial.println("\tDone");

  // Reset trigger position as zero
  trigger_stepper.setCurrentPosition(0);
  trigger_stepper.setMaxSpeed(1000.0);
  trigger_stepper.setAcceleration(400.0);
  delay(500);
  
  Serial.print("Loading Impulse...");
  trigger_stepper.setSpeed(1000);
  trigger_stepper.moveTo(2100);
  while(trigger_stepper.currentPosition() != 2100) {
    trigger_stepper.run();
    delay(3);
  }
  Serial.println("\tDone");
  delay(500);

  Serial.print("Firing Impulse...");
  trigger_stepper.setSpeed(1000);
  trigger_stepper.moveTo(2130);
  while(trigger_stepper.currentPosition() != 2130) {
    trigger_stepper.run();
    delay(3);
  }
  Serial.println("\tDone");
  delay(500);
  
}

// Cycles through Gait Phases BAC 1 to BAC 8
// Note: For proper multi-stepper function, align delta hip & knee step values together
void runTrial() {
  int impulseEvent = 0;
  
  // First leg lift with bent knee
  hip_stepper.setSpeed(1000);
  hip_stepper.moveTo(CENTER_POS-250);
  knee_stepper.setSpeed(1000);
  knee_stepper.moveTo(CENTER_POS+125); 
  while(hip_stepper.currentPosition() != CENTER_POS-250) {
    if(hip_stepper.currentPosition() <= CENTER_POS-125 && knee_stepper.currentPosition() != CENTER_POS+125) {
      knee_stepper.run();
      knee_ = knee_stepper.currentPosition();
      delay(DELAY_SPEED);
    }
    hip_stepper.run();
    delay(DELAY_SPEED);
  }
//  while(1);

  // Start strides
  for(int i = 1; i <= strides; i++) {
    stride_num = i;

    // Movement from BAC 1 - 4
    gait_stage = 1;
    hip_stepper.setSpeed(1000);
    hip_stepper.moveTo(CENTER_POS+250); // from -250
    knee_stepper.setSpeed(1000);
    knee_stepper.moveTo(CENTER_POS); // from +125
    while(hip_stepper.currentPosition() != CENTER_POS+250) {
      if(hip_stepper.currentPosition() <= CENTER_POS && knee_stepper.currentPosition() != CENTER_POS) {
        gait_stage = 2;
        knee_stepper.run();
        knee_ = knee_stepper.currentPosition();
        delay(DELAY_SPEED);
        if(knee_stepper.distanceToGo() == 0)
          gait_stage = 3;
      }
      hip_stepper.run();
      delay(DELAY_SPEED);
      if(hip_stepper.distanceToGo() == 100)
          gait_stage = 4;
    }

    // Movement from BAC 5 - 8
    gait_stage = 5;
    hip_stepper.setSpeed(1000);
    hip_stepper.moveTo(CENTER_POS-250); // from +250
    knee_stepper.setSpeed(1000);
    knee_stepper.moveTo(CENTER_POS+250); // from 0
    while(hip_stepper.currentPosition() != CENTER_POS-250) { // 500 steps
      if(knee_stepper.distanceToGo() == 0) {
        knee_stepper.moveTo(CENTER_POS); // from +250
      }
      if(hip_stepper.currentPosition() == CENTER_POS)
        gait_stage = 6;
      if(hip_stepper.distanceToGo() == 200)
        gait_stage = 7;
      if(hip_stepper.distanceToGo() == 75)
        gait_stage = 8;
      hip_stepper.run();
      delay(DELAY_SPEED);
      knee_stepper.run();
      knee_ = knee_stepper.currentPosition();
      delay(DELAY_SPEED);
    }
    impulseEvent++;
  }
    
  // Resetting position separately
  Serial.println("Resetting Position...");
  gait_stage = 255;
  hip_stepper.setSpeed(1000);
  hip_stepper.moveTo(CENTER_POS);
  while(hip_stepper.currentPosition() != CENTER_POS) {
    hip_stepper.run();
    delay(DELAY_SPEED);
  }  
  knee_stepper.setSpeed(1000);
  knee_stepper.moveTo(CENTER_POS);
  while(knee_stepper.currentPosition() != CENTER_POS) {
    knee_stepper.run();
    knee_ = knee_stepper.currentPosition();
    delay(DELAY_SPEED);
  } 
}
