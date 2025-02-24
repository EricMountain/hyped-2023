#include "repl.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>

namespace hyped::debug {

Repl::Repl(core::ILogger &logger) : logger_(logger)
{
}

void Repl::run()
{
  while (true) {
    handleCommand();
  }
}

std::optional<std::unique_ptr<Repl>> Repl::fromFile(const std::string &path)
{
  std::ifstream input_stream(path);
  if (!input_stream.is_open()) {
    logger_.log(core::LogLevel::kFatal, "Failed to open file %s", path.c_str());
    return std::nullopt;
  }

  rapidjson::IStreamWrapper input_stream_wrapper(input_stream);
  rapidjson::Document document;
  rapidjson::ParseResult result = document.ParseStream(input_stream_wrapper);
  if (!result) {
    logger_.log(core::LogLevel::kFatal,
                "Error parsing JSON: %s",
                rapidjson::GetParseError_En(document.GetParseError()));
    return std::nullopt;
  }

  if (!document.HasMember("debugger")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'debugger' in configuration file at %s",
                path.c_str());
    return std::nullopt;
  }
  const auto debugger = document["debugger"].GetObject();
  auto repl           = std::make_unique<Repl>(logger_);
  repl->addHelpCommand();
  repl->addQuitCommand();

  if (!debugger.HasMember("io")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'debugger.io' in configuration file at %s",
                path.c_str());
    return std::nullopt;
  }
  const auto io = debugger["io"].GetObject();

  if (!io.HasMember("adc")) {
    logger_.log(core::LogLevel::kFatal, "Missing required field 'io.adc' in configuration file");
    return std::nullopt;
  }
  const auto adc = io["adc"].GetObject();
  if (!adc.HasMember("enabled")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'io.adc.enabled' in configuration file");
    return std::nullopt;
  }
  if (adc["enabled"].GetBool()) {
    if (!adc.HasMember("pins")) {
      logger_.log(core::LogLevel::kFatal,
                  "Missing required field 'io.adc.pins' in configuration file");
      return std::nullopt;
    }
    const auto pins = adc["pins"].GetArray();
    for (auto &pin : pins) {
      repl->addAdcCommands(pin.GetUint());
    }
  }

  if (!io.HasMember("i2c")) {
    logger_.log(core::LogLevel::kFatal, "Missing required field 'io.i2c' in configuration file");
    return std::nullopt;
  }
  const auto i2c = io["i2c"].GetObject();
  if (!i2c.HasMember("enabled")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'io.i2c.enabled' in configuration file");
    return std::nullopt;
  }
  if (i2c["enabled"].GetBool()) {
    if (!i2c.HasMember("buses")) {
      logger_.log(core::LogLevel::kFatal,
                  "Missing required field 'io.i2c.buses' in configuration file");
      return std::nullopt;
    }
  }
  const auto buses = i2c["buses"].GetArray();
  for (auto &bus : buses) {
    repl->addI2cCommands(bus.GetUint());
  }
  if (!io.HasMember("pwm")) {
    logger_.log(core::LogLevel::kFatal, "Missing required field 'io.pwm' in configuration file");
    return std::nullopt;
  }
  const auto pwm = io["pwm"].GetObject();
  if (!pwm.HasMember("enabled")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'io.pwm.enabled' in configuration file");
    return std::nullopt;
  }
  if (pwm["enabled"].GetBool()) {
    if (!pwm.HasMember("modules")) {
      logger_.log(core::LogLevel::kFatal,
                  "Missing required field 'io.pwm.modules' in configuration file");
      return std::nullopt;
    }
    const auto modules = pwm["modules"].GetArray();
    for (auto &module : modules) {
      const std::uint8_t module_id = module.GetUint();
      if (module_id > 7 || module_id < 0) {
        logger_.log(
          core::LogLevel::kFatal, "Invalid module id %d in configuration file", module_id);
        return std::nullopt;
      }
      repl->addPwmCommands(module_id);
    }
  }
  if (!io.HasMember("spi")) {
    logger_.log(core::LogLevel::kFatal, "Missing required field 'io.spi' in configuration file");
    return std::nullopt;
  }
  const auto spi = io["spi"].GetObject();
  if (!spi.HasMember("enabled")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'io.spi.enabled' in configuration file");
    return std::nullopt;
  }
  if (spi["enabled"].GetBool()) {
    if (!spi.HasMember("buses")) {
      logger_.log(core::LogLevel::kFatal,
                  "Missing required field 'io.spi.buses' in configuration file");
      return std::nullopt;
    }
    const auto buses = spi["buses"].GetArray();
    for (auto &bus : buses) {
      repl->addSpiCommands(bus.GetUint());
    }
  }
  if (!io.HasMember("uart")) {
    logger_.log(core::LogLevel::kFatal, "Missing required field 'io.uart' in configuration file");
    return std::nullopt;
  }
  const auto uart = io["uart"].GetObject();
  if (!uart.HasMember("enabled")) {
    logger_.log(core::LogLevel::kFatal,
                "Missing required field 'io.uart.enabled' in configuration file");
    return std::nullopt;
  }
  if (uart["enabled"].GetBool()) {
    if (!uart.HasMember("buses")) {
      logger_.log(core::LogLevel::kFatal,
                  "Missing required field 'io.uart.buses' in configuration file");
      return std::nullopt;
    }
    const auto buses = uart["buses"].GetArray();
    for (auto &bus : buses) {
      repl->addUartCommands(bus.GetUint());
    }
  }
  return repl;
}

