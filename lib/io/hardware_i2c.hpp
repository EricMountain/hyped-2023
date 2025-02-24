#pragma once

#include "i2c.hpp"

#include <core/logger.hpp>
#include <core/types.hpp>

namespace hyped::io {

class HardwareI2c : public II2c {
 public:
  /**
   * @brief Creates a HardwareI2c object and opens the file descriptor for the I2C bus.
   * @param bus_address is the address of the I2C bus on the BBB.
   */
  static std::optional<HardwareI2c> create(core::ILogger &logger, const std::uint8_t bus_address);
  ~HardwareI2c();

  virtual std::optional<std::uint8_t> readByte(const std::uint8_t device_address,
                                               const std::uint8_t register_address);
  virtual core::Result writeByteToRegister(const std::uint8_t device_address,
                                           const std::uint8_t register_address,
                                           const std::uint8_t data);
  virtual core::Result writeByte(const std::uint8_t device_address, const std::uint8_t data);

 private:
  HardwareI2c(core::ILogger &logger, const int file_descriptor);
  void setSensorAddress(const std::uint8_t device_address);

 private:
  core::ILogger &logger_;
  std::uint8_t sensor_address_;
  const int file_descriptor_;
};

}  // namespace hyped::io
