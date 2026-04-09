//
//  ads131m02.cpp
//  Power Quality Analyzer
//

#include "ads131m02.hpp"

#include <iostream>
#include <cmath>
#include <chrono>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <lgpio.h>

using namespace std;

// ADS131M02 commands
static constexpr uint16_t ADC15_CMD_NULL    = 0x0000;
static constexpr uint16_t ADC15_CMD_RESET   = 0x0011;
static constexpr uint16_t ADC15_CMD_UNLOCK  = 0x0655;
static constexpr uint16_t ADC15_CMD_RREG    = 0xA000;
static constexpr uint16_t ADC15_CMD_WREG    = 0x6000;

// ADS131M02 registers
static constexpr uint8_t ADC15_REG_ID     = 0x00;
static constexpr uint8_t ADC15_REG_STATUS = 0x01;
static constexpr uint8_t ADC15_REG_MODE   = 0x02;
static constexpr uint8_t ADC15_REG_CLOCK  = 0x03;
static constexpr uint8_t ADC15_REG_GAIN   = 0x04;

// Word lengths
static constexpr uint8_t LEN16BITS = 2;
static constexpr uint8_t LEN24BITS = 3;
static constexpr uint8_t LEN32BITS = 4;

// Gain enums from MikroE
static constexpr uint8_t ADC15_GAIN1   = 0;
static constexpr uint8_t ADC15_GAIN2   = 1;
static constexpr uint8_t ADC15_GAIN4   = 2;
static constexpr uint8_t ADC15_GAIN8   = 3;
static constexpr uint8_t ADC15_GAIN16  = 4;
static constexpr uint8_t ADC15_GAIN32  = 5;
static constexpr uint8_t ADC15_GAIN64  = 6;
static constexpr uint8_t ADC15_GAIN128 = 7;

// Channel offsets in gain register
static constexpr uint8_t ADC15_CHANNEL1 = 0;
static constexpr uint8_t ADC15_CHANNEL2 = 4;

// Frequency config constants for LTC6903
static constexpr double OCT_DIVIDER    = 1039.0;
static constexpr double OCT_RES        = 3.322;
static constexpr double DAC_OFFSET     = 2048.0;
static constexpr double DAC_RES        = 2078.0;
static constexpr int    DAC_OCT_OFFSET = 10;

static constexpr uint8_t ADC15_LTC_CFG_POWER_ON = 2;

// CRC constants
static constexpr uint16_t CRC_INIT_VAL = 0xFFFF;
static constexpr uint16_t CRC_POLYNOM  = 0x1021;

ADS131M02::ADS131M02(const std::string& spiDevice,
                     uint32_t speedHz,
                     int drdyGpio,
                     int cs2Gpio,
                     int rstGpio)
    : spiDevice_(spiDevice),
      speedHz_(speedHz),
      fd_(-1),
      drdyGpio_(drdyGpio),
      cs2Gpio_(cs2Gpio),
      rstGpio_(rstGpio),
      gpiochip_(-1),
      wordLenBytes_(LEN24BITS),
      resolution_(1u << 24),
      gain1_(1),
      gain2_(1) {}

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

    if (lgGpioClaimOutput(gpiochip_, 0, cs2Gpio_, 1) < 0) {
        cerr << "Failed to claim CS2 GPIO\n";
        return false;
    }

    if (lgGpioClaimOutput(gpiochip_, 0, rstGpio_, 1) < 0) {
        cerr << "Failed to claim RST GPIO\n";
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

bool ADS131M02::setRstLevel(int level) {
    if (gpiochip_ < 0) return false;
    return lgGpioWrite(gpiochip_, rstGpio_, level) >= 0;
}

bool ADS131M02::dataReady() const {
    if (gpiochip_ < 0) return false;
    int level = lgGpioRead(gpiochip_, drdyGpio_);
    return level == 0;
}

bool ADS131M02::waitForDataReadyLow(int timeout_us) {
    auto start = chrono::steady_clock::now();

    while (!dataReady()) {
        auto elapsed = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - start).count();

        if (elapsed > timeout_us) {
            return false;
        }

        usleep(10);
    }

    return true;
}

bool ADS131M02::setSpiMode(uint8_t mode) {
    return ioctl(fd_, SPI_IOC_WR_MODE, &mode) >= 0;
}

