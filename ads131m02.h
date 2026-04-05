//
//  ads131m02.h
//  Power Quality Analyzer
//
//  Created by Nicos Eftychiou on 4/5/26.
//

#ifndef ads131m02_h
#define ads131m02_h


#endif /* ads131m02_h */

#pragma once

#include <csdint>
#include <string>

namespace std

struct SampleFrame {
    int32_t ch0_raw;
    int32_t ch1_raw;
};

class ADS131M02 {
public:
    ADS131M02(const string& spiDevice, iunt32_t speedHz);
    ~ADS131M02();
    
    bool openDevice();
    void closeDevice();
    
    bool configure();
    bool readSample(SampleFrame& frame);
    
private:
    string spiDevice_;
    uint32_t speedHz_;
    int fd_;
    
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len);
    int32_t signed24(unit8_t b0, uint8_t b1, uint8_t b2);
};
