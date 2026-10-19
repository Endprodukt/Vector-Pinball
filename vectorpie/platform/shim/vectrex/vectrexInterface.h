/*
 * Ersatz fuer die Kopfdatei des PiTrex-Vertrags: vectrex/game/sound.c bindet sie ein und schreibt damit auf den Klangchip
 * der Vectrex. Den gibt es hier nicht (plat_audio_has_chip() = 0) - der Aufruf wird nie erreicht und tut nichts.
 */
#ifndef VPIE_VECTREX_INTERFACE_SHIM_H
#define VPIE_VECTREX_INTERFACE_SHIM_H

#include <stdint.h>

static inline void v_writePSG(uint8_t reg, uint8_t val) { (void)reg; (void)val; }

#endif
