#include "spi.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#if LINUX
#include <linux/spi/spidev.h>
#else
#define _IOC_SIZEBITS 14
#define SPI_IOC_MAGIC 'k'
#define SPI_IOC_WR_MODE _IOW(SPI_IOC_MAGIC, 1, std::uint8_t)
#define SPI_IOC_WR_MAX_SPEED_HZ _IOW(SPI_IOC_MAGIC, 4, std::uint32_t)
#define SPI_IOC_WR_LSB_FIRST _IOW(SPI_IOC_MAGIC, 2, std::uint8_t)
#define SPI_IOC_WR_BITS_PER_WORD _IOW(SPI_IOC_MAGIC, 3, std::uint8_t)

struct spi_ioc_transfer {
  std::uint64_t tx_buf;
  std::uint64_t rx_buf;

  std::uint32_t len;
  std::uint32_t speed_hz;

  std::uint16_t delay_usecs;
  std::uint8_t bits_per_word;
  std::uint8_t cs_change;
  std::uint8_t tx_nbits;
  std::uint8_t rx_nbits;
  std::uint16_t pad;
};

#define SPI_MSGSIZE(N)                                                                             \
  ((((N) * (sizeof(struct spi_ioc_transfer))) < (1 << _IOC_SIZEBITS))                              \
     ? ((N) * (sizeof(struct spi_ioc_transfer)))                                                   \
     : 0)
#define SPI_IOC_MESSAGE(N) _IOW(SPI_IOC_MAGIC, 0, char[SPI_MSGSIZE(N)])
#define SPI_CS_HIGH 0x04
#endif  // if LINUX

// configure SPI
#define SPI_MODE 3
#define SPI_BITS 8  // each word is 1B
#define SPI_MSBFIRST 0
#define SPI_LSBFIRST 1

#define SPI_FS 0

namespace hyped::io {

constexpr std::uint32_t kSPIAddrBase = 0x481A0000;  // 0x48030000 for SPI0
constexpr std::uint32_t kMmapSize    = 0x1000;

// define what the address space of SPI looks like
#pragma pack(1)
struct SPI_CH {        // offset
  std::uint32_t conf;  // 0x00
  std::uint32_t stat;  // 0x04
  std::uint32_t ctrl;  // 0x08
  std::uint32_t tx;    // 0x0c
  std::uint32_t rx;    // 0x10
};

#pragma pack(1)               // so that the compiler does not change layout
struct SPI_HW {               // offset
  std::uint32_t revision;     // 0x000
  std::uint32_t nope0[0x43];  // 0x004 - 0x110
  std::uint32_t sysconfig;    // 0x110
  std::uint32_t sysstatus;    // 0x114
  std::uint32_t irqstatus;    // 0x118
  std::uint32_t irqenable;    // 0x11c
  std::uint32_t nope1[2];     // 0x120 - 0x124
  std::uint32_t syst;         // 0x124
  std::uint32_t modulctr;     // 0x128
  SPI_CH ch0;                 // 0x12c - 0x140
  SPI_CH ch1;                 // 0x140 - 0x154
  SPI_CH ch2;                 // 0x154 - 0x168
  SPI_CH ch3;                 // 0x168 - 0x17c
  std::uint32_t xferlevel;    // 0x17c
};

Spi::Spi(core::ILogger &logger) : spi_fd_(-1), hw_(0), ch_(0), logger_(logger)
{
  const char device[] = "/dev/spidev0.0";  // spidev0.0 for SPI0
  spi_fd_             = open(device, O_RDWR, 0);

  if (spi_fd_ < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to open SPI device");
    return;
  }

  // set clock frequency
  setClock(Clock::k500KHz);

  std::uint8_t bits = SPI_BITS;  // need to change this value
  if (ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to set bits per word");
  }

  // set clock mode and CS active low
  std::uint8_t mode = (SPI_MODE & 0x3) & ~SPI_CS_HIGH;
  if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to set SPI mode");
  }

  // set bit order
  std::uint8_t order = SPI_MSBFIRST;
  if (ioctl(spi_fd_, SPI_IOC_WR_LSB_FIRST, &order) < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to set bit order");
  }

  bool check_init = initialise();
  if (check_init) {
    logger_.log(core::LogLevel::kDebug, "Successfully created SPI instance");
  } else {
    logger_.log(core::LogLevel::kFatal, "Failed to instantiate SPI");
  }
}

