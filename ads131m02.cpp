//
//  ads131m02.cpp
//  Power Quality Analyzer
//
//  Created by Nicos Eftychiou on 4/5/26.
//  ADC driver

#include "ads131m02.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

using namespace std;

ADS131M02::ADS131M02(const string& spiDevice, uint32_t speedHz)  : spiDevice_(spiDevice), speedHz_(speedHz), fd_(-1) {}

ADS131M02::~ADS131M02() {
    closeDevice();
}

// opens Pi's SPI interface and configures
bool ADS131M02::openDevice() {
    fd_ = open(spiDevice_.c_str(), O_RDWR);
    if (fd_ < 0) {
        cerr << "Failed to opem SPI device:" << spiDevice_ << "\n";
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
    
    return true;
}

    void ADS131M02::closeDevice() {
        if (fd_ >= 0) {
            close (fd_);
            fd_ = -1;
        }
}

// sends bytes to the ADC and reads bytes back
bool ADS131M02::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
spi_ioc_transfer tr{};
tr.tx_buf = reinterpret_cast<unsigned long>(tx);
tr.rx_buf = reinterpret_cast<unsigned long>(rx);
tr.len = static_cast<uint32_t>(len);
tr.speed_hz = speedHz_;
tr.bits_per_word = 8;

return ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) >=0;
}

// ADC gives data as 3 bytes per channel
// combines 3 bytes into single signed number
int32_t ADS131M02::signed24(uint8_t b0, uint8_t b1, uint8_t b2) {
    int32_t value = (static_cast<int32_t>(b0) << 16) |
                    (static_cast<int32_t>(b1) << 8)  |
    static_cast<int32_t>(b2);
    
    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    return value;
}

bool ADS131M02::configure() {
    // placeholder
    return true;
}

// reads one frame from ADC and extracts
// ch0_raw (raw voltage sample)
// ch1_raw (raw current sample)
bool ADS131M02::readSample(SampleFrame& frame) {
    // placeholder 12 byte frame
    uint8_t tx[12] {0};
    uint8_t rx[12] {0};
    
    if (!transfer(tx, rx, sizeof(tx))) {
        return false;
 }
    
    frame.ch0_raw = signed24(rx[3], rx[4], rx[5]);
    frame.ch1_raw = signed24(rx[6], rx[7], rx[8]);
    
    return true;
}