void Repl::printCommands()
{
  logger_.log(core::LogLevel::kInfo, "Available commands:");
  for (const auto &[name, command] : command_map_) {
    logger_.log(core::LogLevel::kInfo, "  %s: %s", name.c_str(), command.description.c_str());
  }
}

void Repl::handleCommand()
{
  std::cout << "> ";
  std::string command;
  std::getline(std::cin, command);
  auto nameAndCommand = command_map_.find(command);
  if (nameAndCommand == command_map_.end()) {
    logger_.log(core::LogLevel::kFatal, "Unknown command: %s", command.c_str());
    return;
  }
  nameAndCommand->second.handler();
}

void Repl::addCommand(const Command &cmd)
{
  command_map_.emplace(cmd.name, cmd);
  logger_.log(core::LogLevel::kDebug, "Added command: %s", cmd.name.c_str());
}

void Repl::addQuitCommand()
{
  addCommand(Command{"quit", "Quit the REPL", [this]() { exit(0); }});
}

void Repl::addHelpCommand()
{
  addCommand(Command{"help", "Print this help message", [this]() { printCommands(); }});
}

void Repl::addAdcCommands(const std::uint8_t pin)
{
  const auto optional_adc = io::Adc::create(logger_, pin);
  if (!optional_adc) {
    logger_.log(core::LogLevel::kFatal, "Failed to create ADC instance on pin %d", pin);
    return;
  }
  const auto adc = std::make_shared<io::Adc>(*optional_adc);
  Command adc_read_command;
  std::stringstream identifier;
  identifier << "adc " << static_cast<int>(pin) << " read";
  adc_read_command.name = identifier.str();
  std::stringstream description;
  description << "Read from ADC pin " << static_cast<int>(pin);
  adc_read_command.description = description.str();
  adc_read_command.handler     = [this, adc, pin]() {
    const auto value = adc->readValue();
    if (value) {
      logger_.log(core::LogLevel::kDebug, "ADC value from pin %d: %d", pin, *value);
    } else {
      logger_.log(core::LogLevel::kFatal, "Failed to read from ADC pin %d", pin);
    }
  };
  addCommand(adc_read_command);
}

