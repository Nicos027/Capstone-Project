#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

class QLabel;
class QPushButton;
class QThread;
class Worker;
class WaveformWidget;
class MqttPublisher;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onRunStopClicked();
    void onNewReadings(double vrms, double irms, const QString& alarm);
    void onNewWaveform(const QVector<double>& voltage,
                       const QVector<double>& current);
    void onAlarmTriggered(const QString& alarmType, double vrms, double irms);
    void onWorkerFinished();
    void onErrorMessage(const QString& msg);

private:
    void buildUi();
    void setRunningVisuals(bool running);

    Ui::MainWindow *ui;

    QLabel         *vrmsValueLabel_;
    QLabel         *irmsValueLabel_;
    QLabel         *alarmLabel_;
    QLabel         *statusLabel_;
    QLabel         *mqttStatusLabel_;
    QPushButton    *runStopBtn_;
    WaveformWidget *waveform_;

    QThread       *workerThread_ = nullptr;
    Worker        *worker_       = nullptr;
    MqttPublisher *mqtt_         = nullptr;

    bool running_ = false;
    int telemetryDivider_ = 0;
};

#endif
