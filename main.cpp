//
//  main.cpp
//  Power Quality Analyzer
//
//  Created by Nicos Eftychiou on 4/5/26.
//

#include "ads131m02.hpp"
#include "signal_processing.hpp"

#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

constexpr double Vref = 1.2;
constexpr double FS_Counts = 8388608.0;
constexpr int Sample_Rate = 4000;
constexpr double Nominal_Vrms = 120.0;
constexpr double Low_Limit = 108.0;
constexpr double High_Limit = 132.0;

// replace with calibrated values
constexpr double Volts_Per_Adc_Volt = 195.4;
constexpr double Amps_Per_Adc_Volt = 60.0;

double rawToAdcVolts(int32_t raw, int gain = 1) {
    return (static_cast<double>(raw) / FS_Counts) * (Vref / gain);
}

double adcToLineVolts(double adcVolts) {
    return adcVolts * Volts_Per_Adc_Volt;
}

double adcToLineAmps(double adcVolts) {
    return adcVolts * Amps_Per_Adc_Volt;
}

bool waitForDrdyFallingEdge(int gpiochip, int pin, int timeoutUs = DRDY_TIMEOUT_US) {
    int last = lgGpioRead(gpiochip, pin);
    if (last < 0) {
        return false;
    }

    const int sleepStepUs = 50;  // simple bring-up polling step
    int waited = 0;

    while (waited < timeoutUs) {
        int now = lgGpioRead(gpiochip, pin);
        if (now < 0) {
            return false;
        }

        // falling edge: 1 -> 0
        if (last == 1 && now == 0) {
            return true;
        }

        last = now;
        std::this_thread::sleep_for(std::chrono::microseconds(sleepStepUs));
        waited += sleepStepUs;
    }

    return false; // timeout
}

int main() {
    // create ADC object
    ADS131M02 adc("/dev/spidev0.0", 1000000);
    
    // open ADC
    if (!adc.openDevice()) {
        return 1;
    }
    // configure ADC (placeholder)
    if (!adc.configure()) {
        cerr << "ADC configuration failed/n";
        return 1;
    }
    
    const size_t cycleSamples = static_cast<size_t>(Sample_Rate / 60.0);
    // create rolling buffers
    RollingBuffer voltageBuffer(5 * cycleSamples);
    RollingBuffer currentBuffer(5 * cycleSamples);
    
    while (true) {
        // repeatedly read ADC samples
        SampleFrame frame{};
        
        if (!adc.readSample(frame)) {
            cerr << "Read failed\n";
            continue;
        }
        // convert raw ADC counts to ADC input voltage
        double vAdc = rawToAdcVolts(frame.ch0_raw, 1);
        double iAdc = rawToAdcVolts(frame.ch1_raw, 1);
        
        // convert ADC input voltage to line values
        double vLine = adcToLineVolts(vAdc);
        double iLine = adcToLineAmps(iAdc);
        
        // store samples in buffers
        voltageBuffer.push(vLine);
        currentBuffer.push(iLine);
        
        if (voltageBuffer.size() >= cycleSamples) {
            // compute RMS from one cycle
            auto vWin = voltageBuffer.latest(cycleSamples);
            auto iWin = currentBuffer.latest(cycleSamples);
            
            double vrms = computeRMS(vWin);
            double irms = computeRMS(iWin);
            
            // check alarm condition
            string alarm = "NORMAL";
            if (vrms < Low_Limit) {
                alarm = "UNDERVOLTAGE";
            } else if (vrms > High_Limit) {
                alarm = "OVERVOLTAGE";
            }
            
            cout << "Vrms: " << vrms << " Irms: " << irms << " Alarm: " << alarm << "\r" << flush;
        }
        
        this_thread::sleep_for(chrono::microseconds(250));
    }
    
    adc.closeDevice();
    return 0;
}
