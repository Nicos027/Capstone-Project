#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "worker.h"
#include "waveformwidget.h"
#include "mqttpublisher.h"
#include "config.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QThread>
#include <QWidget>
#include <QApplication>
#include <QDialog>
#include <QPalette>
#include <QColor>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    buildUi();
    setRunningVisuals(false);

    // Connect to HiveMQ Cloud broker using values from config.h
    mqtt_ = new MqttPublisher(this);
    using namespace VoltWatchConfig;
    if (mqtt_->connectToBroker(MQTT_BROKER,
                                MQTT_PORT,
                                CLIENT_ID,
                                MQTT_USERNAME,
                                MQTT_PASSWORD,
                                TOPIC_PREFIX)) {
        mqttStatusLabel_->setText(QString("MQTT: Connected [%1]").arg(DEVICE_ID));
        mqttStatusLabel_->setStyleSheet("color: #C0DD97; font-size: 11px;");
    } else {
        mqttStatusLabel_->setText("MQTT: Disconnected");
        mqttStatusLabel_->setStyleSheet("color: #F09595; font-size: 11px;");
    }
}

MainWindow::~MainWindow()
{
    if (worker_) worker_->stop();
    if (workerThread_) {
        workerThread_->quit();
        workerThread_->wait(2000);
    }
    delete ui;
}

