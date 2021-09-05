#include "Encoder.h"
#include <Bounce.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>
#include <avr/sleep.h>
#include "kitchen_font.h"
#include "pitches.h"
#include "toneAC.h"
#include "music.h"
#include "music_data.h"

#define PIN_ENCODER_A       7
#define PIN_ENCODER_B       8
#define PIN_ENCODER_BUTTON  16
#define PIN_SLEEP_LED       6
#define PIN_SLEEP_SCREEN    9

// #define USE_SLEEP_LED

// useful to make the timer run faster
#define MS_IN_A_SECOND 1000

#define BRIGHTNESS       16
#define BRIGHTNESS_HALF   8
#define BRIGHTNESS_EXTRA 32

// how long to sound the alarm, after this, we just give up and go back to sleep
#define ALARM_DURATION_SECONDS 60

// how long to wait after user input stops before starting the timer
#define WAIT_AFTER_INPUT_MS              1000
// ignore user inputs for a bit after turning of the alarm as to not set a new time immediately
#define IGNORE_TIMER_SET_AFTER_ALARM_MS  1000 
// how long to wait without user input before going to sleep
const float SLEEP_AFTER_MS   = 5000;
// how long before the display starts to fade, hitting 0 as we go to sleep
const float FADE_AFTER_MS    = 3000;
const float FADE_DURATION_MS = SLEEP_AFTER_MS - FADE_AFTER_MS;

Adafruit_IS31FL3731 matrix = Adafruit_IS31FL3731();
Encoder rotaryEncoder = Encoder(PIN_ENCODER_A, PIN_ENCODER_B);
Bounce  pushbutton    = Bounce(PIN_ENCODER_BUTTON, 10);  // 10 ms debounce

bool buttonDown = false;

elapsedMillis timerSeconds;
elapsedMillis timeSinceInput;
elapsedMillis timeSinceAlarmOff;
elapsedMillis timeSinceButtonDown;

// the main time keeping variable
int time = 0;
// this is used for the double buffering of the display, swaps between 0/1 each update
bool bufferSwapper = true;

// is the alarm active? is set to the number of seconds the alarm will sound and decrements until zero
int alarmActive = 0;

// keep track of where the encoder was last update
long encoderPosition = 0;

long lastFrame = 0;

// this is set by the wake up interrupt, if this is 1 we run the wake up code the next loop()
volatile bool inWakeUp = false;
volatile bool inSleep = false;

void setup() {
  // set all pins as outputs to save power (this is a teensy 2.0 thing)
  for (int i=0; i < 46; i++) {
    if (i == PIN_ENCODER_A) continue;
    if (i == PIN_ENCODER_B) continue;
    if (i == PIN_ENCODER_BUTTON) continue;
    if (i == PIN_SLEEP_SCREEN) continue;
    pinMode(i, OUTPUT);
  }

  // set up the pullup for the encoder push-button
  pinMode(PIN_ENCODER_BUTTON, INPUT_PULLUP);

  // initialize the screen
  if (!matrix.begin()) Serial.println("display not found");

  wakeUp();
}

void wakeUp(){
  inWakeUp = false;

  #ifdef USE_SLEEP_LED
  digitalWrite(PIN_SLEEP_LED, LOW);   // set the LED off
  #endif

  // wake up the screen
  digitalWrite(PIN_SLEEP_SCREEN, HIGH);
   
  matrix.setTextSize(1);
  matrix.setTextWrap(false);  // we dont want text to wrap so it scrolls nicely
  matrix.setTextColor(BRIGHTNESS);
  matrix.setFont(&kitchen_font);
  swapBuffers();

  timeSinceInput = 0;
  timeSinceAlarmOff = IGNORE_TIMER_SET_AFTER_ALARM_MS;
  refreshScreen();
}

void swapBuffers(){
  // this needs to be double buffered because the matrix refreshes so quickly it will flicker
  
  // we display what was the back buffer
  matrix.displayFrame(bufferSwapper ? 0 : 1);

  // swap them around
  bufferSwapper = !bufferSwapper;
  
  // and now we set what was the front buffer to be the target for any updates
  matrix.setFrame(bufferSwapper ? 0 : 1);
  matrix.clear();
  matrix.setCursor(0, 7); 
  matrix.setTextColor(BRIGHTNESS);
}

