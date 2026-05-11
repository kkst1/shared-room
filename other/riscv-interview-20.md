# RISC-V 架构面试题 20 道（嵌入式/系统软件/BSP/内核方向）

面向大厂嵌入式、系统软件、BSP、内核岗位，覆盖 RISC-V 架构高频考点。

---

## 一、指令集架构基础

**Q1. RISC-V 的模块化指令集设计是什么意思？RV32I/RV64I 和扩展（M/A/F/D/C）分别代表什么？**
- 追问：什么是 Zicsr、Zifencei？为什么要把它们从 I 中拆出来？
- 追问：嵌入式场景下你会选择哪些扩展组合？为什么不全选？

**Q2. RISC-V 的寄存器文件有多少个通用寄存器？和 ARM（32 个）、x86 相比有什么设计考量？**
- 追问：x0 硬连线为 0 有什么好处？举几个利用 x0 简化指令编码的例子。
- 追问：caller-saved 和 callee-saved 寄存器的划分规则是什么？为什么这样划分？

**Q3. RISC-V 为什么没有条件码（condition code / flags register）？分支指令怎么实现的？**
- 追问：和 ARM 的条件执行（conditional execution）相比，各有什么优劣？
- 追问：没有 flags 对乱序执行有什么好处？

**Q4. RISC-V 的立即数编码为什么是"散乱"的（bit 位不连续）？这样设计的目的是什么？**
- 追问：U-type 和 J-type 的立即数拼接方式有什么区别？
- 追问：`lui` + `addi` 加载 32 位常数时，为什么 addi 的立即数是符号扩展的？这会导致什么问题？怎么解决？

---

## 二、特权架构与异常处理

**Q5. RISC-V 的三个特权级（M/S/U）分别是什么？为什么 M-mode 是必须的而 S/U 是可选的？**
- 追问：嵌入式 MCU 通常只实现 M-mode，这时候怎么做内存保护？
- 追问：Hypervisor 扩展（H 扩展）加了什么特权级？VS/VU 是什么？

**Q6. 发生异常/中断时，硬件自动做了哪些事情？软件需要做哪些事情？**
- 追问：`mstatus` 中的 `MIE`/`MPIE`/`MPP` 字段分别什么作用？
- 追问：`mret` 指令具体做了什么？和普通 `jalr` 跳转有什么区别？
- 追问：嵌套中断怎么实现？硬件支持还是纯软件？

**Q7. `mtvec` 的两种模式（Direct / Vectored）有什么区别？实际项目中你怎么选？**
- 追问：Vectored 模式下，中断向量表的对齐要求是什么？
- 追问：CLIC（Core-Local Interrupt Controller）和 CLINT 的区别？CLIC 解决了什么问题？

**Q8. RISC-V 的中断分为哪几类？`mip`/`mie` 寄存器怎么控制中断使能和挂起？**
- 追问：software interrupt、timer interrupt、external interrupt 分别由谁触发？
- 追问：PLIC 的工作原理？优先级仲裁和 claim/complete 流程是什么？
- 追问：多核场景下，中断怎么路由到特定 hart？

---

## 三、内存管理与地址空间

**Q9. RISC-V 的 Sv39/Sv48 页表结构是什么？和 ARM 的页表有什么区别？**
- 追问：Sv39 的三级页表每级覆盖多少地址位？PTE 中的 R/W/X 位组合有什么特殊含义？
- 追问：什么是 megapage / gigapage？什么场景下用大页？
- 追问：`satp` 寄存器的作用？切换页表后为什么需要 `sfence.vma`？

**Q10. RISC-V 的 PMP（Physical Memory Protection）是什么？怎么配置？**
- 追问：PMP 的匹配模式（TOR/NA4/NAPOT）分别是什么？各适合什么场景？
- 追问：PMP 条目数量有限（通常 8-16 个），实际项目中怎么规划？
- 追问：PMP 和 MMU 页表保护的关系？两者同时存在时谁优先？

**Q11. `fence` 和 `fence.i` 指令分别做什么？什么场景下必须用？**
- 追问：RISC-V 的内存模型（RVWMO）和 ARM 的弱内存模型有什么异同？
- 追问：`fence rw, rw` 和 `fence iorw, iorw` 的区别？
- 追问：自修改代码（self-modifying code）为什么需要 `fence.i`？光用 `fence` 不行吗？

---

## 四、原子操作与多核同步

**Q12. RISC-V A 扩展提供了哪两类原子操作？LR/SC 和 AMO 分别适合什么场景？**
- 追问：LR/SC 的 reservation set 是什么？SC 失败的可能原因有哪些？
- 追问：为什么 LR/SC 之间的指令数有限制？规范建议最多多少条？
- 追问：用 LR/SC 实现一个 CAS（compare-and-swap），写出伪代码。

