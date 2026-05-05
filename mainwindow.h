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
    void onNewReadings(double vrms,
                   double irms,
                   double realPower,
                   double apparentPower,
                   double powerFactor,
                   const QString& alarm);
    void onNewWaveform(const QVector<double>& voltage,
                       const QVector<double>& current);
    void onAlarmTriggered(const QString& alarmType, double vrms, double irms);
    void onWorkerFinished();
    void onErrorMessage(const QString& msg);
private:
    void buildUi();
    void setRunningVisuals(bool running);

    // Adaptive UI sizing — populated from actual screen size at startup.
    // All font sizes scale together so the layout fits any screen from
    // 800x480 (Freenove 5") up to 1920x1080+ (full desktop).
    struct UiScale {
        int valueFont;    // big colored numbers in the panels
        int labelFont;    // small panel labels and unit text
        int statusFont;   // top-bar status text
        int alarmFont;    // alarm banner text
        int buttonFont;   // RUN / STOP button text
        int topBarH;      // top status bar height (px)
        int readingsH;    // readings row height (px)
        int bottomBarH;   // bottom bar height (px)
        int runBtnW;      // run/stop button width
        int runBtnH;      // run/stop button height
        int exitBtnW;     // exit "X" button width
        int exitBtnH;     // exit "X" button height
    };
    UiScale s_;

    Ui::MainWindow *ui;
    QLabel         *vrmsValueLabel_;
    QLabel         *irmsValueLabel_;
    QLabel         *alarmLabel_;
    QLabel         *statusLabel_;
    QLabel         *mqttStatusLabel_;
    QLabel         *realPowerValue_;
    QLabel         *apparentPowerValue_;
    QLabel         *powerFactorValue_;
    QPushButton    *runStopBtn_;
    WaveformWidget *waveform_;
    QThread       *workerThread_ = nullptr;
    Worker        *worker_       = nullptr;
    MqttPublisher *mqtt_         = nullptr;
    bool running_ = false;
    int telemetryDivider_ = 0;
};
#endif
