#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "worker.h"
#include "waveformwidget.h"
#include "mqttpublisher.h"
#include "alarmhistory.h"
#include "config.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QThread>
#include <QWidget>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QDialog>
#include <QPalette>
#include <QColor>
#include <QStackedWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QAbstractItemView>
#include <QDebug>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Initialize persistent alarm history database. This survives
    // reboots, network outages, and MQTT disconnections so the user
    // can always review what alarms have fired on the device.
    alarmDb_ = new AlarmHistory(this);
    if (!alarmDb_->initialize()) {
        // Non-fatal: we can still run, just without persistent history.
        qWarning() << "Alarm history database failed to initialize; "
                      "running without persistence.";
    }

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
        mqttStatusLabel_->setStyleSheet(QString("color: #C0DD97; font-size: %1px;").arg(s_.statusFont));
    } else {
        mqttStatusLabel_->setText("MQTT: Disconnected");
        mqttStatusLabel_->setStyleSheet(QString("color: #F09595; font-size: %1px;").arg(s_.statusFont));
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

    // ===== Adaptive sizing =====
    // Detect the actual screen geometry and scale every font and dimension
    // proportionally. The reference design point is 800x480 (Freenove 5"):
    // at that size we use the minimum readable fonts. Bigger screens get
    // proportionally larger fonts up to a sensible cap so the UI doesn't
    // look cartoonish on a 1920x1080 display.
    QScreen *screen = QGuiApplication::primaryScreen();
    int sw = screen ? screen->geometry().width()  : 800;
    int sh = screen ? screen->geometry().height() : 480;

    // Scale factor: 1.0 at 800x480, grows linearly up to 2.5 at 1920x1080.
    // Take the smaller of width/height ratios so we never overflow either axis.
    double rw = sw / 800.0;
    double rh = sh / 480.0;
    double k  = std::min(rw, rh);
    if (k < 1.0)  k = 1.0;
    if (k > 2.5)  k = 2.5;

    auto px = [k](int basePx) {
        return std::max(1, static_cast<int>(basePx * k + 0.5));
    };

    s_.valueFont  = px(28);
    s_.labelFont  = px(10);
    s_.statusFont = px(11);
    s_.alarmFont  = px(15);
    s_.buttonFont = px(18);
    s_.topBarH    = px(28);
    s_.readingsH  = px(80);
    s_.bottomBarH = px(56);
    s_.runBtnW    = px(110);
    s_.runBtnH    = px(44);
    s_.exitBtnW   = px(26);
    s_.exitBtnH   = px(22);

    // The main GUI lives on page 0 of a stacked widget; the alarm
    // history view lives on page 1. The History button switches pages.
    mainPage_ = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(mainPage_);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Top status bar ---
    QWidget *topBar = new QWidget(mainPage_);
    topBar->setFixedHeight(s_.topBarH);
    topBar->setStyleSheet("background: #262626; border-bottom: 1px solid #404040;");
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(12, 0, 6, 0);

    statusLabel_ = new QLabel("STOPPED", topBar);
    statusLabel_->setStyleSheet(QString("color: #bbb; font-size: %1px; font-weight: bold;").arg(s_.statusFont));
    topLayout->addWidget(statusLabel_);
    topLayout->addStretch();

    mqttStatusLabel_ = new QLabel("MQTT: --", topBar);
    mqttStatusLabel_->setStyleSheet(QString("color: #888; font-size: %1px;").arg(s_.statusFont));
    topLayout->addWidget(mqttStatusLabel_);

    QLabel *titleLabel = new QLabel("  VoltWatch PQA  ", topBar);
    titleLabel->setStyleSheet(QString("color: #888; font-size: %1px;").arg(s_.statusFont));
    topLayout->addWidget(titleLabel);

    QPushButton *exitBtn = new QPushButton("X", topBar);
    exitBtn->setFixedSize(s_.exitBtnW, s_.exitBtnH);
    exitBtn->setStyleSheet(
        QString("QPushButton { background: #1a1a1a; color: #aaa; border: 1px solid #555; "
                "border-radius: 4px; font-weight: bold; font-size: %1px; }"
                "QPushButton:hover { background: #791F1F; color: #FCEBEB; "
                "border: 1px solid #E24B4A; }").arg(s_.statusFont)
    );
    connect(exitBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    topLayout->addWidget(exitBtn);
    mainLayout->addWidget(topBar);

    // --- Readings row ---
    QWidget *readingsRow = new QWidget(mainPage_);
    readingsRow->setFixedHeight(s_.readingsH);
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
        lbl->setStyleSheet(QString("color: #888; font-size: %1px; letter-spacing: 1px;").arg(s_.labelFont));
        pl->addWidget(lbl);

        outValueLabel = new QLabel("---.--", panel);
        outValueLabel->setStyleSheet(
            QString("color: %1; font-size: %2px; font-weight: bold; "
                    "font-family: 'Courier New', monospace;").arg(color).arg(s_.valueFont)
        );
        pl->addWidget(outValueLabel);

        QLabel *unitLbl = new QLabel(unit, panel);
        unitLbl->setStyleSheet(QString("color: #666; font-size: %1px;").arg(s_.labelFont));
        pl->addWidget(unitLbl);

        return panel;
    };

    QWidget *vPanel  = makeReadingPanel("V RMS", "volts", "#E24B4A", vrmsValueLabel_);
    QWidget *iPanel  = makeReadingPanel("I RMS", "amps", "#EF9F27", irmsValueLabel_);
    QWidget *pPanel  = makeReadingPanel("REAL POWER", "W", "#4CAF50", realPowerValue_);
    QWidget *sPanel  = makeReadingPanel("APPARENT POWER", "VA", "#42A5F5", apparentPowerValue_);
    QWidget *pfPanel = makeReadingPanel("POWER FACTOR", "", "#AB47BC", powerFactorValue_);

    readingsLayout->addWidget(vPanel);
    readingsLayout->addWidget(iPanel);
    readingsLayout->addWidget(pPanel);
    readingsLayout->addWidget(sPanel);
    readingsLayout->addWidget(pfPanel);
    mainLayout->addWidget(readingsRow);

    // --- Waveform ---
    waveform_ = new WaveformWidget(mainPage_);
    mainLayout->addWidget(waveform_, 1);

    // --- Bottom bar: RUN button + alarm banner ---
    QWidget *bottomBar = new QWidget(mainPage_);
    bottomBar->setFixedHeight(s_.bottomBarH);
    bottomBar->setStyleSheet("background: #262626; border-top: 1px solid #404040;");
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(12, 10, 12, 10);
    bottomLayout->setSpacing(12);

    runStopBtn_ = new QPushButton("RUN", bottomBar);
    runStopBtn_->setFixedSize(s_.runBtnW, s_.runBtnH);
    bottomLayout->addWidget(runStopBtn_);

    alarmLabel_ = new QLabel("Press RUN to start measurement", bottomBar);
    alarmLabel_->setAlignment(Qt::AlignCenter);
    alarmLabel_->setWordWrap(true);
    alarmLabel_->setStyleSheet(
        QString("background: #0d0d0d; border: 1px solid #404040; border-radius: 6px; "
                "color: #888; font-size: %1px;").arg(s_.alarmFont)
    );
    bottomLayout->addWidget(alarmLabel_, 1);

    // Alarms (history) button — sits to the right of the alarm banner.
    // Width is intentionally narrower than RUN/STOP so the alarm banner
    // remains the dominant element on the bottom bar.
    historyBtn_ = new QPushButton("ALARMS", bottomBar);
    historyBtn_->setFixedSize(s_.runBtnW, s_.runBtnH);
    historyBtn_->setStyleSheet(QString(
        "QPushButton { background: #262626; color: #C0DD97; "
        "border: 2px solid #3B6D11; border-radius: 10px; "
        "font-size: %1px; font-weight: bold; letter-spacing: 1px; }"
        "QPushButton:hover { background: #173404; color: #EAF3DE; }"
    ).arg(s_.buttonFont));
    bottomLayout->addWidget(historyBtn_);

    mainLayout->addWidget(bottomBar);

    // Build the history page (page 1). buildHistoryPage uses s_, so it
    // must run AFTER the adaptive scaling block above has populated s_.
    buildHistoryPage();

    // Stack the two pages and use the stack as the central widget.
    pageStack_ = new QStackedWidget(this);
    pageStack_->addWidget(mainPage_);     // index 0
    pageStack_->addWidget(historyPage_);  // index 1
    setCentralWidget(pageStack_);

    connect(runStopBtn_, &QPushButton::clicked,
            this, &MainWindow::onRunStopClicked);
    connect(historyBtn_, &QPushButton::clicked,
            this, &MainWindow::onShowHistoryClicked);
}

