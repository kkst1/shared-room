# ALSA 深度面试题（20 题）

---

## 一、架构与核心概念

**Q1. ALSA 的整体架构分几层？从用户态到硬件，数据经过哪些环节？**
- 追问：alsa-lib、alsa-driver、ASoC framework 三者的关系是什么？
- 追问：你直接操作过 `/dev/snd/` 下的设备节点吗？`pcmC0D0p` 这个命名中每个字段代表什么？

**Q2. PCM 设备中的 playback 和 capture 在内核中分别对应什么 stream direction？一个声卡可以同时有多个 PCM 设备吗？**
- 追问：substream 是什么概念？什么场景下一个 PCM device 会有多个 substream？

**Q3. ALSA 中 hw_params 阶段到底做了什么？为什么要单独有这个阶段？**
- 追问：hw_params 和 sw_params 的区别？sw_params 中的 `start_threshold` 和 `avail_min` 分别控制什么行为？

---

## 二、Buffer 与数据流

**Q4. ALSA 的 ring buffer 是怎么组织的？`buffer_size`、`period_size`、`periods` 三者的关系？**
- 追问：period 的本质是什么？为什么不直接用一整块 buffer 而要分 period？
- 追问：period 大小对延迟和 CPU 负载的影响是什么？

**Q5. `hw_ptr` 和 `appl_ptr` 分别代表什么？它们怎么配合完成数据流转？**
- 追问：playback 场景下，如果 `appl_ptr` 追上了 `hw_ptr` 会发生什么？capture 呢？
- 追问：XRUN 的两种类型 underrun 和 overrun 分别对应哪个方向？

**Q6. mmap 模式和 read/write 模式的区别是什么？什么场景下用 mmap？**
- 追问：mmap 模式下用户态怎么知道当前可以写哪段 buffer？
- 追问：`snd_pcm_mmap_begin` / `snd_pcm_mmap_commit` 的工作流程？

**Q7. interleaved 和 non-interleaved 格式的区别？你的项目用的哪种？为什么？**
- 追问：`snd_pcm_writei` 和 `snd_pcm_writen` 分别对应哪种？

---

## 三、状态机与错误处理

**Q8. ALSA PCM 的状态机有哪些状态？画出主要的状态转换路径。**
- 追问：`PREPARED -> RUNNING` 的触发条件是什么？自动触发和手动触发有什么区别？
- 追问：`XRUN` 状态下能直接写数据吗？怎么恢复？

**Q9. `snd_pcm_recover` 做了什么？你在项目中怎么处理 XRUN 的？**
- 追问：recover 之后需要重新 prepare 吗？还是直接可以继续写？
- 追问：如果 XRUN 频繁发生，你从哪些方向排查？

**Q10. `-EPIPE` 和 `-ESTRPIPE` 分别代表什么错误？处理方式有什么不同？**
- 追问：`-ESTRPIPE` 在什么场景下会出现？suspend/resume 和 ALSA 的关系？

---

## 四、ASoC 与硬件层

**Q11. ASoC 框架的三大组件是什么？它们怎么组合成一个完整的声卡？**
- 追问：machine driver 的职责是什么？为什么不能只有 codec driver 和 platform driver？
- 追问：`dai_link` 结构体中最关键的几个字段是什么？

**Q12. McASP 是什么？它和 I2S 的关系？你的项目中 McASP 配置了哪些参数？**
- 追问：McASP 的 serializer 是什么概念？你用了几个？方向怎么配的？
- 追问：McASP 的时钟源从哪来？BCLK 和 MCLK 的关系？

**Q13. 你的 codec（PCM5122）的驱动是怎么和 McASP 对接的？I2C 在其中扮演什么角色？**
- 追问：I2C 是用来传音频数据的吗？codec 的控制面和数据面分别走什么总线？
- 追问：设备树中怎么把 codec 和 dai_link 关联起来的？

**Q14. 采样率、位深、通道数这些参数是在哪一层协商确定的？如果 codec 不支持某个采样率会怎样？**
- 追问：`set_fmt` 中的 `SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS` 每一段什么意思？

---

## 五、实际调试与工程经验

**Q15. 你怎么确认音频数据链路是通的？从用户态到 DAC 输出，你的验证步骤是什么？**
- 追问：`aplay` 播放一个 wav 文件没有声音，你从哪里开始排查？
- 追问：你用过 `arecord` 抓数据再 `aplay` 回放来验证吗？

**Q16. `amixer` 和 `alsamixer` 的区别？你怎么用它们调试音量和路由问题？**
- 追问：ALSA 的 control interface 和 PCM interface 的关系？
- 追问：kcontrol 在内核中是怎么注册的？

**Q17. 你遇到过音频数据正确但播放有杂音/爆音的情况吗？可能的原因有哪些？**
- 追问：时钟不匹配会导致什么现象？怎么确认 BCLK/LRCLK 频率是否正确？
- 追问：DMA buffer 对齐问题会导致什么症状？

**Q18. 你的 ADS8688（ADC）采集的数据格式是什么？怎么转换成 ALSA 能识别的 PCM 格式？**
- 追问：ADS8688 走的是 SPI，不是 I2S，你怎么把它的数据喂进 ALSA pipeline 的？
- 追问：如果 ADC 采样率和 ALSA 配置的采样率不一致，你怎么处理？

---

## 六、进阶与原理

**Q19. ALSA 的 DMAC（DMA Controller）在音频传输中扮演什么角色？DMA buffer 和 ALSA ring buffer 是同一块内存吗？**
- 追问：DMA 传输完一个 period 后，内核怎么知道的？中断还是轮询？
- 追问：`pointer` 回调函数的作用是什么？它返回的值代表什么？

**Q20. 如果让你在 R5F 核上采集音频数据，然后通过共享内存传给 A53 上的 ALSA 播放，这条链路的时序怎么保证？**
- 追问：R5F 侧没有 ALSA，你怎么保证送过来的数据格式和 ALSA 期望的一致？
- 追问：共享内存的生产者-消费者同步你用了什么机制？如果 A53 消费慢了怎么办？
- 追问：这条链路的端到端延迟你测过吗？瓶颈在哪？
