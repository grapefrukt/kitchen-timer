#include "music.h"
#include "toneAC.h"

Melody melody;
int index = -1;

// this calculates the duration of a whole note in ms
int wholenote = 0;
int noteDuration = 0;

elapsedMillis musicTimer;

bool isPlayingMelody(){
  return index >= 0;
}

void updateMelody(){
  if ((int) musicTimer < noteDuration) return;
  if (index < 0) return;

  musicTimer -= noteDuration;
  
  // calculates the duration of each note
  int divider = melody.data[index + 1];

  if (divider > 0) {
    // regular note, just proceed
    noteDuration = (wholenote) / divider;
  } else if (divider < 0) {
    // dotted notes are represented with negative durations!!
    noteDuration = (wholenote) / abs(divider);
    noteDuration *= 1.5; // increases the duration in half for dotted notes
  }

  // we only play the note for 90% of the duration, leaving 10% as a pause
  if (melody.data[index] == REST) noToneAC();
  else toneAC(melody.data[index], melody.volume, noteDuration * .9, true);

  // if we're at the end of the song, stop
  if (index + 2 >= melody.size * 2) stopMelody();
  // move the playhead to the next note
  else index += 2;
}

void playMelody(Melody newMelody) {
  melody = newMelody;
  wholenote = (60000 * 4) / melody.tempo;
  index = 0;
  musicTimer = 0;
  noteDuration = 0;
}

void stopMelody() {
  index = -1;
}