void MainWindow::setRunningVisuals(bool running)
{
    running_ = running;
    if (running) {
        runStopBtn_->setText("STOP");
        runStopBtn_->setStyleSheet(
            QString("QPushButton { background: #791F1F; border: 2px solid #E24B4A; "
                    "border-radius: 10px; color: #FCEBEB; "
                    "font-size: %1px; font-weight: bold; letter-spacing: 2px; }"
                    "QPushButton:hover { background: #8f2424; }").arg(s_.buttonFont)
        );
        statusLabel_->setText("RUNNING");
        statusLabel_->setStyleSheet(QString("color: #C0DD97; font-size: %1px; font-weight: bold;").arg(s_.statusFont));
    } else {
        runStopBtn_->setText("RUN");
        runStopBtn_->setStyleSheet(
            QString("QPushButton { background: #3B6D11; border: 2px solid #97C459; "
                    "border-radius: 10px; color: #EAF3DE; "
                    "font-size: %1px; font-weight: bold; letter-spacing: 2px; }"
                    "QPushButton:hover { background: #4a8817; }").arg(s_.buttonFont)
        );
        statusLabel_->setText("STOPPED");
        statusLabel_->setStyleSheet(QString("color: #bbb; font-size: %1px; font-weight: bold;").arg(s_.statusFont));

        vrmsValueLabel_->setText("---.--");
        irmsValueLabel_->setText("---.--");
        realPowerValue_->setText("---.--");
        apparentPowerValue_->setText("---.--");
        powerFactorValue_->setText("---.--");
        alarmLabel_->setText("Press RUN to start measurement");
        alarmLabel_->setStyleSheet(
            QString("background: #0d0d0d; border: 1px solid #404040; border-radius: 6px; "
                    "color: #888; font-size: %1px;").arg(s_.alarmFont)
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
            QString("background: #173404; border: 1px solid #3B6D11; "
                    "border-radius: 6px; color: #C0DD97; "
                    "font-size: %1px; font-weight: bold;").arg(s_.alarmFont)
        );
    } else if (alarm == "UNDERVOLTAGE_WARN") {
        alarmLabel_->setText(QString("UNDERVOLTAGE WARNING -- %1 V").arg(vrms, 0, 'f', 1));
        alarmLabel_->setStyleSheet(
            QString("background: #4F3A00; border: 1px solid #EF9F27; "
                    "border-radius: 6px; color: #FFE8B3; "
                    "font-size: %1px; font-weight: bold;").arg(s_.alarmFont)
        );
    } else if (alarm == "UNDERVOLTAGE_TRIP") {
        alarmLabel_->setText("UNDERVOLTAGE | RELAY OPEN");
        alarmLabel_->setStyleSheet(
            QString("background: #501313; border: 1px solid #E24B4A; "
                    "border-radius: 6px; color: #FCEBEB; "
                    "font-size: %1px; font-weight: bold;").arg(s_.alarmFont)
        );
    } else if (alarm == "OVERVOLTAGE") {
        alarmLabel_->setText("OVERVOLTAGE | RELAY OPEN");
        alarmLabel_->setStyleSheet(
            QString("background: #501313; border: 1px solid #E24B4A; "
                    "border-radius: 6px; color: #FCEBEB; "
                    "font-size: %1px; font-weight: bold;").arg(s_.alarmFont)
        );
    } else if (alarm == "OVERCURRENT") {
        alarmLabel_->setText("OVERCURRENT | RELAY OPEN");
        alarmLabel_->setStyleSheet(
            QString("background: #501313; border: 1px solid #E24B4A; "
                    "border-radius: 6px; color: #FCEBEB; "
                    "font-size: %1px; font-weight: bold;").arg(s_.alarmFont)
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
    // Always record the alarm to the local database first. This survives
    // power loss, network outages, and MQTT failures, so the user can
    // always review what happened on this device.
    if (alarmDb_) {
        alarmDb_->recordAlarm(alarmType, vrms, irms);
    }

    // If the history view is currently visible, refresh it so the new
    // alarm shows up at the top immediately.
    if (pageStack_ && pageStack_->currentWidget() == historyPage_) {
        refreshHistoryList();
    }

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

void MainWindow::buildHistoryPage()
{
    historyPage_ = new QWidget(this);
    historyPage_->setStyleSheet("background: #1a1a1a;");

    QVBoxLayout *layout = new QVBoxLayout(historyPage_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Header bar with title + Back button ---
    QWidget *headerBar = new QWidget(historyPage_);
    headerBar->setFixedHeight(s_.topBarH * 2);
    headerBar->setStyleSheet("background: #262626; border-bottom: 1px solid #404040;");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(12, 6, 12, 6);
    headerLayout->setSpacing(12);

    QPushButton *backBtn = new QPushButton("< BACK", headerBar);
    backBtn->setFixedSize(s_.runBtnW, s_.runBtnH);
    backBtn->setStyleSheet(QString(
        "QPushButton { background: #262626; color: #aaa; "
        "border: 2px solid #555; border-radius: 10px; "
        "font-size: %1px; font-weight: bold; letter-spacing: 1px; }"
        "QPushButton:hover { background: #333; color: #e8e8e8; }"
    ).arg(s_.buttonFont));
    headerLayout->addWidget(backBtn);

    QLabel *historyTitle = new QLabel("ALARM HISTORY", headerBar);
    historyTitle->setAlignment(Qt::AlignCenter);
    historyTitle->setStyleSheet(QString(
        "color: #C0DD97; font-size: %1px; "
        "font-weight: bold; letter-spacing: 2px;"
    ).arg(s_.buttonFont));
    headerLayout->addWidget(historyTitle, 1);

    // Spacer on the right for visual balance against the Back button.
    QWidget *spacer = new QWidget(headerBar);
    spacer->setFixedSize(s_.runBtnW, s_.runBtnH);
    headerLayout->addWidget(spacer);

    layout->addWidget(headerBar);

    // --- Subtitle row ---
    QLabel *subtitle = new QLabel(
        QString("Showing the last %1 alarms recorded on this device. "
                "Records persist across reboots and network outages.")
            .arg(AlarmHistory::MAX_RECORDS),
        historyPage_);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QString(
        "color: #888; font-size: %1px; padding: 6px;"
    ).arg(s_.statusFont));
    layout->addWidget(subtitle);

    // --- Scrollable list of alarms ---
    historyList_ = new QListWidget(historyPage_);
    historyList_->setStyleSheet(QString(
        "QListWidget { background: #0d0d0d; border: none; "
        "color: #e8e8e8; font-family: 'Courier New', monospace; "
        "font-size: %1px; padding: 8px; }"
        "QListWidget::item { padding: 8px 12px; "
        "border-bottom: 1px solid #2a2a2a; }"
        "QListWidget::item:selected { background: #262626; "
        "color: #FCEBEB; }"
        "QScrollBar:vertical { background: #1a1a1a; width: 14px; }"
        "QScrollBar::handle:vertical { background: #404040; "
        "border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #555; }"
    ).arg(s_.statusFont));
    historyList_->setSelectionMode(QAbstractItemView::NoSelection);
    historyList_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(historyList_, 1);

    connect(backBtn, &QPushButton::clicked,
            this, &MainWindow::onBackToMainClicked);
}

void MainWindow::refreshHistoryList()
{
    if (!historyList_ || !alarmDb_) {
        return;
    }

    historyList_->clear();
    QVector<AlarmRecord> records = alarmDb_->recentAlarms();

    if (records.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem(
            "No alarms recorded yet. The list will populate as alarms occur.");
        item->setTextAlignment(Qt::AlignCenter);
        item->setForeground(QColor("#666"));
        historyList_->addItem(item);
        return;
    }

    for (const AlarmRecord& r : records) {
        // Color-code by severity. Trip events are red, warnings amber.
        QColor color;
        if (r.alarmType == "OVERVOLTAGE" ||
            r.alarmType == "UNDERVOLTAGE_TRIP" ||
            r.alarmType == "OVERCURRENT") {
            color = QColor("#F09595");
        } else if (r.alarmType == "UNDERVOLTAGE_WARN") {
            color = QColor("#FFE8B3");
        } else {
            color = QColor("#e8e8e8");
        }

        // Format: "2026-05-08 14:23:07   OVERVOLTAGE          V=132.5  I=11.20"
        QString line = QString("%1   %2   V=%3 V   I=%4 A")
                           .arg(r.timestamp.toString("yyyy-MM-dd HH:mm:ss"))
                           .arg(r.alarmType, -20)            // left-pad 20 chars
                           .arg(r.vrms,  6, 'f', 1)
                           .arg(r.irms,  5, 'f', 2);

        QListWidgetItem *item = new QListWidgetItem(line);
        item->setForeground(color);
        historyList_->addItem(item);
    }
}

void MainWindow::onShowHistoryClicked()
{
    if (!pageStack_ || !historyPage_) return;
    refreshHistoryList();
    pageStack_->setCurrentWidget(historyPage_);
}

void MainWindow::onBackToMainClicked()
{
    if (!pageStack_ || !mainPage_) return;
    pageStack_->setCurrentWidget(mainPage_);
}
