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

// any remaining ticks on the rotary will be cleared after this long (ms)
#define CLEAR_TICKS_AFTER_MS 750

// how long to wait after user input stops before starting the timer
#define WAIT_AFTER_INPUT_MS              1000
// ignore user inputs for a bit after turning of the alarm as to not set a new time immediately
#define IGNORE_TIMER_SET_AFTER_ALARM_MS  1000 
// how long to wait in the view total state
#define WAIT_WHILE_VIEW_TOTAL_MS         2000 
// how long to wait without user input before going to sleep
const float SLEEP_AFTER_MS   = 5000;
// how long before the display starts to fade, hitting 0 as we go to sleep
const float FADE_AFTER_MS    = 3000;
const float FADE_DURATION_MS = SLEEP_AFTER_MS - FADE_AFTER_MS;


enum State {
  IDLE,
  SET_TIMER,
  TIMER,
  ALARM,
  ALARM_OFF_COOLDOWN,
  VIEW_TOTAL,
  MUSIC,
};

State state = IDLE;
elapsedMillis timeInState;

Adafruit_IS31FL3731 matrix = Adafruit_IS31FL3731();
Encoder rotaryEncoder = Encoder(PIN_ENCODER_A, PIN_ENCODER_B);
Bounce  pushbutton    = Bounce(PIN_ENCODER_BUTTON, 10);  // 10 ms debounce

bool buttonDown = false;

elapsedMillis timerSeconds;
elapsedMillis timeSinceInput;
elapsedMillis timeSinceTick;
elapsedMillis timeButtonHold;

// the main time keeping variable
int time = 0;
// this stores the total time counted for this "session"
int timeTotal = 0;

// this is used for the double buffering of the display, swaps between 0/1 each update
bool bufferSwapper = true;

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
  setState(ALARM_OFF_COOLDOWN);
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

int ticks = 0;
void readRotaryEncoder(){
  int delta = -rotaryEncoder.readAndReset();
  if (delta == 0) return;
    
  // because one "step" on the encoder is four ticks, it will sometimes become offset by a tick or two
  // this will manifest as the timer jumping in a strange fashion. this has gotten worse as the encoder has 
  // been getting worn out, the way i fix this is to reset the tick counter if there's been no input for a short while
  // this assumes that if the encoder is still, it's in one of the detents
  if (timeSinceTick > CLEAR_TICKS_AFTER_MS && ticks != 0) ticks = 0;
  
  
  timeSinceTick = 0;
  ticks += delta;

  // one "step" is four ticks, so we only move if we're four+ ticks past our last position
  if (ticks < 4 && ticks > -4) return;
  
  int direction = delta > 0 ? 1 : -1;
  encoderPosition += direction;
  
  // finally, call the onRotary function to tell it we've moved
  onRotary(direction, encoderPosition >> 2);
  ticks = 0;
}

void onRotary(int delta, int position){
  timeSinceInput = 0;

  // if the alarm is going off, the knob won't change anything
  if (state == ALARM){
    dismissAlarm();
    return;
  }

  // if we recently turned off the alarm, ignore inputs for a bit
  if (state == ALARM_OFF_COOLDOWN) return;

  setState(SET_TIMER);

  const int volume = 5;
  const int duration = 5;
  const bool background = true;
  if (delta > 0) toneAC(position % 2 == 0 ? NOTE_C7 : NOTE_D7, volume, duration, background);
  else if (time > 0) toneAC(position % 2 == 0 ? NOTE_C7 : NOTE_B6, volume, duration, background);

  // if the counter is at zero, we also reset the timeTotal counter to make this a new "session"
  if (time == 0) timeTotal = 0;

  // 30 second steps up to 5 minutes, then 1 minute steps
  if (time < 5 * 60) {
    time += delta * 30;
    timeTotal += delta * 30;
  } else {
    time += delta * 60;
    timeTotal += delta * 60;
  }
  
  // don't go below zero
  if (time < 0) time = 0;
  if (timeTotal < 0) timeTotal = 0;
  
  refreshScreen(true);
}

void onButtonDown(){
  timeSinceInput = 0;
  
  if (state == ALARM){
    dismissAlarm();
    return;
  }

  if (state == IDLE || state == TIMER){
    setState(VIEW_TOTAL);
    return;
  }
  
  buttonDown = true;
}

void onButtonUp(){
  buttonDown = false;
  timeSinceInput = 0;
}

void dismissAlarm(){
  setState(ALARM_OFF_COOLDOWN);
  time = 0;
  playMelody(melody_timer_dismiss);
  refreshScreen();
}

void onSecond(){
  if (state == ALARM) onAlarm(false);
  if (time == 0) {
    refreshScreen();
    return;
  }

  if (state != TIMER) return;
    
  time -= 1;
  if (time < 0) time = 0;

  refreshScreen(true);

  if (time == 0) onAlarm(true);
}

