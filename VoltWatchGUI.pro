QT       += core gui widgets

CONFIG += c++17

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += USE_REAL_ADC

LIBS += -llgpio -lmosquitto -lcurl

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    worker.cpp \
    waveformwidget.cpp \
    mqttpublisher.cpp \
    signal_processing.cpp \
    ads131m02.cpp

HEADERS += \
    config.h \
    mainwindow.h \
    worker.h \
    waveformwidget.h \
    mqttpublisher.h \
    signal_processing.hpp \
    ads131m02.hpp

FORMS += \
    mainwindow.ui
