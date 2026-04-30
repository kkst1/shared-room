# ARMv8-A 底层面试题（20 题）

面向嵌入式/系统软件/BSP/内核方向，覆盖大厂高频考点。

---

## 一、异常等级与特权模型

**Q1. ARMv8-A 的四个异常等级（EL0-EL3）分别运行什么软件？为什么要分这么多级？**
- 追问：EL2 和 EL3 的区别是什么？Hypervisor 和 Secure Monitor 各自解决什么问题？
- 追问：一个普通的 Linux 用户态进程触发 syscall，异常等级怎么变化？硬件做了哪些事？
- 追问：从 EL1 进入 EL3 的路径是什么？能直接跳吗？

**Q2. ARMv8 的 Secure 和 Non-Secure 状态是怎么隔离的？SCR_EL3.NS 位的作用？**
- 追问：Non-Secure 状态下能访问 Secure 的物理内存吗？硬件怎么拦截的？
- 追问：TrustZone 的隔离粒度到什么程度？是页级别还是总线级别？
- 追问：OP-TEE 运行在哪个异常等级？它和 ATF 的关系？

---

## 二、寄存器与执行状态

**Q3. AArch64 有多少个通用寄存器？X30 的特殊用途是什么？SP 和 PC 能当通用寄存器用吗？**
- 追问：X29 通常用来做什么？为什么需要 Frame Pointer？
- 追问：函数调用时哪些寄存器是 caller-saved，哪些是 callee-saved？依据是什么？

**Q4. PSTATE 中的 NZCV 标志位分别代表什么？条件执行是怎么工作的？**
- 追问：AArch64 还有条件执行指令吗？和 AArch32 的 IT block 有什么区别？
- 追问：CSEL 和 CSINC 指令的典型用法？编译器什么时候会生成这些指令？

**Q5. AArch64 和 AArch32 的切换是怎么发生的？能在同一个异常等级内切换吗？**
- 追问：EL1 运行 64 位内核，EL0 能运行 32 位应用吗？反过来呢？
- 追问：切换执行状态时寄存器映射怎么处理？W0 和 X0 的关系？

---

## 三、内存管理与 MMU

**Q6. ARMv8 的虚拟地址空间是怎么划分的？TTBR0_EL1 和 TTBR1_EL1 分别管哪段地址？**
- 追问：为什么要用两个页表基地址寄存器？只用一个行不行？
- 追问：内核地址空间的高位全 1，用户地址空间的高位全 0，中间那段地址访问会怎样？
- 追问：TCR_EL1 中的 T0SZ 和 T1SZ 控制什么？怎么计算实际的虚拟地址宽度？

**Q7. ARMv8 的四级页表结构是怎样的？从虚拟地址到物理地址的翻译过程？**
- 追问：4KB 页和 64KB 页的页表级数有什么不同？为什么有些场景选大页？
- 追问：Block descriptor 和 Table descriptor 的区别？什么时候用 Block mapping？
- 追问：页表项中的 AP、UXN、PXN 位分别控制什么权限？

**Q8. TLB 的作用和结构？什么时候需要手动 invalidate TLB？**
- 追问：ASID 是什么？它怎么避免进程切换时 flush 整个 TLB？
- 追问：`TLBI VALE1IS` 这条指令中每个字段代表什么？IS 后缀的含义？
- 追问：内核修改了一个页表项之后，需要执行哪些操作才能保证所有 CPU 看到新映射？完整的序列是什么？

---

## 四、Cache 体系

**Q9. ARMv8 的 Cache 层次结构是怎样的？L1 I-Cache 和 D-Cache 为什么要分开？**
- 追问：PIPT、VIPT、VIVT 分别是什么？ARMv8 的 L1 D-Cache 通常是哪种？为什么？
- 追问：VIPT 会有 alias 问题吗？ARMv8 怎么处理的？

**Q10. Cache 一致性协议在 ARMv8 多核系统中怎么工作的？什么是 MOESI/MESI？**
- 追问：ACE 和 CHI 协议的区别？它们在 SoC 中的位置？
- 追问：两个核同时写同一个 cache line 会发生什么？false sharing 是什么？怎么避免？

