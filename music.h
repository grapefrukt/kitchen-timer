#ifndef music_h
#define music_h

#include "pitches.h"

typedef struct {
    int* data;
    int size;
    int tempo;
} Melody;


void playMelody(Melody melody);
void updateMusic();

#endif