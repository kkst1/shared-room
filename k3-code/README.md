# K3 A53 四线程采集/显示/回放示例

这个目录实现了你描述的 A53 侧核心代码骨架：

- 线程1：`IngestWorker`，`rpmsg_char + 共享内存` 极简搬运（最高实时优先级，不做计算）
- 线程2：`DspWorker`，NEON 优化窗口处理 + FFT + 倍频程，输出 UI 数据
- 线程3：`UiBridge`，Qt 主线程 60Hz 固定重绘
- 线程4：`PersistenceWorker`，低优先级异步落盘（eMMC）
- 旁路：`OfflineBypassPlayer`，从 eMMC WAV 读取 -> `libsamplerate` 重采样到 48k -> ALSA `snd_pcm` 播放

## 构建

```bash
mkdir build
cd build
cmake ..
make -j
```

## 运行前参数（按板卡实际修改）

- `rpmsg_device`：例如 `/dev/rpmsg0`
- `shm_device`：例如 `/dev/mem`（映射 reserved-memory）
- `shm_base` / `shm_size`：与 DTS reserved-memory 对齐
- `persist_path`：例如 `/run/media/mmcblk0p1/capture.raw`
- `alsa_device`：例如 `hw:0,0`

## 注意

- 这里给的是“可工程化改造”的主骨架，底层寄存器地址、rpmsg 描述符格式、DTS 区间请替换成你项目实际值。
- 所有函数都加了中文注释，便于你面试时按代码讲“数据流、线程模型、实时性取舍”。

