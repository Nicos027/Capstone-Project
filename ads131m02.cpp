//
//  ads131m02.cpp
//  Power Quality Analyzer
//

#include "ads131m02.hpp"

#include <iostream>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <lgpio.h>

using namespace std;

static constexpr uint8_t REG_ID     = 0x00;
static constexpr uint8_t REG_STATUS = 0x01;
static constexpr uint8_t REG_MODE   = 0x02;
static constexpr uint8_t REG_CLOCK  = 0x03;
static constexpr uint8_t REG_GAIN   = 0x04;

static constexpr uint16_t CMD_RESET  = 0x0011;
static constexpr uint16_t CMD_UNLOCK = 0x0655;
static constexpr uint16_t CMD_RREG   = 0xA000;
static constexpr uint16_t CMD_WREG   = 0x6000;

static constexpr uint16_t CRC_INIT_VAL = 0xFFFF;
static constexpr uint16_t CRC_POLYNOM  = 0x1021;

ADS131M02::ADS131M02(const string& spiDevice,
                     uint32_t speedHz,
                     int drdyGpio,
                     int rstGpio,
                     int cs2Gpio)
    : spiDevice_(spiDevice),
      speedHz_(speedHz),
      fd_(-1),
      drdyGpio_(drdyGpio),
      rstGpio_(rstGpio),
      cs2Gpio_(cs2Gpio),
      gpiochip_(-1) {}

ADS131M02::~ADS131M02() {
    closeDevice();
}

bool ADS131M02::initBoardGpio() {
    gpiochip_ = lgGpiochipOpen(0);
    if (gpiochip_ < 0) {
        cerr << "Failed to open gpiochip\n";
        return false;
    }

    if (lgGpioClaimInput(gpiochip_, 0, drdyGpio_) < 0) {
        cerr << "Failed to claim DRDY GPIO\n";
        return false;
    }

    if (lgGpioClaimOutput(gpiochip_, 0, rstGpio_, 1) < 0) {
        cerr << "Failed to claim RST GPIO\n";
        return false;
    }

    if (lgGpioClaimOutput(gpiochip_, 0, cs2Gpio_, 1) < 0) {
        cerr << "Failed to claim CS2 GPIO\n";
        return false;
    }

    return true;
}

void ADS131M02::closeBoardGpio() {
    if (gpiochip_ >= 0) {
        lgGpiochipClose(gpiochip_);
        gpiochip_ = -1;
    }
}

bool ADS131M02::setRstLevel(int level) {
    if (gpiochip_ < 0) return false;
    return lgGpioWrite(gpiochip_, rstGpio_, level) >= 0;
}

bool ADS131M02::setCs2Level(int level) {
    if (gpiochip_ < 0) return false;
    return lgGpioWrite(gpiochip_, cs2Gpio_, level) >= 0;
}

bool ADS131M02::waitForDrdyLow(int timeout_us) {
    auto start = chrono::steady_clock::now();

    while (true) {
        int level = lgGpioRead(gpiochip_, drdyGpio_);
        if (level < 0) {
            return false;
        }

        if (level == 0) {
            return true;
        }

        auto elapsed = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - start).count();

        if (elapsed > timeout_us) {
            return false;
        }

        usleep(10);
    }
}

bool ADS131M02::openDevice() {
    fd_ = open(spiDevice_.c_str(), O_RDWR);
    if (fd_ < 0) {
        cerr << "Failed to open SPI device: " << spiDevice_ << "\n";
        return false;
    }

    uint8_t mode = SPI_MODE_1;
    uint8_t bits = 8;

    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) {
        cerr << "Failed to set SPI mode\n";
        return false;
    }

    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        cerr << "Failed to set bits per word\n";
        return false;
    }

    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speedHz_) < 0) {
        cerr << "Failed to set SPI speed\n";
        return false;
    }

    if (!initBoardGpio()) {
        return false;
    }

    return true;
}

void ADS131M02::closeDevice() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    closeBoardGpio();
}

bool ADS131M02::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = static_cast<uint32_t>(len);
    tr.speed_hz = speedHz_;
    tr.bits_per_word = 8;

    return ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) >= 0;
}

int32_t ADS131M02::signed24(uint8_t b0, uint8_t b1, uint8_t b2) {
    int32_t value = (static_cast<int32_t>(b0) << 16) |
                    (static_cast<int32_t>(b1) << 8)  |
                     static_cast<int32_t>(b2);

    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    return value;
}

int32_t ADS131M02::bufToVal24(const uint8_t* startByte) {
    int32_t value = (static_cast<int32_t>(startByte[0]) << 24) |
                    (static_cast<int32_t>(startByte[1]) << 16) |
                    (static_cast<int32_t>(startByte[2]) << 8);
    value >>= 8;
    return value;
}

uint16_t ADS131M02::crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = CRC_INIT_VAL;

    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ CRC_POLYNOM);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool ADS131M02::sendCommand16(uint16_t cmd) {
    uint8_t tx[6] = {0};
    uint8_t rx[6] = {0};

    tx[0] = static_cast<uint8_t>(cmd >> 8);
    tx[1] = static_cast<uint8_t>(cmd);
    tx[2] = 0x00;

    uint16_t crc = crc16_ccitt(tx, 3);
    tx[3] = static_cast<uint8_t>(crc >> 8);
    tx[4] = static_cast<uint8_t>(crc);
    tx[5] = 0x00;

    return transfer(tx, rx, sizeof(tx));
}

bool ADS131M02::readRegister(uint8_t addr, uint16_t& value) {
    uint8_t tx[6] = {0};
    uint8_t rx[6] = {0};

    uint16_t cmd = static_cast<uint16_t>(CMD_RREG | ((addr & 0x3F) << 7));
    tx[0] = static_cast<uint8_t>(cmd >> 8);
    tx[1] = static_cast<uint8_t>(cmd);
    tx[2] = 0x00;

    uint16_t crc = crc16_ccitt(tx, 3);
    tx[3] = static_cast<uint8_t>(crc >> 8);
    tx[4] = static_cast<uint8_t>(crc);
    tx[5] = 0x00;

    if (!transfer(tx, rx, sizeof(tx))) {
        return false;
    }

    uint8_t rxReg[3] = {0};
    uint8_t txDummy[3] = {0};
    if (!transfer(txDummy, rxReg, sizeof(rxReg))) {
        return false;
    }

    int32_t temp = bufToVal24(rxReg);
    value = static_cast<uint16_t>(temp >> 8);
    return true;
}

bool ADS131M02::writeRegister(uint8_t addr, uint16_t value) {
    uint8_t tx[9] = {0};
    uint8_t rx[9] = {0};

    uint16_t cmd = static_cast<uint16_t>(CMD_WREG | ((addr & 0x3F) << 7));
    tx[0] = static_cast<uint8_t>(cmd >> 8);
    tx[1] = static_cast<uint8_t>(cmd);
    tx[2] = 0x00;

    tx[3] = static_cast<uint8_t>(value >> 8);
    tx[4] = static_cast<uint8_t>(
