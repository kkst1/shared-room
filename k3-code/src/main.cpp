#include <QApplication>
#include <QObject>

#include <iostream>
#include <string>

#include "dsp_engine.hpp"
#include "lockfree_ring_buffer.hpp"
#include "persistence_worker.hpp"
#include "playback_bypass.hpp"
#include "shm_transport.hpp"
#include "types.hpp"
#include "ui_bridge.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    qRegisterMetaType<DspUiFrame>("DspUiFrame");

    // 示例配置：实际部署时建议改为读取 yaml/json/命令行参数。
    AppConfig config {};

    // 线程1->线程2，线程1->线程4 的两条无锁队列。
    SpscRingBuffer<SharedBlockView> dsp_queue(config.ring_capacity);
    SpscRingBuffer<SharedBlockView> persist_queue(config.ring_capacity);

    UiBridge ui;
    ui.setup();
    ui.show();

    SharedMemoryTransport transport(config, dsp_queue, persist_queue);
    if (!transport.initialize()) {
        std::cerr << "failed to initialize transport. check rpmsg/shm config." << std::endl;
        return 1;
    }

    DspEngineWorker dsp_worker(config, dsp_queue, &ui);
    PersistenceWorker persist_worker(config, persist_queue);
    if (!persist_worker.start()) {
        std::cerr << "failed to open persistence file: " << config.persist_path << std::endl;
        return 1;
    }

    transport.start();
    dsp_worker.start();

    // 可选：旁路回放模式。
    // 运行示例：./k3_app /run/media/mmcblk0p1/demo.wav
    if (argc > 1) {
        OfflineBypassPlayer player(config);
        const std::string wav = argv[1];
        if (!player.play_wav_file(wav)) {
            std::cerr << "offline bypass playback failed: " << wav << std::endl;
        }
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        dsp_worker.stop();
        transport.stop();
        persist_worker.stop();
    });

    return app.exec();
}

