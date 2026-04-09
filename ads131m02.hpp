//
//  ads131m02.hpp
//  Power Quality Analyzer
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

struct SampleFrame {
    uint16_t status;
    int32_t ch0_raw;
    int32_t ch1_raw;
};

class ADS131M02 {
public:
    ADS131M02(const std::string& spiDevice,
              uint32_t speedHz,
              int drdyGpio,
              int rstGpio,
              int cs2Gpio);

    ~ADS131M02();

    bool openDevice();
    void closeDevice();

    bool configure();
    bool readSample(SampleFrame& frame);

private:
    std::string spiDevice_;
    uint32_t speedHz_;
    int fd_;

    int drdyGpio_;
    int rstGpio_;
    int cs2Gpio_;
    int gpiochip_;

    bool initBoardGpio();
    void closeBoardGpio();

    bool setRstLevel(int level);
    bool setCs2Level(int level);
    bool waitForDrdyLow(int timeout_us);

    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len);
    int32_t signed24(uint8_t b0, uint8_t b1, uint8_t b2);

    bool sendCommand16(uint16_t cmd);
    bool readRegister(uint8_t addr, uint16_t& value);
    bool writeRegister(uint8_t addr, uint16_t value);

    uint16_t crc16_ccitt(const uint8_t* data, size_t len);
    int32_t bufToVal24(const uint8_t* startByte);

    bool setWordLen24();
    bool setGainCh0(uint8_t gain);
    bool setGainCh1(uint8_t gain);
};
