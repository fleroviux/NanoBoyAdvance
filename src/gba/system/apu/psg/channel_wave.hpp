/**
  * Copyright (C) 2019 fleroviux (Frederic Meyer)
  *
  * This file is part of NanoboyAdvance.
  *
  * NanoboyAdvance is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * NanoboyAdvance is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with NanoboyAdvance. If not, see <http://www.gnu.org/licenses/>.
  */

#pragma once

#include "sequencer.hpp"

#include <cstdint>

namespace GameBoyAdvance {

class WaveChannel {

public:
  WaveChannel(Scheduler& scheduler);
  
  void Reset();
  
  void Generate();
  auto Read (int offset) -> std::uint8_t;
  void Write(int offset, std::uint8_t value);
  
  void WriteSample(int offset, std::uint8_t value) {
    wave_ram[wave_bank ^ 1][offset] = value;
  }
  
  std::int8_t sample = 0;
  
private:
  constexpr int GetSynthesisIntervalFromFrequency(int frequency) {
    // 8 cycles equals 2097152 Hz, the highest possible sample rate.
    return 8 * (2048 - frequency);
  }
  
  Event event { 0, [this]() { this->Generate(); } };
  
  Sequencer sequencer;
  
  bool enabled;
  bool force_volume;
  int  volume;
  int  frequency;
  int  dimension;
  int  wave_bank;
  int  length_max;
  bool length_enable;
  
  std::uint8_t wave_ram[2][16];
  
  int phase;
};

}