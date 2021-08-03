#include <Encoder.h>
#include <Bounce.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>
#include "kitchen_font.h"
#include "pitches.h"
#include "toneAC.h"
#include "music.h"
#include "music_data.h"

#define PIN_ENCODER_A      18
#define PIN_ENCODER_B      19
#define PIN_ENCODER_BUTTON 20
#define PIN_PIEZO          25

// useful to make the timer run faster
#define MS_IN_A_SECOND 1000

#define MAX_SECONDS 59

#define BRIGHTNESS 16
#define BRIGHTNESS_HALF 8
#define BRIGHTNESS_EXTRA 32

#define ALARM_DURATION_SECONDS 30

// how long to wait after user input stops before starting the timer
#define WAIT_AFTER_INPUT_MS 1000 
#define IGNORE_TIMER_SET_AFTER_ALARM_MS 1000 

Adafruit_IS31FL3731 matrix = Adafruit_IS31FL3731();

Encoder rotaryEncoder(PIN_ENCODER_A, PIN_ENCODER_B);
Bounce pushbutton = Bounce(PIN_ENCODER_BUTTON, 10);  // 10 ms debounce

elapsedMillis timerSeconds;
elapsedMillis timeSinceInput;
elapsedMillis timeSinceAlarmOff;

int time = 2;
bool bufferSwapper = true;
int alarmActive = false;

int seconds() {
  return time % 60;
}

int minutes(){
  return time / 60;
}

void setup() {
  Serial.begin(9600);
  Serial.println("This kitchen timer speaks serial. That's unusual.");

  pinMode(PIN_ENCODER_BUTTON, INPUT_PULLUP);
  
  if (!matrix.begin()) Serial.println("display not found");

  wakeUp();
}

void wakeUp(){
  matrix.setTextSize(1);
  matrix.setTextWrap(false);  // we dont want text to wrap so it scrolls nicely
  matrix.setTextColor(BRIGHTNESS);
  matrix.setFont(&kitchen_font);
  swapBuffers();

  timeSinceInput = WAIT_AFTER_INPUT_MS;
  timeSinceAlarmOff = IGNORE_TIMER_SET_AFTER_ALARM_MS;
  refreshScreen();
}

void swapBuffers(){
  // this needs to be double buffered because the matrix refreshes so quickly it will flicker
  matrix.displayFrame(bufferSwapper ? 0 : 1);
  
  bufferSwapper = !bufferSwapper;
  matrix.setFrame(bufferSwapper ? 0 : 1);
  matrix.clear();
  matrix.setCursor(0, 7); 
}

// keep track of where the encoder was last update
long oldPosition = 0;
void readRotaryEncoder(){
  // read the encoder
  long newPosition = rotaryEncoder.read();
  // look at how far we've moved since last update
  int delta = newPosition - oldPosition;
  
  int direction = delta > 0 ? 1 : -1;
  delta = abs(delta);
  
  // one "step" is four ticks, so we only move if we're three+ ticks past our last position
  if (delta < 3) return;
  int ticks = 0;
  
  // update the old position, make sure this stays on multiples of four to match the encoder
  while (delta > 0) {
    oldPosition += direction * 4;
    delta -= 4;
    ticks++;
  }
  // finally, call the onRotary function to tell it we've moved
  onRotary(ticks * direction, oldPosition >> 2);
}

void onRotary(int delta, int position){
  const int volume = 5;
  const int duration = 5;
  const bool background = true;

  // if the alarm is going off, the knob won't change anything
  if (alarmActive){
    dismissAlarm();
    return;
  }

  // if we recently turned off the alarm, ignore inputs for a bit
  if (alarmRecentlyOff()) return;
  
  if (delta > 0) toneAC(position % 2 == 0 ? NOTE_C7 : NOTE_D7, volume, duration, background);
  else toneAC(position % 2 == 0 ? NOTE_C7 : NOTE_B6, volume, duration, background);

  time += delta * 60;
  if (time < 0) time = 0;
  
  timeSinceInput = 0;
  alarmActive = 0;

  refreshScreen();
}

bool alarmRecentlyOff(){
  return timeSinceAlarmOff < IGNORE_TIMER_SET_AFTER_ALARM_MS;
}

void onButton(){
  if (alarmActive) dismissAlarm();
  else time = 2;
}

void dismissAlarm(){
  alarmActive = 0;
  timeSinceAlarmOff = 0;
  sfxConfirm();
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

  refreshScreen();

  if (time == 0) onAlarm(true);
}


void onAlarm(bool setActive){
  if (setActive) alarmActive = ALARM_DURATION_SECONDS;
  else alarmActive--;
  
  if (alarmActive % 2 == 1) return;
  playMelody(melody_alarm);
}

void onStartTimer(){
  sfxConfirm();
}

void refreshScreen(){
  if (alarmActive){
    int frame = millis() / 100;
    int frameHalf = millis() / 200;
    int frameHalfOffset = millis() / 200 + 100;

    matrix.setCursor(3 - frameHalf % 4, 8);
    matrix.print(";");

    matrix.setCursor(11 + frameHalfOffset % 4, 8);
    matrix.print("<");
    
    matrix.setCursor(5 + (frame % 2 == 0 ? 0 : -1), 8);
    matrix.print("@");
    swapBuffers();
    return;
  }

  if (alarmRecentlyOff()) {
    matrix.print("ok");
    swapBuffers();
    return;
  }
  

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
  updateMelody();

  readRotaryEncoder();
  if (pushbutton.update() && pushbutton.fallingEdge()) onButton();
  if (timerSeconds >= MS_IN_A_SECOND) {
    onTick();
    timerSeconds -= MS_IN_A_SECOND;
  }

  if (alarmActive || alarmRecentlyOff()) refreshScreen();
}

void sfxConfirm(){
  const int volume = 3;
  const int duration = 30;
  const bool background = false;
  
  toneAC(NOTE_C6, volume, duration, background);
  toneAC(NOTE_D6, volume, duration, background);
  delay(30);
  toneAC(NOTE_C6, volume, duration, background);
  toneAC(NOTE_D6, volume, duration, background);
}