bool Spi::initialise()
{
  int fd;
  void *base;

  fd = open("/dev/mem", O_RDWR);
  if (fd < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to open /dev/mem");
    return false;
  }

  base = mmap(0, kMmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, kSPIAddrBase);
  if (base == MAP_FAILED) {
    logger_.log(core::LogLevel::kFatal, "Failed to map bank 0x%x", kSPIAddrBase);
    return false;
  }

  hw_ = reinterpret_cast<SPI_HW *>(base);
  ch_ = &hw_->ch0;

  logger_.log(core::LogLevel::kDebug, "Successfully created Mapping %d", sizeof(SPI_HW));
  return true;
}

void Spi::setClock(Clock clk)
{
  std::uint32_t data;
  switch (clk) {
    case Clock::k500KHz:
      data = 500000;
      break;
    case Clock::k1MHz:
      data = 1000000;
      break;
    case Clock::k4MHz:
      data = 4000000;
      break;
    case Clock::k16MHz:
      data = 16000000;
      break;
    case Clock::k20MHz:
      data = 20000000;
      break;
  }

  if (ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &data) < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to set clock frequency of %d", data);
  }
}

#if SPI_FS
void SPI::transfer(std::uint8_t *tx, std::uint8_t *rx, std::uint16_t len)
{
  if (spi_fd_ < 0) return;  // early exit if no spi device present
  spi_ioc_transfer message = {};

  message.tx_buf = reinterpret_cast<std::uint64_t>(tx);
  message.rx_buf = reinterpret_cast<std::uint64_t>(rx);
  message.len    = len;

  if (ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &message) < 0) {
    logger_.log(core::Loglevel::kFatal, "Failed to submit TRANSFER message");
  }
}
#else
void Spi::transfer(std::uint8_t *tx, std::uint8_t *, std::uint16_t len)
{
  if (hw_ == 0) return;  // early exit if no spi mapped

  for (std::uint16_t x = 0; x < len; x++) {
    // logger_.log("SPI_TEST","channel 0 status before: %d", 10);
    // while(!(ch0->status & 0x2));
    logger_.log(core::LogLevel::kDebug, "Status register: %x", ch_->stat);
    ch_->ctrl = ch_->ctrl | 0x1;
    ch_->conf = ch_->conf & 0xfffcffff;
    ch_->tx   = tx[x];
    logger_.log(core::LogLevel::kDebug, "Status register: %x", ch_->stat);
    logger_.log(core::LogLevel::kDebug, "Config register: %x", ch_->conf);
    logger_.log(core::LogLevel::kDebug, "Control register: %x", ch_->ctrl);

    while (!(ch_->stat & 0x1)) {
      logger_.log(core::LogLevel::kDebug, "Status register: %d", ch_->stat);
    }
    logger_.log(core::LogLevel::kDebug, "Status register: %d", ch_->stat);
    // logger_.log("SPI_TEST","Read buffer: %d", ch0->rx_buf);
    // logger_.log("SPI_TEST","channel 0 status after: %d", 10);
    // write_buffer++;
  }

#endif
}

void Spi::read(std::uint8_t addr, std::uint8_t *rx, std::uint16_t len)
{
  if (spi_fd_ < 0) return;  // early exit if no spi device present

  spi_ioc_transfer message[2] = {};

  // send address
  message[0].tx_buf = reinterpret_cast<std::uint64_t>(&addr);
  message[0].rx_buf = 0;
  message[0].len    = 1;

  // receive data
  message[1].tx_buf = 0;
  message[1].rx_buf = reinterpret_cast<std::uint64_t>(rx);
  message[1].len    = len;

  if (ioctl(spi_fd_, SPI_IOC_MESSAGE(2), message) < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to submit 2 TRANSFER messages");
  }
}

void Spi::write(std::uint8_t addr, std::uint8_t *tx, std::uint16_t len)
{
  if (spi_fd_ < 0) return;  // early exit if no spi device present

  spi_ioc_transfer message[2] = {};
  // send address
  message[0].tx_buf = reinterpret_cast<std::uint64_t>(&addr);
  message[0].rx_buf = 0;
  message[0].len    = 1;

  // write data
  message[1].tx_buf = reinterpret_cast<std::uint64_t>(tx);
  message[1].rx_buf = 0;
  message[1].len    = len;

  if (ioctl(spi_fd_, SPI_IOC_MESSAGE(2), message) < 0) {
    logger_.log(core::LogLevel::kFatal, "Failed to submit 2 TRANSFER messages");
  }
}

Spi::~Spi()
{
  if (spi_fd_ < 0) return;  // early exit if no spi device present

  close(spi_fd_);
}

}  // namespace hyped::io
