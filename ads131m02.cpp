//
//  ads131m02.cpp
//  Power Quality Analyzer
//
//  ADC driver for ADS131M02 on ADC 15 Click
//

#include "ads131m02.h"

#include <iostream>
#include <chrono>
#include <cstring>
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

static constexpr uint16_t CMD_NULL   = 0x0000;
static constexpr uint16_t CMD_RESET  = 0x0011;
static constexpr uint16_t CMD_UNLOCK = 0x0655;

static void putWord24(uint8_t* buf, size_t wordIndex, uint16_t value16) {
    buf[wordIndex * 3 + 0] = static_cast<uint8_t>((value16 >> 8) & 0xFF);
    buf[wordIndex * 3 + 1] = static_cast<uint8_t>(value16 & 0xFF);
    buf[wordIndex * 3 + 2] = 0x00;
}

static uint16_t getWord16From24(const uint8_t* buf, size_t wordIndex) {
    return (static_cast<uint16_t>(buf[wordIndex * 3 + 0]) << 8) |
           (static_cast<uint16_t>(buf[wordIndex * 3 + 1]));
}

ADS131M02::ADS131M02(const string& spiDevice,
                     uint32_t speedHz,
                     int drdyGpio,
                     int cs2Gpio)
    : spiDevice_(spiDevice),
      speedHz_(speedHz),
      fd_(-1),
      drdyGpio_(drdyGpio),
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

    if (lgGpioClaimOutput(gpiochip_, 0, cs2Gpio_, 0) < 0) {
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

bool ADS131M02::setCs2Level(int level) {
    if (gpiochip_ < 0) return false;
    return lgGpioWrite(gpiochip_, cs2Gpio_, level) >= 0;
}

bool ADS131M02::waitForDrdyTransition(int timeout_us) {
    auto start = chrono::steady_clock::now();

    int level = lgGpioRead(gpiochip_, drdyGpio_);
    if (level < 0) return false;

    // If already low, wait for release
    while (level == 0) {
        auto elapsed = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_us) return false;

        usleep(2);
        level = lgGpioRead(gpiochip_, drdyGpio_);
        if (level < 0) return false;
    }

    // Wait for next asserted-low DRDY
    while (level == 1) {
        auto elapsed = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_us) return false;

        usleep(2);
        level = lgGpioRead(gpiochip_, drdyGpio_);
        if (level < 0) return false;
    }

    return true;
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

bool ADS131M02::sendCommand16(uint16_t cmd) {
    uint8_t tx[12] = {0};
    uint8_t rx[12] = {0};

    putWord24(tx, 0, cmd);
    putWord24(tx, 1, 0x0000);
    putWord24(tx, 2, 0x0000);
    putWord24(tx, 3, 0x0000);

    return transfer(tx, rx, sizeof(tx));
}

bool ADS131M02::readRegister(uint8_t addr, uint16_t& value) {
    const uint16_t rreg = static_cast<uint16_t>(0xA000 | ((addr & 0x3F) << 7));

    uint8_t tx1[12] = {0};
    uint8_t rx1[12] = {0};
    putWord24(tx1, 0, rreg);

    if (!transfer(tx1, rx1, sizeof(tx1))) {
        return false;
    }

    uint8_t tx2[12] = {0};
    uint8_t rx2[12] = {0};
    putWord24(tx2, 0, CMD_NULL);

    if (!transfer(tx2, rx2, sizeof(tx2))) {
        return false;
    }

    value = getWord16From24(rx2, 0);
    return true;
}

bool ADS131M02::writeRegister(uint8_t addr, uint16_t value) {
    const uint16_t wreg = static_cast<uint16_t>(0x6000 | ((addr & 0x3F) << 7));

    uint8_t tx[12] = {0};
    uint8_t rx[12] = {0};

    putWord24(tx, 0, wreg);
    putWord24(tx, 1, value);
    putWord24(tx, 2, 0x0000);
    putWord24(tx, 3, 0x0000);

    return transfer(tx, rx, sizeof(tx));
}

bool ADS131M02::configure() {
    // ADC 15 Click board-specific clock-control line.
    // Start with CS2 low. If behavior is still weird later, we can try high.
    if (!setCs2Level(0)) {
        cerr << "Failed to drive CS2/PWM\n";
        return false;
    }

    usleep(5000);

    if (!sendCommand16(CMD_RESET)) {
        return false;
    }
    usleep(5000);

    if (!sendCommand16(CMD_UNLOCK)) {
        return false;
    }
    usleep(1000);

    uint16_t id = 0;
    if (!readRegister(REG_ID, id)) {
        return false;
    }
    cout << "ADS131M02 ID = 0x" << hex << id << dec << "\n";

    // 24-bit mode, gain = 1, default clock settings
    if (!writeRegister(REG_MODE,  0x0510)) return false;
    if (!writeRegister(REG_CLOCK, 0x030E)) return false;
    if (!writeRegister(REG_GAIN,  0x0000)) return false;

    uint16_t mode = 0, clock = 0, gain = 0, status = 0;
    if (!readRegister(REG_MODE, mode))     return false;
    if (!readRegister(REG_CLOCK, clock))   return false;
    if (!readRegister(REG_GAIN, gain))     return false;
    if (!readRegister(REG_STATUS, status)) return false;

    cout << "MODE   = 0x" << hex << mode   << "\n";
    cout << "CLOCK  = 0x" << hex << clock  << "\n";
    cout << "GAIN   = 0x" << hex << gain   << "\n";
    cout << "STATUS = 0x" << hex << status << dec << "\n";

    return true;
}

bool ADS131M02::readSample(SampleFrame& frame) {
    if (!waitForDrdyTransition(200000)) {
        return false;
    }

    uint8_t tx[12] = {0};
    uint8_t rx[12] = {0};

    putWord24(tx, 0, CMD_NULL);

    if (!transfer(tx, rx, sizeof(tx))) {
        return false;
    }

    uint32_t w0 = (static_cast<uint32_t>(rx[0]) << 16) |
                  (static_cast<uint32_t>(rx[1]) << 8)  |
                   static_cast<uint32_t>(rx[2]);

    uint32_t w1 = (static_cast<uint32_t>(rx[3]) << 16) |
                  (static_cast<uint32_t>(rx[4]) << 8)  |
                   static_cast<uint32_t>(rx[5]);

    uint32_t w2 = (static_cast<uint32_t>(rx[6]) << 16) |
                  (static_cast<uint32_t>(rx[7]) << 8)  |
                   static_cast<uint32_t>(rx[8]);

    uint32_t w3 = (static_cast<uint32_t>(rx[9]) << 16) |
                  (static_cast<uint32_t>(rx[10]) << 8) |
                   static_cast<uint32_t>(rx[11]);

    // Light debug; remove later if too noisy
    cout << hex
         << "W0=0x" << w0
         << " W1=0x" << w1
         << " W2=0x" << w2
         << " W3=0x" << w3
         << dec << "\n";

    frame.ch0_raw = signed24(rx[3], rx[4], rx[5]);
    frame.ch1_raw = signed24(rx[6], rx[7], rx[8]);

    return true;
}
