#pragma once
#include "common.h"

struct UiPalette {
    D2D1_COLOR_F box, line, shadow, card, ink, dim, pill, pillHov, pillLine, off, track, acc, danger;
};

UiPalette uiPalette(bool dark);
