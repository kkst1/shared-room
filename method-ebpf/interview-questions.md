# eBPF 内存分析项目面试题（50 题）

## 一、项目验证类（验证是否亲手实现）

**Q1. 你的 eBPF 程序挂载点用的是 tracepoint 还是 kprobe？为什么选这个？**
- 追问：tracepoint 和 kprobe 在稳定性、性能、可用性上有什么区别？

**Q2. 你的 kmalloc tracepoint 的完整路径是什么？你怎么找到的？**
- 追问：如果目标内核没有这个 tracepoint，你怎么办？

**Q3. ring buffer 满了你怎么处理的？你实际遇到过丢事件吗？**
- 追问：你怎么统计 dropped count 的？用户态怎么感知到丢失？

**Q4. 你的 `mem_event` 结构体为什么把 `stack_id` 定义为 `__s32` 而不是 `__u32`？**

**Q5. 你用的 `bpf_get_stackid` 的 flag 参数传了什么？为什么？**
- 追问：`BPF_F_FAST_STACK_CMP` 和 `BPF_F_REUSE_STACKID` 分别什么作用？

**Q6. 你的 alloc table（ptr -> alloc_info）用的什么类型的 map？容量设了多少？**
- 追问：如果 map 满了会怎样？你怎么处理的？

**Q7. 你在 kfree 的 hook 里怎么拿到被释放的指针地址的？参数从哪来？**

**Q8. 开发过程中 verifier 拒绝过你的程序吗？具体是什么错误？你怎么解决的？**

**Q9. 你的 skeleton 文件是怎么生成的？构建流程是什么样的？**
- 追问：bpftool gen skeleton 的输入是什么？输出是什么？

**Q10. 你调试 eBPF 程序用了什么手段？怎么确认内核态逻辑正确？**
- 追问：`bpf_printk` 的输出在哪里看？生产环境能用吗？

## 二、eBPF 原理与机制

**Q11. eBPF verifier 的核心职责是什么？它会检查哪些东西？**
- 追问：verifier 对循环有什么限制？5.3 之后有什么变化？

**Q12. eBPF 程序的栈空间限制是多少？你怎么应对这个限制的？**

**Q13. ring buffer 和 perf buffer 的核心区别是什么？**
- 追问：ring buffer 是 per-CPU 的还是全局共享的？这带来什么好处和问题？

**Q14. BPF map 的生命周期是怎样的？程序 detach 后 map 还在吗？**
- 追问：什么是 pinned map？什么场景下需要用？

**Q15. CO-RE 的重定位机制具体是怎么工作的？**
- 追问：如果目标内核没有 BTF 信息怎么办？

**Q16. `vmlinux.h` 是怎么生成的？它包含什么内容？**
- 追问：为什么不直接 include 内核头文件？

**Q17. eBPF 程序能调用任意内核函数吗？什么是 BPF helper？**
- 追问：不同程序类型能用的 helper 一样吗？

**Q18. tail call 是什么？你的项目用到了吗？为什么？**

**Q19. BPF_MAP_TYPE_STACK_TRACE 这个 map 的工作原理是什么？**
- 追问：stack_id 是怎么生成的？相同调用栈会得到相同 id 吗？

**Q20. eBPF 程序的执行上下文是什么？它运行在哪个 CPU、哪个进程上下文中？**

## 三、Linux 内存子系统

**Q21. kmalloc 和 vmalloc 的区别是什么？什么时候用哪个？**
- 追问：kmalloc 底层走的是 slab 还是 buddy？取决于什么？

**Q22. SLAB/SLUB/SLOB 三者的区别和适用场景？当前主流内核默认用哪个？**

**Q23. `kmem_cache_alloc` 和 `kmalloc` 的关系是什么？**
- 追问：为什么你的项目要同时 hook 这两个？只 hook kmalloc 不够吗？

**Q24. 内核的 buddy system 是怎么工作的？page order 是什么概念？**

**Q25. 什么是内存碎片？内核怎么缓解外部碎片？**
- 追问：compaction 和 reclaim 的区别？

**Q26. OOM killer 的触发条件和选择策略是什么？**
- 追问：`oom_score_adj` 怎么影响选择？

**Q27. `/proc/meminfo` 中 `Slab`、`SReclaimable`、`SUnreclaim` 分别代表什么？**

**Q28. 内核态内存泄漏和用户态内存泄漏的本质区别是什么？**
- 追问：内核态泄漏为什么更危险？

**Q29. kfree 之后内存真的还给了系统吗？还是留在 slab cache 里？**

**Q30. 什么是 GFP flags？`GFP_KERNEL` 和 `GFP_ATOMIC` 的区别？**
- 追问：在中断上下文中能用 `GFP_KERNEL` 吗？为什么？

## 四、Linux 系统与内核通用知识

**Q31. tracepoint 的实现原理是什么？它和 ftrace 的关系？**

**Q32. `/proc/kallsyms` 的内容是什么？`kptr_restrict` 设为 1 和 2 分别有什么效果？**

**Q33. 什么是 BTF？它和 DWARF 的区别？为什么内核选择 BTF？**

**Q34. Linux 的进程和线程在内核中有什么区别？`pid` 和 `tgid` 分别代表什么？**
- 追问：你的项目按 pid 还是 tgid 聚合？为什么？

**Q35. 什么是 per-CPU 变量？eBPF 中的 per-CPU map 解决什么问题？**

**Q36. 中断上下文和进程上下文的区别？在中断上下文中能做什么不能做什么？**

**Q37. 什么是 RCU？它和读写锁的区别？eBPF map 的并发访问用了什么机制？**

**Q38. Linux 的 namespace 和 cgroup 分别解决什么问题？**
- 追问：你的工具在容器环境中能正常工作吗？PID namespace 会影响你的 pid 统计吗？

**Q39. 什么是 uprobe？和 kprobe 的区别？你的项目有没有考虑用户态内存分析？**

**Q40. seccomp 和 eBPF 的关系是什么？**

## 五、工程实现与设计决策

**Q41. 你的用户态消费 ring buffer 的线程模型是什么？单线程轮询还是 epoll？**
- 追问：如果消费速度跟不上生产速度，你怎么处理？

**Q42. 你的 leak 检测扫描是怎么触发的？定时器还是每次事件驱动？**
- 追问：扫描 alloc table 的时间复杂度是多少？数据量大了怎么办？

**Q43. 你的 histogram 是在内核态聚合还是用户态聚合？为什么？**
- 追问：如果放内核态做 log2 bucket，verifier 会有问题吗？

**Q44. 你怎么处理短生命周期的分配？比如 alloc 后几微秒就 free 了，你的 alloc table 会膨胀吗？**

**Q45. 你的符号解析是实时做的还是离线做的？性能开销怎么样？**
- 追问：`/proc/kallsyms` 有多大？你怎么做地址到符号的映射？二分查找？

**Q46. 如果让你加一个"按 cgroup 聚合"的功能，你怎么实现？**

**Q47. 你的工具对目标系统的性能影响有多大？你测过吗？怎么测的？**
- 追问：hook kmalloc 这种高频路径，overhead 主要来自哪里？

**Q48. 如果内核版本没有你需要的 tracepoint，你的 fallback 方案是什么？**

**Q49. 你的项目能同时分析多个进程吗？filter 是在内核态做还是用户态做？**
- 追问：为什么建议在内核态做 PID 过滤而不是全量采集后在用户态过滤？

**Q50. 如果让你把这个工具做成一个 daemon 长期运行，你需要考虑哪些额外问题？**
- 追问：map 容量管理、内存占用增长、日志轮转、信号处理，你会怎么设计？
