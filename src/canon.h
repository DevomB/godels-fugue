#ifndef CANON_H
#define CANON_H

int canon_melody_index(int voice, int time, int delay, int length);
int canon_span(int length, int delay);
int canon_source_index(int voice, int time, int delay, int length, int retrograde);

#endif
