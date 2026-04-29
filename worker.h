#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <QThread>
#include <QVector>
#include <QString>
#include <atomic>

class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr);
    ~Worker();

    void stop();

public slots:
    void run();

signals:
    void newReadings(double vrms,
                 double irms,
                 double realPower,
                 double apparentPower,
                 double powerFactor,
                 const QString& alarm);
    void newWaveform(const QVector<double>& voltage,
                     const QVector<double>& current);
    void alarmTriggered(const QString& alarmType, double vrms, double irms);
    void finished();
    void errorMessage(const QString& msg);

private:
    std::atomic<bool> running_;
};

#endif