**Q11. DMA 和 Cache 一致性问题怎么处理？什么时候需要手动 clean/invalidate cache？**
- 追问：`DC CIVAC`、`DC CVAC`、`DC IVAC` 三条指令的区别？
- 追问：DMA 读和 DMA 写分别需要什么 cache 操作？顺序搞反了会怎样？
- 追问：什么是 non-cacheable mapping？什么场景下用 device memory 属性？

---

## 五、异常与中断

**Q12. ARMv8 的异常向量表结构是怎样的？为什么有 16 个 entry？**
- 追问：同步异常和异步异常的区别？IRQ、FIQ、SError 分别属于哪类？
- 追问：异常发生时硬件自动保存了哪些状态？SPSR_ELx 和 ELR_ELx 的作用？
- 追问：`ERET` 指令做了什么？

**Q13. GIC（Generic Interrupt Controller）的架构是怎样的？GICv3 和 GICv2 的主要区别？**
- 追问：SPI、PPI、SGI、LPI 分别是什么？各自的典型使用场景？
- 追问：中断亲和性（affinity）怎么配置？怎么把一个中断绑定到特定 CPU？
- 追问：中断优先级和抢占是怎么工作的？PMR 寄存器的作用？

**Q14. FIQ 和 IRQ 的区别？在 ATF/OP-TEE 的场景下，FIQ 通常用来做什么？**
- 追问：Secure 中断和 Non-Secure 中断的路由规则？SCR_EL3 中的 FIQ/IRQ 路由位怎么配？

---

## 六、内存序与屏障

**Q15. ARMv8 的内存模型是强序还是弱序？这对多核编程意味着什么？**
- 追问：什么是 store buffer？为什么 CPU 写入的数据其他核可能看不到？
- 追问：举一个不加屏障会出 bug 的具体例子。

**Q16. DMB、DSB、ISB 三条屏障指令分别做什么？什么场景下用哪个？**
- 追问：`DMB ISH` 和 `DMB OSH` 的区别？ISH/OSH/NSH 代表什么 shareability domain？
- 追问：修改页表后为什么需要 DSB + ISB 而不是只用 DMB？
- 追问：`ldar`/`stlr` 指令和显式屏障的关系？什么是 acquire/release 语义？

---

## 七、启动流程

**Q17. ARMv8 系统从上电到 Linux 内核运行，经历了哪些阶段？每个阶段运行在什么异常等级？**
- 追问：BL1 → BL2 → BL31 → BL33 分别对应什么？ATF 的角色是什么？
- 追问：U-Boot 运行在 EL 几？它怎么把控制权交给 Linux 内核的？
- 追问：内核启动时 MMU 是开的还是关的？内核怎么建立初始页表？

**Q18. 多核启动用的是 PSCI 还是 spin-table？两者的区别？**
- 追问：PSCI 的 `CPU_ON` 调用流程是什么？从核怎么知道跳到哪个地址执行？
- 追问：设备树中 `enable-method` 字段的作用？
- 追问：从核启动后的第一条指令在哪个异常等级执行？

---

## 八、调试与性能

**Q19. ARMv8 的硬件断点和 watchpoint 是怎么实现的？最多能设几个？**
- 追问：JTAG 和 self-hosted debug 的区别？MDSCR_EL1 的作用？
- 追问：单步执行（single step）在硬件层面怎么实现的？SS bit 在哪里？
- 追问：Linux 内核的 kgdb 和 JTAG 调试的区别？各自的适用场景？

**Q20. ARMv8 的 PMU（Performance Monitor Unit）能做什么？怎么用它定位性能瓶颈？**
- 追问：PMU 能统计哪些事件？cache miss、branch mispredict、TLB miss 分别对应什么事件号？
- 追问：`perf` 工具底层是怎么和 PMU 交互的？
- 追问：PMU 的 counter overflow 中断有什么用？采样型 profiling 的原理？
