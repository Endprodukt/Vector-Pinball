/*
 * hiscore.h - Bestenliste: je Tisch die vier besten Ergebnisse mit drei Initialen (SD-Karte: pinball/scores.bin).
 */
#ifndef HISCORE_H
#define HISCORE_H

#include <stdint.h>

#define HS_PLACES 4
typedef struct { uint32_t score; char name[4]; } hs_entry_t;

void hs_load(void);
void hs_save(void);
const hs_entry_t *hs_get(int table, int place);          /* score 0 = leer */
int  hs_rank(int table, uint32_t score);                 /* Platz 0..3, den das Ergebnis belegen wuerde, oder -1 */
void hs_insert(int table, int place, uint32_t score, const char *name);
const char *hs_last_name(void);                          /* zuletzt eingegebene Initialen als Vorschlag */

#endif
