#include "music.h"
#include "toneAC.h"

int *melody;
int melodySize;
int index;

// change this to make the song slower or faster
int tempo = 144;

// this calculates the duration of a whole note in ms
int wholenote = (60000 * 4) / tempo;
int noteDuration = 0;
int volume = 1;

elapsedMillis musicTimer;

void updateMusic(){
  if (musicTimer < (int) noteDuration) return;
  if (melody == NULL) return;

  musicTimer -= noteDuration;
  
  // calculates the duration of each note
  int divider = melody[index + 1];

  if (divider > 0) {
    // regular note, just proceed
    noteDuration = (wholenote) / divider;
  } else if (divider < 0) {
    // dotted notes are represented with negative durations!!
    noteDuration = (wholenote) / abs(divider);
    noteDuration *= 1.5; // increases the duration in half for dotted notes
  }

  // we only play the note for 90% of the duration, leaving 10% as a pause
  if (melody[index] == REST) noToneAC();
  else toneAC(melody[index], volume, noteDuration * 0.9, true);

  // if we're at the end of the song, stop
  if (index + 2 >= melodySize * 2) melody = NULL;

  // move the playhead to the next note
  index += 2;
}

void playMelody(int* newMelody, int newSize) {
  melody = newMelody;
  melodySize = newSize;
  index = 0;
}