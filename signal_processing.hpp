//
//  signal_processing.h
//  Power Quality Analyzer
//
//  Created by Nicos Eftychiou on 4/6/26.
//  Sample storage

#ifndef signal_processing_h
#define signal_processing_h


#endif /* signal_processing_h */

#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include <cstddef>

using namespace std;

class RollingBuffer {
public:
    explicit RollingBuffer(size_t maxSize);
    
    void push(double value);
    vector<double> latest(size_t count) const;
    size_t size() const;
    
private:
    deque<double> data_;
    size_t maxSize_;
};

double computeRMS(const vector<double>& x);