**Q13. AMO 指令（amoswap/amoadd/amoand 等）的 `.aq` 和 `.rl` 后缀是什么意思？**
- 追问：acquire 和 release 语义对应 C11 内存序的哪个级别？
- 追问：实现一个 spinlock，用 amoswap.w.aq 获取、amoswap.w.rl 释放，解释为什么这样保证正确性。
- 追问：和 ARM 的 `ldaxr`/`stlxr` 对比，设计哲学有什么不同？

---

## 五、启动流程与 BSP

**Q14. 一个 RISC-V SoC 从上电到跳入 Linux 内核，经历哪些阶段？每个阶段做什么？**
- 追问：ZSBL → FSBL → OpenSBI → U-Boot → Linux 这条链路中，每一级的职责边界是什么？
- 追问：OpenSBI 是什么？它提供的 SBI 接口解决什么问题？举几个常用的 SBI call。
- 追问：设备树（DTF/FDT）在启动流程中怎么传递？谁生成、谁消费？

**Q15. 多核 RISC-V 系统的启动策略是什么？所有 hart 同时启动还是有主从之分？**
- 追问：HSM（Hart State Management）SBI 扩展是做什么的？
- 追问：Linux 的 SMP 启动中，secondary hart 怎么被唤醒？和 ARM 的 PSCI 对比。
- 追问：如果你在写 bare-metal 多核代码，怎么让 hart 1 等待 hart 0 完成初始化？

**Q16. RISC-V 的 CSR（Control and Status Register）访问指令有哪些？`csrrw`/`csrrs`/`csrrc` 的语义是什么？**
- 追问：为什么 CSR 指令用 read-modify-write 而不是单独的 read 和 write？
- 追问：`csrrwi`/`csrrsi`/`csrrci` 的立即数版本有什么限制？
- 追问：访问不存在的 CSR 会发生什么？

---

## 六、中断控制器与外设

**Q17. RISC-V 平台上 CLINT 和 PLIC 分别负责什么？它们的寄存器布局是怎样的？**
- 追问：`mtime` 和 `mtimecmp` 怎么配合实现定时中断？多核下每个 hart 有独立的 `mtimecmp` 吗？
- 追问：PLIC 的 threshold 寄存器有什么用？设为最大值会怎样？
- 追问：AIA（Advanced Interrupt Architecture）相比 PLIC 解决了什么问题？MSI 支持为什么重要？

**Q18. 在 RISC-V 嵌入式系统中，你怎么写一个中断驱动的 UART 驱动？从寄存器配置到中断处理全流程。**
- 追问：中断处理函数中能不能调用 printf？为什么？
- 追问：接收缓冲区溢出怎么处理？环形缓冲区怎么设计？
- 追问：DMA 和中断驱动的 trade-off 是什么？什么时候该用 DMA？

---

## 七、编译与 ABI

**Q19. RISC-V 的调用约定（calling convention）是什么？函数参数和返回值怎么传递？**
- 追问：a0-a7 不够用时参数怎么传？栈上参数的对齐规则？
- 追问：RV32 上返回一个 64 位值怎么处理？
- 追问：`-mabi=ilp32` / `ilp32f` / `ilp32d` / `lp64` / `lp64d` 分别是什么意思？混用会怎样？
- 追问：为什么 RISC-V 的 `ra`（return address）是 caller-saved 而不是像 ARM 的 `lr` 那样？

**Q20. RISC-V 的 C 扩展（压缩指令）是怎么工作的？对代码密度和流水线设计有什么影响？**
- 追问：16 位压缩指令和 32 位指令可以混排吗？取指单元怎么处理对齐？
- 追问：哪些指令有压缩版本？压缩指令能访问所有 32 个寄存器吗？
- 追问：C 扩展对分支预测器有什么影响？和 ARM Thumb-2 对比。
- 追问：如果你在做一个面积敏感的嵌入式核，不实现 C 扩展会损失多少代码密度？

---

## 附：面试评分维度

| 维度 | 考察点 |
|------|--------|
| 基础扎实度 | 能否准确描述指令编码、特权级、异常流程 |
| 工程经验 | 是否真正写过 BSP/驱动/启动代码，能否描述踩坑细节 |
| 横向对比能力 | 能否和 ARM/x86 做有深度的对比，而不是只背 RISC-V 规范 |
| 系统思维 | 能否从硬件-固件-OS 全链路理解一个功能的实现 |
| 深度追问抗压 | 追问 2-3 层后是否还能给出合理回答或诚实说不知道 |