void readRotaryEncoder(){
  // read the encoder
  long newPosition = -rotaryEncoder.read();
  // look at how far we've moved since last update
  int delta = newPosition - encoderPosition;

  int direction = delta > 0 ? 1 : -1;
  delta = abs(delta);
  
  // one "step" is four ticks, so we only move if we're three+ ticks past our last position
  if (delta < 3) return;
  int ticks = 0;
  
  // update the old position, make sure this stays on multiples of four to match the encoder
  while (delta > 0) {
    encoderPosition += direction * 4;
    delta -= 4;
    ticks++;
  }
  // finally, call the onRotary function to tell it we've moved
  onRotary(ticks * direction, encoderPosition >> 2);
}

void onRotary(int delta, int position){
  timeSinceInput = 0;

  // if the alarm is going off, the knob won't change anything
  if (alarmActive){
    dismissAlarm();
    return;
  }

  // if we recently turned off the alarm, ignore inputs for a bit
  if (alarmRecentlyOff()) return;

  const int volume = 5;
  const int duration = 5;
  const bool background = true;
  if (delta > 0) toneAC(position % 2 == 0 ? NOTE_C7 : NOTE_D7, volume, duration, background);
  else if (time > 0) toneAC(position % 2 == 0 ? NOTE_C7 : NOTE_B6, volume, duration, background);

  // 30 second steps up to 5 minutes, then 1 minute steps
  if (time < 5 * 60) {
    time += delta * 30;
  } else {
    time += delta * 60;  
  }
  
  // don't go below zero
  if (time < 0) time = 0;
  
  refreshScreen(true);
}

void onButtonDown(){
  timeSinceInput = 0;
  
  if (alarmActive){
    dismissAlarm();
    return;
  }
  timeSinceButtonDown = 0;
  buttonDown = true;
}

void onButtonUp(){
  buttonDown = false;
  timeSinceInput = 0;
}

void dismissAlarm(){
  alarmActive = 0;
  timeSinceAlarmOff = 0;
  time = 0;
  playMelody(melody_timer_dismiss);
  refreshScreen();
}

bool waitOnLastFrame = false;
void onTick(){
  if (alarmActive) onAlarm(false);
  if (time == 0) {
    refreshScreen();
    return;
  }

  // don't count time when the user is inputting
  if (timeSinceInput < WAIT_AFTER_INPUT_MS){
    waitOnLastFrame = true;
    return;
  } else if (waitOnLastFrame){
    onStartTimer();
    waitOnLastFrame = false;
    return;
  }
   
  time -= 1;
  if (time < 0) time = 0;

  refreshScreen(true);

  if (time == 0) onAlarm(true);
}

void onAlarm(bool setActive){
  if (setActive) alarmActive = ALARM_DURATION_SECONDS;
  else alarmActive--;
  
  if (alarmActive % 2 == 1) return;
  playMelody(melody_alarm);
}

void onStartTimer(){
  playMelody(melody_timer_start);
}

void refreshScreen(){
  refreshScreen(false);
}