bool ADS131M02::openDevice() {
    fd_ = open(spiDevice_.c_str(), O_RDWR);
    if (fd_ < 0) {
        cerr << "Failed to open SPI device: " << spiDevice_ << "\n";
        return false;
    }

    uint8_t mode = SPI_MODE_1;
    uint8_t bits = 8;

    if (!setSpiMode(mode)) {
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

bool ADS131M02::writeBytes(const uint8_t* tx, size_t len) {
    std::vector<uint8_t> rx(len, 0);
    return transfer(tx, rx.data(), len);
}

bool ADS131M02::readBytes(uint8_t* rx, size_t len) {
    std::vector<uint8_t> tx(len, 0);
    return transfer(tx.data(), rx, len);
}

uint16_t ADS131M02::crc16ccitt(const uint8_t* data, int count) const {
    uint16_t crc = CRC_INIT_VAL;

    while (--count >= 0) {
        crc = crc ^ (static_cast<uint16_t>(*data++) << 8);
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ CRC_POLYNOM);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }

    return crc;
}

void ADS131M02::bufToVal(const uint8_t* startByte, int32_t& outputData) const {
    switch (wordLenBytes_) {
        case LEN16BITS:
            outputData = ((static_cast<int32_t>(startByte[0]) << 24) |
                          (static_cast<int32_t>(startByte[1]) << 16));
            outputData >>= 16;
            break;

        case LEN24BITS:
            outputData = ((static_cast<int32_t>(startByte[0]) << 24) |
                          (static_cast<int32_t>(startByte[1]) << 16) |
                          (static_cast<int32_t>(startByte[2]) << 8));
            outputData >>= 8;
            break;

        case LEN32BITS:
            outputData = ((static_cast<int32_t>(startByte[0]) << 24) |
                          (static_cast<int32_t>(startByte[1]) << 16) |
                          (static_cast<int32_t>(startByte[2]) << 8)  |
                           static_cast<int32_t>(startByte[3]));
            break;

        default:
            outputData = 0;
            break;
    }
}

void ADS131M02::valToBuf(int32_t inputData, uint8_t* startByte) const {
    switch (wordLenBytes_) {
        case LEN16BITS:
            startByte[0] = static_cast<uint8_t>(inputData >> 8);
            startByte[1] = static_cast<uint8_t>(inputData);
            break;

        case LEN24BITS:
            startByte[0] = static_cast<uint8_t>(inputData >> 16);
            startByte[1] = static_cast<uint8_t>(inputData >> 8);
            startByte[2] = static_cast<uint8_t>(inputData);
            break;

        case LEN32BITS:
            startByte[0] = static_cast<uint8_t>(inputData >> 24);
            startByte[1] = static_cast<uint8_t>(inputData >> 16);
            startByte[2] = static_cast<uint8_t>(inputData >> 8);
            startByte[3] = static_cast<uint8_t>(inputData);
            break;

        default:
            break;
    }
}

bool ADS131M02::ltcWrite(uint8_t oct, uint16_t dac, uint8_t cfg) {
    uint8_t oldMode = SPI_MODE_1;
    if (!setSpiMode(SPI_MODE_0)) {
        return false;
    }

    uint16_t tempData = 0;
    tempData |= static_cast<uint16_t>(oct & 0xF) << 12;
    tempData |= static_cast<uint16_t>(dac & 0x3FF) << 2;
    tempData |= static_cast<uint16_t>(cfg & 0x3);

    uint8_t txData[2] = {
        static_cast<uint8_t>(tempData >> 8),
        static_cast<uint8_t>(tempData)
    };

    if (!setCs2Level(0)) return false;
    bool ok = writeBytes(txData, 2);
    if (!setCs2Level(1)) return false;

    if (!setSpiMode(oldMode)) {
        return false;
    }

    return ok;
}

bool ADS131M02::setFrequency(uint32_t frequencyHz) {
    uint8_t oct = static_cast<uint8_t>(OCT_RES * log10(frequencyHz / OCT_DIVIDER));
    uint16_t dac = static_cast<uint16_t>(
        DAC_OFFSET - ((DAC_RES * pow(2.0, DAC_OCT_OFFSET + oct)) / static_cast<double>(frequencyHz))
    );
    return ltcWrite(oct, dac, ADC15_LTC_CFG_POWER_ON);
}

bool ADS131M02::regWrite(uint8_t reg, uint16_t data) {
    uint8_t tempBuf[16] = {0};

    int32_t tempData = ADC15_CMD_WREG | ((static_cast<uint16_t>(reg & 0x3F)) << 7);
    tempData <<= (8 * (wordLenBytes_ - 2));
    valToBuf(tempData, tempBuf);

    tempData = static_cast<int32_t>(data) << (8 * (wordLenBytes_ - 2));
    valToBuf(tempData, &tempBuf[wordLenBytes_]);

    tempData = crc16ccitt(tempBuf, wordLenBytes_ * 2);
    tempData <<= (8 * (wordLenBytes_ - 2));
    valToBuf(tempData, &tempBuf[wordLenBytes_ * 2]);

    return writeBytes(tempBuf, wordLenBytes_ * 3);
}

bool ADS131M02::regRead(uint8_t reg, uint16_t& data) {
    uint8_t tempBuf[16] = {0};

    int32_t tempData = ADC15_CMD_RREG | ((static_cast<uint16_t>(reg & 0x3F)) << 7);
    tempData <<= (8 * (wordLenBytes_ - 2));
    valToBuf(tempData, tempBuf);

    tempData = crc16ccitt(tempBuf, wordLenBytes_);
    tempData <<= (8 * (wordLenBytes_ - 2));
    valToBuf(tempData, &tempBuf[wordLenBytes_]);

    if (!writeBytes(tempBuf, wordLenBytes_ * 2)) {
        return false;
    }

    uint8_t rx[wordLenBytes_];
    if (!readBytes(rx, wordLenBytes_)) {
        return false;
    }

    bufToVal(rx, tempData);
    data = static_cast<uint16_t>(tempData >> (8 * (wordLenBytes_ - 2)));
    return true;
}

bool ADS131M02::setGain(uint8_t channelOffset, uint8_t gainEnum) {
    uint16_t currentGain = 0;
    if (!regRead(ADC15_REG_GAIN, currentGain)) {
        return false;
    }

    currentGain &= ~(ADC15_GAIN128 << channelOffset);
    currentGain |= (gainEnum << channelOffset);

    if (!regWrite(ADC15_REG_GAIN, currentGain)) {
        return false;
    }

    if (channelOffset == ADC15_CHANNEL2) {
        gain2_ = static_cast<uint8_t>(pow(2.0, gainEnum) + 0.1);
    } else {
        gain1_ = static_cast<uint8_t>(pow(2.0, gainEnum) + 0.1);
    }

    return true;
}

bool ADS131M02::setWordLen24() {
    uint16_t modeReg = 0;
    if (!regRead(ADC15_REG_MODE, modeReg)) {
        return false;
    }

    modeReg &= ~(3 << 8);
    modeReg |= (1 << 8); // 24-bit word length enum from MikroE driver

    if (!regWrite(ADC15_REG_MODE, modeReg)) {
        return false;
    }

    wordLenBytes_ = LEN24BITS;
    resolution_ = static_cast<uint32_t>(pow(2.0, wordLenBytes_ * 8) + 0.1);
    return true;
}

bool ADS131M02::configure() {
    // MikroE-style board init:
    // 1) program LTC6903 clock generator
    // 2) pulse RST
    // 3) set word length and gain

    if (!setFrequency(8192000)) {
        cerr << "Failed to program LTC6903\n";
        return false;
    }

    if (!setRstLevel(0)) return false;
    usleep(1000);
    if (!setRstLevel(1)) return false;
    usleep(100000);

    if (!setWordLen24()) {
        cerr << "Failed to set word length\n";
        return false;
    }

    if (!setGain(ADC15_CHANNEL1, ADC15_GAIN1)) {
        cerr << "Failed to set CH1 gain\n";
        return false;
    }

    if (!setGain(ADC15_CHANNEL2, ADC15_GAIN1)) {
        cerr << "Failed to set CH2 gain\n";
        return false;
    }

    uint16_t id = 0, mode = 0, clock = 0, gain = 0, status = 0;
    if (!regRead(ADC15_REG_ID, id)) return false;
    if (!regRead(ADC15_REG_MODE, mode)) return false;
    if (!regRead(ADC15_REG_CLOCK, clock)) return false;
    if (!regRead(ADC15_REG_GAIN, gain)) return false;
    if (!regRead(ADC15_REG_STATUS, status)) return false;

    cout << "ADS131M02 ID = 0x" << hex << id << dec << "\n";
    cout << "MODE   = 0x" << hex << mode << "\n";
    cout << "CLOCK  = 0x" << hex << clock << "\n";
    cout << "GAIN   = 0x" << hex << gain << "\n";
    cout << "STATUS = 0x" << hex << status << dec << "\n";

    return true;
}

bool ADS131M02::readSample(SampleFrame& frame) {
    if (!waitForDataReadyLow(200000)) {
        return false;
    }

    uint8_t outBuf[16] = {0};
    if (!readBytes(outBuf, wordLenBytes_ * 4)) {
        return false;
    }

    int32_t tempData = 0;
    int32_t ch1 = 0;
    int32_t ch2 = 0;

    bufToVal(outBuf, tempData);
    frame.status = static_cast<uint16_t>(tempData >> (8 * (wordLenBytes_ - 2)));

    bufToVal(&outBuf[wordLenBytes_], ch1);
    bufToVal(&outBuf[wordLenBytes_ * 2], ch2);

    bufToVal(&outBuf[wordLenBytes_ * 3], tempData);
    uint16_t crcVal = static_cast<uint16_t>(tempData >> (8 * (wordLenBytes_ - 2)));
    uint16_t calcCrc = crc16ccitt(outBuf, wordLenBytes_ * 3);

    if (crcVal != calcCrc) {
        return false;
    }

    frame.ch0_raw = ch1;
    frame.ch1_raw = ch2;
    return true;
}
