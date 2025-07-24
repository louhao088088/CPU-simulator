# RISC-V CPU 模拟器

这是一个简单的 RISC-V CPU 模拟器，支持基本的 RISC-V 指令集。

## 项目结构

```
├── include/              # 头文件
│   ├── cpu_state.h      # CPU状态定义
│   ├── instruction.h    # 指令处理
│   └── riscv_simulator.h# 模拟器主类
├── src/                 # 源代码
│   ├── cpu_state.cpp
│   ├── instruction.cpp
│   └── riscv_simulator.cpp
├── main.cpp             # 程序入口
├── sample/              # 样本测试数据
└── reference/           # 参考文档
```

## 支持的指令

- **U-type**: LUI, AUIPC
- **J-type**: JAL, JALR
- **B-type**: BEQ, BNE, BLT, BGE, BLTU, BGEU
- **I-type**: ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI
- **I-type Load**: LB, LH, LW, LBU, LHU
- **S-type**: SB, SH, SW
- **R-type**: ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND

## 版本说明

- **原始版本**: `simpleCPU.cpp` 单文件实现单流水 CPU

## 注意事项

- 程序会在遇到 `0x0ff00513` 指令时停止执行
- 结果输出为寄存器 x10 的低 8 位
- 内存大小限制为 1MB
