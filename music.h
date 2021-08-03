#ifndef music_h
#define music_h

#include "pitches.h"

typedef struct {
    const int* data;
    int size;
    int tempo;
} Melody;


void playMelody(Melody melody);
void updateMelody();
void stopMelody();

#endif