void Repl::addI2cCommands(const std::uint8_t bus)
{
  const auto optional_i2c = io::HardwareI2c::create(logger_, bus);
  if (!optional_i2c) {
    logger_.log(core::LogLevel::kFatal, "Failed to create I2C instance on bus %d", bus);
    return;
  }
  const auto i2c = std::make_shared<io::HardwareI2c>(*optional_i2c);
  {
    Command i2c_read_command;
    std::stringstream identifier;
    identifier << "i2c " << static_cast<int>(bus) << " read";
    i2c_read_command.name = identifier.str();
    std::stringstream description;
    description << "Read from I2C bus " << static_cast<int>(bus);
    i2c_read_command.description = description.str();
    i2c_read_command.handler     = [this, i2c, bus]() {
      std::uint8_t device_address, register_address;
      std::cout << "Device address: ";
      std::cin >> device_address;
      std::cout << "Register address: ";
      std::cin >> register_address;
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      const auto value = i2c->readByte(device_address, register_address);
      if (value) {
        logger_.log(core::LogLevel::kDebug, "I2C value from bus %d: %d", bus, *value);
      } else {
        logger_.log(core::LogLevel::kFatal, "Failed to read from I2C bus %d", bus);
      }
    };
    addCommand(i2c_read_command);
  }
  {
    Command i2c_write_command;
    std::stringstream identifier;
    identifier << "i2c " << static_cast<int>(bus) << " write";
    i2c_write_command.name = identifier.str();
    std::stringstream description;
    description << "Write to I2C bus " << static_cast<int>(bus);
    i2c_write_command.description = description.str();
    i2c_write_command.handler     = [this, i2c, bus]() {
      std::uint32_t device_address, register_address, data;
      std::cout << "Device address: ";
      std::cin >> std::hex >> device_address;
      std::cout << "Register address: ";
      std::cin >> std::hex >> register_address;
      std::cout << "Data: ";
      std::cin >> std::hex >> data;
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      const core::Result result = i2c->writeByteToRegister(device_address, register_address, data);
      if (result == core::Result::kSuccess) {
        logger_.log(
          core::LogLevel::kDebug, "I2C write successful to device %d on %d", device_address, bus);
      } else {
        logger_.log(core::LogLevel::kFatal, "Failed to write to I2C bus: %d", bus);
      }
    };
    addCommand(i2c_write_command);
  }
}

void Repl::addPwmCommands(const std::uint8_t module)
{
  const io::PwmModule pwm_module = static_cast<io::PwmModule>(module);
  const auto optional_pwm        = io::Pwm::create(logger_, pwm_module);
  if (!optional_pwm) {
    logger_.log(core::LogLevel::kFatal, "Failed to create PWM module");
    return;
  }
  const auto pwm = std::make_shared<io::Pwm>(optional_pwm.value());
  {
    Command pwm_run_command;
    std::stringstream identifier;
    identifier << "pwm " << static_cast<int>(pwm_module) << " run";
    pwm_run_command.name = identifier.str();
    std::stringstream description;
    description << "Run PWM module: " << static_cast<int>(pwm_module);
    pwm_run_command.description = description.str();
    pwm_run_command.handler     = [this, pwm, pwm_module]() {
      std::uint32_t period;
      std::cout << "Period: ";
      std::cin >> period;
      core::Float duty_cycle;
      std::cout << "Duty cycle: ";
      std::cin >> duty_cycle;
      std::uint8_t polarity;
      std::cout << "Polarity (0 for active high and 1 for active low): ";
      std::cin >> polarity;
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      const core::Result period_set_result = pwm->setPeriod(period);
      if (period_set_result == core::Result::kFailure) {
        logger_.log(core::LogLevel::kFatal, "Failed to set PWM period");
        return;
      }
      const core::Result duty_cycle_set_result = pwm->setDutyCycleByPercentage(duty_cycle);
      if (duty_cycle_set_result == core::Result::kFailure) {
        logger_.log(core::LogLevel::kFatal, "Failed to set PWM duty cycle");
        return;
      }
      const core::Result polarity_set_result
        = pwm->setPolarity(static_cast<io::Polarity>(polarity));
      if (polarity_set_result == core::Result::kFailure) {
        logger_.log(core::LogLevel::kFatal, "Failed to set PWM polarity");
        return;
      }
      const core::Result enable_result = pwm->setMode(io::Mode::kRun);
      if (enable_result == core::Result::kFailure) {
        logger_.log(core::LogLevel::kFatal, "Failed to enable PWM module");
        return;
      }
    };
    addCommand(pwm_run_command);
  }
  {
    Command pwm_stop_command;
    std::stringstream identifier;
    identifier << "pwm " << static_cast<int>(pwm_module) << " stop";
    pwm_stop_command.name = identifier.str();
    std::stringstream description;
    description << "Stop PWM module: " << static_cast<int>(pwm_module);
    pwm_stop_command.description = description.str();
    pwm_stop_command.handler     = [this, pwm, pwm_module]() {
      const core::Result disable_result = pwm->setMode(io::Mode::kStop);
      if (disable_result == core::Result::kFailure) {
        logger_.log(core::LogLevel::kFatal, "Failed to stop PWM module");
        return;
      }
    };
    addCommand(pwm_stop_command);
  }
}