void refreshScreen(bool force){
  long currentFrame = frame();
  if (currentFrame == lastFrame && !force) return;
  lastFrame = currentFrame;
  
  // play alarm animation
  if (alarmActive){
    int frameHalf = currentFrame / 2;
    int frameHalfOffset = currentFrame / 2 + 50;

    matrix.setCursor(3 - frameHalf % 4, 8);
    matrix.print(";");

    matrix.setCursor(11 + frameHalfOffset % 4, 8);
    matrix.print("<");
    
    matrix.setCursor(5 + (currentFrame % 2 == 0 ? 0 : -1), 8);
    matrix.print("@");
    swapBuffers();
    return;
  }

  // alarm's just been turned off, show a little confirm message
  if (alarmRecentlyOff()) {
    matrix.setCursor(2, 7);
    matrix.print("off");
    swapBuffers();
    return;
  }

  // we're idle, show a cute face
  if (time == 0) {
    
    // do a nice fade out before going to sleep
    int time = timeSinceInput;
    float fade = (float) (time - FADE_AFTER_MS)  / FADE_DURATION_MS;
    if (fade < 0) fade = 0;
    matrix.setTextColor(BRIGHTNESS * (1.0 - fade));

    long frame = currentFrame % 40;
    matrix.setCursor(4, 7);
    if (frame > 1) matrix.print("="); // eyes
    matrix.setCursor(7, 7);
    matrix.print("?"); // nose
    matrix.setCursor(5, 13);
    matrix.print(">"); // mouth
    swapBuffers();
    return;
  }

  // timer is active, show the timer digits
  if (minutes() > 0) {
    matrix.print(minutes());

    if (minutes() > 9){
      matrix.print("m"); 
    } else {
      matrix.print(":");
    }
  }
  
  if (seconds() < 10) matrix.print(0);
  matrix.print(seconds());
  if (minutes() == 0) matrix.print("s");
  
  if (seconds() > 0) {
    const int barY = 8;
    int lastBarPixelX = seconds() / 4;
    matrix.drawLine(0, barY, lastBarPixelX, barY, BRIGHTNESS);
    matrix.drawLine(lastBarPixelX + 1, barY, lastBarPixelX + 1, barY, seconds() % 2 == 0 ? BRIGHTNESS_EXTRA : BRIGHTNESS_HALF);
  }
  
  swapBuffers();
}

void loop() {
  if (inWakeUp) wakeUp();

  readRotaryEncoder();
  if (pushbutton.update()){
    if (pushbutton.fallingEdge()) onButtonDown();
    if (pushbutton.risingEdge())  onButtonUp();
  }

  if (timerSeconds >= MS_IN_A_SECOND) {
    onTick();
    timerSeconds -= MS_IN_A_SECOND;
  }

  if (buttonDown && timeSinceButtonDown > 1000) {
    dismissAlarm();
    onButtonUp();
  }

  if (alarmActive || alarmRecentlyOff() || time == 0) refreshScreen();
  if (timeSinceInput > SLEEP_AFTER_MS && time == 0 && alarmActive == 0) sleep();

  updateMelody();
  idle();
}

void idle() {
  set_sleep_mode(SLEEP_MODE_IDLE);
  noInterrupts();
  sleep_enable();
  interrupts();
  sleep_cpu();
  sleep_disable();
}

void sleep() {
  #ifdef USE_SLEEP_LED
    digitalWrite(PIN_SLEEP_LED, HIGH);   // set the LED on
  #endif

  // make sure melodies are stopped, we don't want to start playing anything when we wake up
  stopMelody();

  // reset the button down flag too, just to avoid any funny business
  buttonDown = false;

  // set the sleep flag, this makes the wakeUpInterrupt function actually trigger the wakeup once called
  inSleep = true;

  // setting the pin mode triggers some kind of sleep thing on the screen, so i only do it here
  // not in setup. this seems to work well. 
  pinMode(PIN_SLEEP_SCREEN, OUTPUT);
  digitalWrite(PIN_SLEEP_SCREEN, LOW);

  Serial.end(); // shut off USB
  ADCSRA = 0;   // shut off ADC
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  noInterrupts(); 
  sleep_enable();
  interrupts(); 
  sleep_cpu(); // cpu goes to sleep here
  sleep_disable(); // this is where we come back in again after sleeping
}


// this function gets called by the encoder class, i couln't make the interrupts play nice together
// so i hacked it in there, this sets a flag that is then read by the loop() which brings everything back
void wakeUpInterrupt(){
  if (!inSleep) return;
  inSleep = false;
  inWakeUp = true;
}

int seconds() {
  return time % 60;
}

int minutes(){
  return time / 60;
}

long frame() {
  return millis() / 100;
}

bool alarmRecentlyOff(){
  return timeSinceAlarmOff < IGNORE_TIMER_SET_AFTER_ALARM_MS;
}
