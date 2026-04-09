//
//  ads131m02.h
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
              int cs2Gpio,
              int rstGpio);

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
    int cs2Gpio_;
    int rstGpio_;
    int gpiochip_;

    uint8_t wordLenBytes_;
    uint32_t resolution_;
    uint8_t gain1_;
    uint8_t gain2_;

    bool initBoardGpio();
    void closeBoardGpio();

    bool setCs2Level(int level);
    bool setRstLevel(int level);
    bool dataReady() const;
    bool waitForDataReadyLow(int timeout_us);

    bool setSpiMode(uint8_t mode);
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len);
    bool writeBytes(const uint8_t* tx, size_t len);
    bool readBytes(uint8_t* rx, size_t len);

    uint16_t crc16ccitt(const uint8_t* data, int count) const;

    void bufToVal(const uint8_t* startByte, int32_t& outputData) const;
    void valToBuf(int32_t inputData, uint8_t* startByte) const;

    bool ltcWrite(uint8_t oct, uint16_t dac, uint8_t cfg);
    bool setFrequency(uint32_t frequencyHz);

    bool regWrite(uint8_t reg, uint16_t data);
    bool regRead(uint8_t reg, uint16_t& data);

    bool setWordLen24();
    bool setGain(uint8_t channelOffset, uint8_t gainEnum);
};
