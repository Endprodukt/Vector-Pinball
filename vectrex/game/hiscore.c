/* nicht zeitkritisch: auf dem UVMC2 auf Groesse uebersetzen, der Speicher ist knapp (das Kit baut sonst mit -O3) */
#ifdef __arm__
#pragma GCC optimize("Os")
#endif
#include "hiscore.h"
#include "settings.h"
#include "plat.h"
#include <string.h>

#define HS_FILE  "scores.bin"
#define HS_MAGIC 0x56504853u         /* "VPHS" */

static struct { uint32_t magic; char last[4]; hs_entry_t e[NUM_TABLES_MAX][HS_PLACES]; } hs;

static void clean_name(char *n)
{
    for (int i = 0; i < 3; i++) {
        char c = n[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ' ')) n[i] = ' ';
    }
    n[3] = 0;
}

void hs_load(void)
{
    if (plat_read_file(HS_FILE, (uint8_t *)&hs, sizeof hs) != sizeof hs || hs.magic != HS_MAGIC) {
        memset(&hs, 0, sizeof hs);
        hs.magic = HS_MAGIC;
        memcpy(hs.last, "AAA", 4);
    }
    clean_name(hs.last);
    for (int t = 0; t < NUM_TABLES_MAX; t++) for (int p = 0; p < HS_PLACES; p++) clean_name(hs.e[t][p].name);
}

void hs_save(void) { plat_write_file(HS_FILE, (const uint8_t *)&hs, sizeof hs); }

const hs_entry_t *hs_get(int table, int place) { return &hs.e[table][place]; }
const char *hs_last_name(void) { return hs.last; }

int hs_rank(int table, uint32_t score)
{
    if (!score || table < 0 || table >= NUM_TABLES_MAX) return -1;
    for (int p = 0; p < HS_PLACES; p++) if (score > hs.e[table][p].score) return p;
    return -1;
}

void hs_insert(int table, int place, uint32_t score, const char *name)
{
    if (place < 0 || place >= HS_PLACES) return;
    for (int p = HS_PLACES - 1; p > place; p--) hs.e[table][p] = hs.e[table][p - 1];
    hs.e[table][place].score = score;
    memcpy(hs.e[table][place].name, name, 3);
    hs.e[table][place].name[3] = 0;
    clean_name(hs.e[table][place].name);
    memcpy(hs.last, hs.e[table][place].name, 4);
    hs_save();
}
