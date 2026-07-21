#pragma once

#include <SDL.h>

#include "types.h"

void draw_clock(SDL_Renderer* r, int ww, int wh, const Config& cfg, bool ringing, double hold_progress);
