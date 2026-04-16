#pragma once

#include <mutex>
#include <optional>

#include <QMainWindow>
#include <QObject>
#include <QTimer>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>

#include "types.hpp"

Q_DECLARE_METATYPE(DspUiFrame)

// UiBridge 运行在线程3（Qt 主线程）：
// - 接收 DSP 线程通过 QueuedConnection 投递的数据；
// - 用固定 60Hz 定时器做重绘节流；
// - 渲染波形与频谱，避免被计算线程直接驱动导致卡顿。
class UiBridge : public QMainWindow {
    Q_OBJECT
public:
    explicit UiBridge(QWidget* parent = nullptr);

    // 初始化窗口与图表资源。
    void setup();

public slots:
    // 由 DSP 线程异步调用，仅缓存“最新一帧”。
    void enqueue_frame(const DspUiFrame& frame);

private slots:
    // 60Hz 定时刷新，主线程只在这个节奏绘图。
    void render_tick();

private:
    // 把缓存帧真正写入 QtCharts。
    void render_frame(const DspUiFrame& frame);

private:
    QtCharts::QChart* wave_chart_ = nullptr;
    QtCharts::QChart* spectrum_chart_ = nullptr;
    QtCharts::QLineSeries* wave_series_ = nullptr;
    QtCharts::QLineSeries* spectrum_series_ = nullptr;
    QtCharts::QChartView* wave_view_ = nullptr;
    QtCharts::QChartView* spectrum_view_ = nullptr;
    QTimer timer_;

    std::mutex frame_mutex_;
    std::optional<DspUiFrame> pending_frame_;
};
