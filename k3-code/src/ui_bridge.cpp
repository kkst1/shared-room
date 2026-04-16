#include "ui_bridge.hpp"

#include <QVBoxLayout>
#include <QWidget>

UiBridge::UiBridge(QWidget* parent) : QMainWindow(parent) {
    // 固定 60Hz 节奏刷新 UI，避免计算线程直接驱动重绘导致抖动。
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &UiBridge::render_tick);
}

void UiBridge::setup() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    wave_series_ = new QtCharts::QLineSeries(this);
    wave_chart_ = new QtCharts::QChart();
    wave_chart_->addSeries(wave_series_);
    wave_chart_->createDefaultAxes();
    wave_chart_->setTitle("Waveform Envelope");

    spectrum_series_ = new QtCharts::QLineSeries(this);
    spectrum_chart_ = new QtCharts::QChart();
    spectrum_chart_->addSeries(spectrum_series_);
    spectrum_chart_->createDefaultAxes();
    spectrum_chart_->setTitle("Spectrum (dB)");

    wave_view_ = new QtCharts::QChartView(wave_chart_, this);
    spectrum_view_ = new QtCharts::QChartView(spectrum_chart_, this);
    wave_view_->setRenderHint(QPainter::Antialiasing, false);
    spectrum_view_->setRenderHint(QPainter::Antialiasing, false);

    layout->addWidget(wave_view_);
    layout->addWidget(spectrum_view_);
    setCentralWidget(central);
    resize(1200, 800);

    timer_.start();
}

void UiBridge::enqueue_frame(const DspUiFrame& frame) {
    // 只缓存“最后一帧”，防止 UI 堆积导致延迟持续增长。
    std::lock_guard<std::mutex> lock(frame_mutex_);
    pending_frame_ = frame;
}

void UiBridge::render_tick() {
    std::optional<DspUiFrame> frame;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!pending_frame_.has_value()) {
            return;
        }
        frame = std::move(pending_frame_);
        pending_frame_.reset();
    }
    render_frame(frame.value());
}

void UiBridge::render_frame(const DspUiFrame& frame) {
    QVector<QPointF> wave_points;
    wave_points.reserve(static_cast<int>(frame.waveform_envelope.size()));
    for (int i = 0; i < static_cast<int>(frame.waveform_envelope.size()); ++i) {
        wave_points.push_back(QPointF(i, frame.waveform_envelope[static_cast<size_t>(i)]));
    }
    wave_series_->replace(wave_points);

    QVector<QPointF> spectrum_points;
    spectrum_points.reserve(static_cast<int>(frame.spectrum_db.size()));
    for (int i = 0; i < static_cast<int>(frame.spectrum_db.size()); ++i) {
        spectrum_points.push_back(QPointF(i, frame.spectrum_db[static_cast<size_t>(i)]));
    }
    spectrum_series_->replace(spectrum_points);

    // 更新窗口标题可直观看到数据流动与序号推进。
    setWindowTitle(QString("K3 UI - seq:%1 ts:%2")
                       .arg(frame.sequence)
                       .arg(static_cast<qulonglong>(frame.timestamp_ns)));
}
