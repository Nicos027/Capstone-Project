//
//  signal_processing.cpp
//  Power Quality Analyzer
//
//  Created by Nicos Eftychiou on 4/6/26.
//  Sample storage

#include "signal_processing.hpp"

using namespace std;

// stores recent samples
RollingBuffer::RollingBuffer(size_t maxSize) : maxSize_(maxSize) {}

void RollingBuffer::push(double value) {
    if (data_.size() >= maxSize_) {
        data_.pop_front();
    }
    data_.push_back(value);
}

vector<double> RollingBuffer::latest(size_t count) const {
    if (count > data_.size()) {
        count = data_.size();
    }
    return vector<double>(data_.end() - count, data_.end());
}

size_t RollingBuffer::size() const {
    return data_.size();
}

// calculates RMS from block of samples
// square each sample
// average them
// take sqrt
double computeRMS(const vector<double>& x) {
    if (x.empty()) return 0.0;
    
    double sumSq = 0.0;
    for (double v : x) {
        sumSq += v * v;
    }
    return sqrt(sumSq / static_cast<double>(x.size()));
}
double computeMean(const vector<double>& x) {
    if (x.empty()) return 0.0;

    double sum = 0.0;
    for (double v : x) {
        sum += v;
    }

    return sum / static_cast<double>(x.size());
}

double computeACRMS(const vector<double>& x) {
    if (x.empty()) return 0.0;

    double mean = computeMean(x);

    double sumSq = 0.0;
    for (double v : x) {
        double centered = v - mean;
        sumSq += centered * centered;
    }

    return sqrt(sumSq / static_cast<double>(x.size()));
}

double computeMeanProduct(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0;
    }

    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += a[i] * b[i];
    }

    return sum / static_cast<double>(a.size());
}

double computeApparentPower(double vrms, double irms) {
    return vrms * irms;
}

double computePowerFactor(double realPower, double apparentPower) {
    if (apparentPower == 0.0) {
        return 0.0;
    }

    return realPower / apparentPower;
}
