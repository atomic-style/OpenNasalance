// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "atomic_wave.h"

#include <math.h>

static const char *TAG = "atomic_wave";

const float pi = 3.1415927f;
const float pi_2 = 1.5707963f;

float a_wave_sin(float amplitude, uint64_t current, uint64_t max) {
    float phase = (float)current / (float)max;
    return amplitude * sinf(phase * 2.0f * pi);
}