void MainWindow::buildUi()
{
    setWindowTitle("VoltWatch");

    setStyleSheet(
        "QMainWindow { background: #1a1a1a; }"
        "QWidget { color: #e8e8e8; font-family: Arial, sans-serif; }"
    );

    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Top status bar ---
    QWidget *topBar = new QWidget(central);
    topBar->setFixedHeight(36);
    topBar->setStyleSheet("background: #262626; border-bottom: 1px solid #404040;");

    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(12, 0, 6, 0);

    statusLabel_ = new QLabel("STOPPED", topBar);
    statusLabel_->setStyleSheet("color: #bbb; font-size: 13px; font-weight: bold;");
    topLayout->addWidget(statusLabel_);
    topLayout->addStretch();

    mqttStatusLabel_ = new QLabel("MQTT: --", topBar);
    mqttStatusLabel_->setStyleSheet("color: #888; font-size: 11px;");
    topLayout->addWidget(mqttStatusLabel_);

    QLabel *titleLabel = new QLabel("  VoltWatch PQA  ", topBar);
    titleLabel->setStyleSheet("color: #888; font-size: 12px;");
    topLayout->addWidget(titleLabel);

    QPushButton *exitBtn = new QPushButton("X", topBar);
    exitBtn->setFixedSize(30, 26);
    exitBtn->setStyleSheet(
        "QPushButton { background: #1a1a1a; color: #aaa; border: 1px solid #555; "
        "border-radius: 4px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #791F1F; color: #FCEBEB; "
        "border: 1px solid #E24B4A; }"
    );
    connect(exitBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    topLayout->addWidget(exitBtn);

    mainLayout->addWidget(topBar);

    // --- Readings row ---
    QWidget *readingsRow = new QWidget(central);
    readingsRow->setFixedHeight(140);
    QHBoxLayout *readingsLayout = new QHBoxLayout(readingsRow);
    readingsLayout->setContentsMargins(0, 0, 0, 0);
    readingsLayout->setSpacing(1);

    auto makeReadingPanel = [&](const QString& label,
                                 const QString& unit,
                                 const QString& color,
                                 QLabel*& outValueLabel)
    {
        QWidget *panel = new QWidget(readingsRow);
        panel->setStyleSheet("background: #1a1a1a;");
        QVBoxLayout *pl = new QVBoxLayout(panel);
        pl->setContentsMargins(20, 10, 20, 10);
        pl->setSpacing(2);

        QLabel *lbl = new QLabel(label, panel);
        lbl->setStyleSheet("color: #888; font-size: 12px; letter-spacing: 1px;");
        pl->addWidget(lbl);

        outValueLabel = new QLabel("---.--", panel);
        outValueLabel->setStyleSheet(
            QString("color: %1; font-size: 72px; font-weight: bold; "
                    "font-family: 'Courier New', monospace;").arg(color)
        );
        pl->addWidget(outValueLabel);

        QLabel *unitLbl = new QLabel(unit, panel);
        unitLbl->setStyleSheet("color: #666; font-size: 12px;");
        pl->addWidget(unitLbl);

        return panel;
    };

    QWidget *vPanel  = makeReadingPanel("V RMS", "volts", "#E24B4A", vrmsValueLabel_);
    QWidget *iPanel  = makeReadingPanel("I RMS", "amps",  "#EF9F27", irmsValueLabel_);
    QWidget *pPanel  = makeReadingPanel("REAL POWER", "W",   "#4CAF50", realPowerValue_);
    QWidget *sPanel  = makeReadingPanel("APPARENT POWER", "VA", "#42A5F5", apparentPowerValue_);
    QWidget *pfPanel = makeReadingPanel("POWER FACTOR", "",    "#AB47BC", powerFactorValue_);
    
    readingsLayout->addWidget(vPanel);
    readingsLayout->addWidget(iPanel);
    readingsLayout->addWidget(pPanel);
    readingsLayout->addWidget(sPanel);
    readingsLayout->addWidget(pfPanel);

    QWidget *divider = new QWidget(readingsRow);
    divider->setFixedWidth(1);
    divider->setStyleSheet("background: #404040;");
    readingsLayout->addWidget(divider);

    readingsLayout->addWidget(iPanel, 1);

    mainLayout->addWidget(readingsRow);

    // --- Waveform ---
    waveform_ = new WaveformWidget(central);
    mainLayout->addWidget(waveform_, 1);

    // --- Bottom bar: RUN button + alarm banner ---
    QWidget *bottomBar = new QWidget(central);
    bottomBar->setFixedHeight(90);
    bottomBar->setStyleSheet("background: #262626; border-top: 1px solid #404040;");

    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(12, 10, 12, 10);
    bottomLayout->setSpacing(12);

    runStopBtn_ = new QPushButton("RUN", bottomBar);
    runStopBtn_->setFixedSize(160, 70);
    bottomLayout->addWidget(runStopBtn_);

    alarmLabel_ = new QLabel("Press RUN to start measurement", bottomBar);
    alarmLabel_->setAlignment(Qt::AlignCenter);
    alarmLabel_->setWordWrap(true);
    alarmLabel_->setStyleSheet(
        "background: #0d0d0d; border: 1px solid #404040; border-radius: 6px; "
        "color: #888; font-size: 18px;"
    );
    bottomLayout->addWidget(alarmLabel_, 1);

    mainLayout->addWidget(bottomBar);

    setCentralWidget(central);

    connect(runStopBtn_, &QPushButton::clicked,
            this, &MainWindow::onRunStopClicked);
}

void MainWindow::setRunningVisuals(bool running)
{
    running_ = running;

    if (running) {
        runStopBtn_->setText("STOP");
        runStopBtn_->setStyleSheet(
            "QPushButton { background: #791F1F; border: 2px solid #E24B4A; "
            "border-radius: 10px; color: #FCEBEB; "
            "font-size: 22px; font-weight: bold; letter-spacing: 2px; }"
            "QPushButton:hover { background: #8f2424; }"
        );
        statusLabel_->setText("RUNNING");
        statusLabel_->setStyleSheet("color: #C0DD97; font-size: 13px; font-weight: bold;");
    } else {
        runStopBtn_->setText("RUN");
        runStopBtn_->setStyleSheet(
            "QPushButton { background: #3B6D11; border: 2px solid #97C459; "
            "border-radius: 10px; color: #EAF3DE; "
            "font-size: 22px; font-weight: bold; letter-spacing: 2px; }"
            "QPushButton:hover { background: #4a8817; }"
        );
        statusLabel_->setText("STOPPED");
        statusLabel_->setStyleSheet("color: #bbb; font-size: 13px; font-weight: bold;");

        vrmsValueLabel_->setText("---.--");
        irmsValueLabel_->setText("---.--");
        alarmLabel_->setText("Press RUN to start measurement");
        alarmLabel_->setStyleSheet(
            "background: #0d0d0d; border: 1px solid #404040; border-radius: 6px; "
            "color: #888; font-size: 18px;"
        );
    }
}

void MainWindow::onRunStopClicked()
{
    if (!running_) {
        workerThread_ = new QThread(this);
        worker_ = new Worker();
        worker_->moveToThread(workerThread_);

        connect(workerThread_, &QThread::started, worker_, &Worker::run);
        connect(worker_, &Worker::newReadings, this, &MainWindow::onNewReadings);
        connect(worker_, &Worker::newWaveform, this, &MainWindow::onNewWaveform);
        connect(worker_, &Worker::alarmTriggered, this, &MainWindow::onAlarmTriggered);
        connect(worker_, &Worker::finished, this, &MainWindow::onWorkerFinished);
        connect(worker_, &Worker::errorMessage, this, &MainWindow::onErrorMessage);
        connect(worker_, &Worker::finished, workerThread_, &QThread::quit);
        connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);

        workerThread_->start();
        setRunningVisuals(true);
    } else {
        if (worker_) worker_->stop();
    }
}