void Repl::addSpiCommands(const std::uint8_t bus)
{
  const auto optional_spi = io::HardwareSpi::create(logger_);
  if (!optional_spi) {
    logger_.log(core::LogLevel::kFatal, "Failed to create I2C instance on bus %d", bus);
    return;
  }
  const auto spi = std::make_shared<io::HardwareSpi>(*optional_spi);
  {
    Command spi_read_byte_command;
    std::stringstream identifier;
    identifier << "spi " << static_cast<int>(bus) << " read byte";
    spi_read_byte_command.name = identifier.str();
    std::stringstream description;
    description << "Read from SPI bus " << static_cast<int>(bus);
    spi_read_byte_command.description = description.str();
    spi_read_byte_command.handler     = [this, spi, bus]() {
      std::uint8_t register_address;
      std::cout << "Register address: ";
      std::cin >> register_address;
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::uint8_t read_buffer;
      const core::Result result = spi->read(register_address, &read_buffer, 1);
      if (result == core::Result::kSuccess) {
        logger_.log(core::LogLevel::kDebug, "SPI value from bus %d: %d", bus, read_buffer);
      } else {
        logger_.log(core::LogLevel::kFatal, "Failed to read from SPI bus %d", bus);
      }
    };
    addCommand(spi_read_byte_command);
  }
  {
    Command spi_write_byte_command;
    std::stringstream identifier;
    identifier << "spi " << static_cast<int>(bus) << " write byte";
    spi_write_byte_command.name = identifier.str();
    std::stringstream description;
    description << "Write to SPI bus " << static_cast<int>(bus);
    spi_write_byte_command.description = description.str();
    spi_write_byte_command.handler     = [this, spi, bus]() {
      std::uint32_t register_address, data;
      std::cout << "Register address: ";
      std::cin >> std::hex >> register_address;
      std::cout << "Data: ";
      std::cin >> std::hex >> data;
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      const std::uint8_t *data_ptr = reinterpret_cast<const std::uint8_t *>(&data);
      const core::Result result    = spi->write(register_address, data_ptr, 1);
      if (result == core::Result::kSuccess) {
        logger_.log(
          core::LogLevel::kDebug, "Successful SPI write to device %d on %d", register_address, bus);
      } else {
        logger_.log(core::LogLevel::kFatal, "Failed to write to SPI bus: %d", bus);
      }
    };
    addCommand(spi_write_byte_command);
  }
}

void Repl::addUartCommands(const std::uint8_t bus)
{
  const UartBus uart_bus   = static_cast<UartBus>(bus);
  const auto optional_uart = io::Uart::create(logger_, uart_bus, BaudRate::kB38400);
  if (!optional_uart) {
    logger_.log(core::LogLevel::kFatal, "Failed to create UART instance on bus %d", bus);
    return;
  }
  const auto uart = std::make_shared<io::Uart>(*optional_uart);
  {
    Command uart_read_command;
    std::stringstream identifier;
    identifier << "uart " << static_cast<int>(bus) << " read";
    uart_read_command.name = identifier.str();
    std::stringstream description;
    description << "Read from UART bus " << static_cast<int>(bus);
    uart_read_command.description = description.str();
    uart_read_command.handler     = [this, uart, bus]() {
      std::uint8_t read_buffer[255];
      const core::Result result = uart->readBytes(read_buffer, 255);
      if (result == core::Result::kSuccess) {
        logger_.log(core::LogLevel::kDebug, "UART value from bus %d: %s", bus, read_buffer);
      } else {
        logger_.log(core::LogLevel::kFatal, "Failed to read from UART bus %d", bus);
      }
    };
    addCommand(uart_read_command);
  }
  {
    Command uart_write_command;
    std::stringstream identifier;
    identifier << "uart " << static_cast<int>(bus) << " write";
    uart_write_command.name = identifier.str();
    std::stringstream description;
    description << "Write to UART bus " << static_cast<int>(bus);
    uart_write_command.description = description.str();
    uart_write_command.handler     = [this, uart, bus]() {
      std::string data;
      std::cout << "Data: ";
      std::getline(std::cin, data);
      if (data.length() > 255) {
        logger_.log(core::LogLevel::kFatal, "Data too long for UART bus: %d", bus);
        return;
      }
      const core::Result result = uart->sendBytes(data.c_str(), data.length());
      if (result == core::Result::kSuccess) {
        logger_.log(core::LogLevel::kDebug, "Successfully wrote to UART bus %d", bus);
      } else {
        logger_.log(core::LogLevel::kFatal, "Failed to write to UART bus: %d", bus);
      }
    };
    addCommand(uart_write_command);
  }
}

}  // namespace hyped::debug