#include "waveformwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPaintEvent>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setAutoFillBackground(true);
    setStyleSheet("background: #0d0d0d;");
}

void WaveformWidget::setData(const QVector<double>& voltage,
                              const QVector<double>& current)
{
    vData_ = voltage;
    iData_ = current;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();

    p.fillRect(rect(), QColor("#0d0d0d"));

    p.setPen(QPen(QColor("#2a2a2a"), 1, Qt::SolidLine));
    const int gridCols = 10;
    const int gridRows = 6;
    for (int c = 1; c < gridCols; ++c) {
        int x = (w * c) / gridCols;
        p.drawLine(x, 0, x, h);
    }
    for (int r = 1; r < gridRows; ++r) {
        int y = (h * r) / gridRows;
        p.drawLine(0, y, w, y);
    }

    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(0, h / 2, w, h / 2);

    if (vData_.isEmpty() && iData_.isEmpty()) {
        p.setPen(QColor("#555"));
        p.drawText(rect(), Qt::AlignCenter, "Press RUN to start measurement");
        return;
    }

    auto yForValue = [&](double value, double range) {
        double normalized = value / range;
        if (normalized > 1.0) normalized = 1.0;
        if (normalized < -1.0) normalized = -1.0;
        return (h / 2.0) - normalized * (h / 2.0 - 4);
    };

    if (vData_.size() >= 2) {
        p.setPen(QPen(QColor("#E24B4A"), 2));
        QPainterPath path;
        for (int i = 0; i < vData_.size(); ++i) {
            double x = (double(i) / double(vData_.size() - 1)) * w;
            double y = yForValue(vData_[i], vRange_);
            if (i == 0) path.moveTo(x, y);
            else        path.lineTo(x, y);
        }
        p.drawPath(path);
    }

    if (iData_.size() >= 2) {
        p.setPen(QPen(QColor("#EF9F27"), 2));
        QPainterPath path;
        for (int i = 0; i < iData_.size(); ++i) {
            double x = (double(i) / double(iData_.size() - 1)) * w;
            double y = yForValue(iData_[i], iRange_);
            if (i == 0) path.moveTo(x, y);
            else        path.lineTo(x, y);
        }
        p.drawPath(path);
    }

    p.setFont(QFont("Arial", 9));
    p.setPen(QColor("#E24B4A"));
    p.drawText(10, 18, "-- Voltage");
    p.setPen(QColor("#EF9F27"));
    p.drawText(100, 18, "-- Current");

    p.setPen(QColor("#888888"));
    p.setFont(QFont("Courier", 9));
    p.drawText(4, 14, QString("+%1V").arg(vRange_));
    p.drawText(4, h - 4, QString("-%1V").arg(vRange_));
    p.drawText(w - 48, 14, QString("+%1A").arg(iRange_));
    p.drawText(w - 48, h - 4, QString("-%1A").arg(iRange_));
}