void MainWindow::onNewReadings(double vrms,
                               double irms,
                               double realPower,
                               double apparentPower,
                               double powerFactor,
                               const QString& alarm)
{
    vrmsValueLabel_->setText(QString::number(vrms, 'f', 1));
    irmsValueLabel_->setText(QString::number(irms, 'f', 2));
    realPowerValue_->setText(QString::number(realPower, 'f', 1));
    apparentPowerValue_->setText(QString::number(apparentPower, 'f', 1));
    powerFactorValue_->setText(QString::number(powerFactor, 'f', 3));

    if (alarm == "NORMAL") {
        alarmLabel_->setText("NORMAL");
        alarmLabel_->setStyleSheet(
            "background: #173404; border: 1px solid #3B6D11; "
            "border-radius: 6px; color: #C0DD97; "
            "font-size: 22px; font-weight: bold;"
        );
    } else if (alarm == "UNDERVOLTAGE") {
        alarmLabel_->setText(QString("UNDERVOLTAGE -- %1 V").arg(vrms, 0, 'f', 1));
        alarmLabel_->setStyleSheet(
            "background: #501313; border: 1px solid #E24B4A; "
            "border-radius: 6px; color: #FCEBEB; "
            "font-size: 22px; font-weight: bold;"
        );
    } else if (alarm == "OVERVOLTAGE") {
        alarmLabel_->setText(QString("OVERVOLTAGE -- %1 V | RELAY OPEN").arg(vrms, 0, 'f', 1));
        alarmLabel_->setStyleSheet(
            "background: #501313; border: 1px solid #E24B4A; "
            "border-radius: 6px; color: #FCEBEB; "
            "font-size: 20px; font-weight: bold;"
        );
    } else if (alarm == "OVERCURRENT") {
        alarmLabel_->setText(QString("OVERCURRENT -- %1 A | RELAY OPEN").arg(irms, 0, 'f', 2));
        alarmLabel_->setStyleSheet(
            "background: #501313; border: 1px solid #E24B4A; "
            "border-radius: 6px; color: #FCEBEB; "
            "font-size: 20px; font-weight: bold;"
        );
    }

    if (mqtt_ && mqtt_->isConnected()) {
        if (++telemetryDivider_ >= 40) {
            telemetryDivider_ = 0;
            mqtt_->publishReading(vrms, irms, alarm);
        }
    }
}

void MainWindow::onNewWaveform(const QVector<double>& voltage,
                                const QVector<double>& current)
{
    waveform_->setData(voltage, current);
}

void MainWindow::onAlarmTriggered(const QString& alarmType, double vrms, double irms)
{
    if (mqtt_ && mqtt_->isConnected()) {
        mqtt_->publishAlarm(alarmType, vrms, irms);
    }
}

void MainWindow::onWorkerFinished()
{
    worker_ = nullptr;
    workerThread_ = nullptr;
    setRunningVisuals(false);
}

void MainWindow::onErrorMessage(const QString& msg)
{
    QDialog dialog(this);
    dialog.setWindowTitle("VoltWatch Error");
    dialog.setModal(true);
    dialog.setFixedSize(360, 160);

    QPalette dlgPal = dialog.palette();
    dlgPal.setColor(QPalette::Window, QColor("#1a1a1a"));
    dlgPal.setColor(QPalette::WindowText, QColor("#FCEBEB"));
    dialog.setPalette(dlgPal);
    dialog.setAutoFillBackground(true);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    QLabel *msgLabel = new QLabel(msg, &dialog);
    msgLabel->setAlignment(Qt::AlignCenter);
    msgLabel->setWordWrap(true);
    QPalette lblPal = msgLabel->palette();
    lblPal.setColor(QPalette::WindowText, QColor("#FCEBEB"));
    msgLabel->setPalette(lblPal);
    QFont lblFont = msgLabel->font();
    lblFont.setPointSize(13);
    lblFont.setBold(true);
    msgLabel->setFont(lblFont);
    layout->addWidget(msgLabel);

    QPushButton *okBtn = new QPushButton("OK", &dialog);
    okBtn->setFixedSize(100, 44);
    okBtn->setAutoFillBackground(true);

    QPalette btnPal = okBtn->palette();
    btnPal.setColor(QPalette::Button, QColor("#3B6D11"));
    btnPal.setColor(QPalette::ButtonText, QColor("#FFFFFF"));
    okBtn->setPalette(btnPal);

    QFont btnFont = okBtn->font();
    btnFont.setPointSize(13);
    btnFont.setBold(true);
    okBtn->setFont(btnFont);

    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    dialog.exec();
}