void onAlarm(bool setActive){
  if (setActive) setState(ALARM);
  if ((timeInState / 1000) % 2 == 1) return;
  playMelody(melody_alarm);
}

void refreshScreen(){
  refreshScreen(false);
}

void refreshScreen(bool force){
  long currentFrame = frame();
  if (currentFrame == lastFrame && !force) return;
  lastFrame = currentFrame;
  
  // play alarm animation
  if (state == ALARM){
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
  if (state == ALARM_OFF_COOLDOWN) {
    matrix.setCursor(2, 7);
    matrix.print("off");
    swapBuffers();
    return;
  }

  // we're idle, show a cute face
  if (state == IDLE) {
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

  int seconds = -1;
  int minutes = -1;

  // timer is active, show the timer digits
  if (state == TIMER || state == SET_TIMER) {
    seconds = time % 60;
    minutes = time / 60;

    if (seconds > 0) {
      const int barY = 8;
      int lastBarPixelX = seconds / 4;
      matrix.drawLine(0, barY, lastBarPixelX, barY, BRIGHTNESS);
      matrix.drawLine(lastBarPixelX + 1, barY, lastBarPixelX + 1, barY, seconds % 2 == 0 ? BRIGHTNESS_EXTRA : BRIGHTNESS_HALF);
    }
  }

  if (state == VIEW_TOTAL){
    seconds = timeTotal % 60;
    minutes = timeTotal / 60;
  }

  if (state == TIMER || state == SET_TIMER || state == VIEW_TOTAL) {
    if (minutes > 0) {
      matrix.print(minutes);
  
      if (minutes > 9){
        matrix.print("m"); 
      } else {
        matrix.print(":");
      }
    }

    if (seconds < 10) matrix.print(0);
    matrix.print(seconds);
    if (minutes == 0) matrix.print("s");
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
  if (!buttonDown) timeButtonHold = 0;

  if (timerSeconds >= MS_IN_A_SECOND) {
    onSecond();
    timerSeconds -= MS_IN_A_SECOND;
  }

  switch(state) {
    case IDLE: loopIdle();
    break;
    case SET_TIMER: loopSetTimer();
    break;
    case TIMER: loopTimer();
    break;
    case ALARM: loopAlarm();
    break;
    case ALARM_OFF_COOLDOWN: loopAlarmOffCooldown();
    break;
    case VIEW_TOTAL: loopViewTotal();
    break;
    case MUSIC: loopMusic();
  }
  
  updateMelody();
  idle();
}

void loopIdle(){
  refreshScreen();
  if (timeSinceInput > SLEEP_AFTER_MS) sleep();
}

void loopSetTimer(){
  if (timeSinceInput < WAIT_AFTER_INPUT_MS) return;
  if (time > 0){
    playMelody(melody_timer_start);
    setState(TIMER);
  } else {
    playMelody(melody_timer_dismiss);
    setState(IDLE);
  }
  // we reset this here to make the delay until the timer ticks consistent
  timerSeconds = 700;
}

void loopTimer(){

}

void loopAlarm(){
  // if the button is down, we instantly dismiss any alarm, if it's just the timer, we wait for one second first
  if (buttonDown) {
    dismissAlarm();
    onButtonUp();
  }
  refreshScreen();
}

void loopAlarmOffCooldown(){
  refreshScreen();
  if (timeInState < IGNORE_TIMER_SET_AFTER_ALARM_MS) return;
  setState(IDLE);
}

void loopViewTotal(){
  refreshScreen();
  if (timeButtonHold > 3000){
    playMelody(melody_nevergonnagive);
    setState(MUSIC);
  }
  if (buttonDown || timeInState < WAIT_WHILE_VIEW_TOTAL_MS) return;
  if (time > 0){
    setState(TIMER);
    playMelody(melody_timer_start);
  } else {
    playMelody(melody_timer_dismiss);
    setState(IDLE);
  }
}

void loopMusic(){
  if (!isPlayingMelody()) setState(IDLE);
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
  return;
  
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

long frame() {
  return millis() / (long) 100;
}

void setState(State newState){
  if (state == newState) return;
  state = newState;
  timeInState = 0;
  switch(state) {
    case IDLE: Serial.println("IDLE");
    break;
    case SET_TIMER: Serial.println("SET_TIMER");
    break;
    case TIMER: Serial.println("TIMER");
    break;
    case ALARM: Serial.println("ALARM");
    break;
    case ALARM_OFF_COOLDOWN: Serial.println("ALARM_OFF_COOLDOWN");
    break;
    case VIEW_TOTAL: Serial.println("VIEW_TOTAL");
    break;
    case MUSIC: Serial.println("MUSIC");
  }
}
