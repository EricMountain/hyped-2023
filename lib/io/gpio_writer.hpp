#pragma once

#include <cstdint>

#include <core/types.hpp>

namespace hyped::io {

class GpioWriter {
 public:
  GpioWriter(const uint8_t pin);

  void write(const core::DigitalSignal state);
};

}  // namespace hyped::io
