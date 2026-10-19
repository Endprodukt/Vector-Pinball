/*
 * mp3.c - der MP3-Dekoder fuer eigene Lieder: minimp3 (lieff, gemeinfrei/CC0, third_party/minimp3/minimp3.h).
 *
 * Nur Layer 3, ohne SIMD. Der Arbeitsbereich des Dekoders (rund 16 KB) liegt im Original auf dem Stapel; auf dem UVMC2 hat der
 * Stapel 2 KB, darum ist er hier statisch (MINIMP3_SCRATCH_STORAGE, die einzige Aenderung an minimp3.h). Es gibt deshalb
 * genau einen Dekoder zur gleichen Zeit - mehr braucht das Spiel nicht.
 */
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_SCRATCH_STORAGE static
#define MINIMP3_IMPLEMENTATION
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "minimp3.h"
