#ifndef AYSYNTH_H
#define AYSYNTH_H
#include <stdint.h>
void aysynth_init(int sample_rate);
void aysynth_write(int reg, uint8_t val);        /* Register 0-13 des AY-3-8912 */
void aysynth_render(int16_t *out, int samples);  /* 16 Bit mono */
#endif
