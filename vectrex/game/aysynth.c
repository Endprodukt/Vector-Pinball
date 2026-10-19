/*
 * aysynth.c - einfache Nachbildung des Klangchips der Vectrex (AY-3-8912, Takt 1,5 MHz): drei Rechtecktoene,
 * ein Rauschgenerator, feste Lautstaerken. Dient dem Host-Simulator und der Buchse des UVMC2, damit dort
 * dieselben Effekte zu hoeren sind wie aus dem Lautsprecher der Konsole. Von der Huellkurve des Chips ist nur die Form
 * nachgebildet, die das Spiel benutzt: der fallende Saegezahn in Schleife (Register 13 = 8). Steht ein Kanal auf
 * Huellkurve (Lautstaerke 16) und sind Ton und Rauschen im Mischer aus, ist sein Ausgang die Huellkurve selbst - ein
 * Saegezahn mit der Frequenz Takt / (256 * Periode), der klassische Brummbass dieses Chips.
 */
#include "aysynth.h"

#define AY_CLOCK 1500000
static uint8_t  reg[16];
static uint32_t rate = 32000;
static uint32_t phase[3], nphase, lfsr = 1, ephase;
static uint8_t  out[3], nout;
static int32_t  lp;

static const int16_t VOL[16] = { 0, 60, 88, 126, 180, 256, 364, 516, 730, 1036, 1468, 2080, 2948, 4176, 5916, 8380 };

void aysynth_init(int sample_rate) { rate = (uint32_t)sample_rate; lfsr = 1; }
void aysynth_write(int r, uint8_t v) { if (r >= 0 && r < 16) reg[r] = v; }

void aysynth_render(int16_t *dst, int n)
{
    uint32_t inc[3], ninc;
    for (int c = 0; c < 3; c++) {
        uint32_t p = (uint32_t)reg[2 * c] | ((uint32_t)(reg[2 * c + 1] & 15) << 8);
        if (p < 1) p = 1;
        /* Halbperiode = 8 * p Takte; Zuwachs je Abtastwert in 1/65536 Halbperiode */
        inc[c] = (uint32_t)(((uint64_t)AY_CLOCK << 16) / ((uint64_t)8 * p * rate));
    }
    {
        uint32_t np = reg[6] & 31;
        if (np < 1) np = 1;
        ninc = (uint32_t)(((uint64_t)AY_CLOCK << 16) / ((uint64_t)16 * np * rate));
    }
    uint32_t ep = (uint32_t)reg[11] | ((uint32_t)reg[12] << 8), einc;
    if (ep < 1) ep = 1;
    einc = (uint32_t)(((uint64_t)AY_CLOCK << 16) / ((uint64_t)16 * ep * rate));     /* 16 Stufen je Durchlauf, in 1/65536 Stufe */
    for (int i = 0; i < n; i++) {
        int32_t mix = 0;
        ephase += einc;
        const int elev = 15 - (int)((ephase >> 16) & 15);
        nphase += ninc;
        while (nphase >= 65536) {
            nphase -= 65536;
            lfsr = (lfsr >> 1) | (((lfsr ^ (lfsr >> 3)) & 1) << 16);
            nout = (uint8_t)(lfsr & 1);
        }
        for (int c = 0; c < 3; c++) {
            phase[c] += inc[c];
            while (phase[c] >= 65536) { phase[c] -= 65536; out[c] ^= 1; }
            int tone_off = (reg[7] >> c) & 1, noise_off = (reg[7] >> (3 + c)) & 1;
            if ((out[c] | tone_off) & (nout | noise_off)) mix += VOL[(reg[8 + c] & 16) ? elev : (reg[8 + c] & 15)];
        }
        lp += (mix - lp) / 3;                                 /* weicher, wie der kleine Lautsprecher */
        dst[i] = (int16_t)(lp > 32767 ? 32767 : lp);
    }
}
