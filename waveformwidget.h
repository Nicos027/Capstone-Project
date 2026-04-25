#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>

class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

public slots:
    void setData(const QVector<double>& voltage,
                 const QVector<double>& current);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> vData_;
    QVector<double> iData_;
    double vRange_ = 200.0;
    double iRange_ = 25.0;
};

#endif
