#include "../include/cpu_state.h"
#include "../include/instruction.h"

#include <cstdint>
#include <iostream>

// Processor 类的构造函数
Processor::Processor()
    : has_fetched_instruction(false), cycle_count(0), instruction_count(0),
      branch_mispredictions(0) {
    // 初始化CDB结果
    cdb_result.valid = false;
    cdb_result.rob_idx = 0;
    cdb_result.value = 0;
}

void Processor::tick(CPU_State &cpu) {
    cycle_count++;
    commit(cpu);            // 阶段6: 提交 (只能按顺序提交)
    writeback(cpu);         // 阶段5: 写回/广播 (乱序完成的指令)
    execute(cpu);           // 阶段4: 执行 (多条指令并行执行)
    dispatch(cpu);          // 阶段3: 分派 (多条指令同时分派到不同执行单元)
    decode_and_rename(cpu); // 阶段2: 解码与重命名 (处理依赖关系)
    fetch(cpu);             // 阶段1: 取指 (可能取多条指令)
}

// 1. 取指阶段 - 每周期最多取一条指令
void Processor::fetch(CPU_State &cpu) {
    if (cpu.fetch_stalled || cpu.pipeline_flushed) {
        cpu.pipeline_flushed = false;
        return;
    }

    // 每周期最多取一条指令
    if (cpu.rob_size < ROB_SIZE) {
        if (cpu.pc >= MEMORY_SIZE - 3) {
            cpu.fetch_stalled = true;
            return;
        }

        uint32_t instruction = static_cast<uint32_t>(cpu.memory[cpu.pc]) |
                               (static_cast<uint32_t>(cpu.memory[cpu.pc + 1]) << 8) |
                               (static_cast<uint32_t>(cpu.memory[cpu.pc + 2]) << 16) |
                               (static_cast<uint32_t>(cpu.memory[cpu.pc + 3]) << 24);

        if (instruction == HALT_INSTRUCTION) {
            cpu.fetch_stalled = true;
            return;
        }

        // 为ROB分配条目
        uint32_t rob_idx = cpu.rob_tail;
        ROBEntry &rob_entry = cpu.rob[rob_idx];
        rob_entry.busy = true;
        rob_entry.pc = cpu.pc;
        rob_entry.state = InstrState::Dispatch;
        rob_entry.ready = false;
        rob_entry.is_branch = false;

        // 基本解码
        DecodedInstruction decoded = decode_instruction(instruction, cpu.pc);
        rob_entry.instr_type = decoded.type;
        rob_entry.dest_reg = decoded.rd;
        rob_entry.rs1 = decoded.rs1;
        rob_entry.rs2 = decoded.rs2;
        rob_entry.imm = decoded.imm;

        // 分支预测
        if (is_branch_instruction(decoded.type)) {
            rob_entry.is_branch = true;
            bool predicted = predict_branch(cpu, cpu.pc);
            rob_entry.predicted_taken = predicted;

            if (predicted) {
                rob_entry.target_pc = cpu.pc + decoded.imm;
                cpu.pc = rob_entry.target_pc;
            } else {
                cpu.pc += 4;
            }
        } else {
            cpu.pc += 4;
        }

        // 更新ROB指针
        cpu.rob_tail = (cpu.rob_tail + 1) % ROB_SIZE;
        cpu.rob_size++;
        instruction_count++;
    }
}

// 2. 解码和重命名阶段 - 真正的寄存器重命名
void Processor::decode_and_rename(CPU_State &cpu) {
    // 每周期处理多条指令的重命名
    int rename_width = 2;
    int renamed_count = 0;

    for (int i = 0; i < ROB_SIZE && renamed_count < rename_width; i++) {
        uint32_t idx = (cpu.rob_head + i) % ROB_SIZE;
        if (!cpu.rob[idx].busy || cpu.rob[idx].state != InstrState::Dispatch) {
            continue;
        }

        ROBEntry &rob_entry = cpu.rob[idx];

        // 寄存器重命名的核心逻辑
        uint32_t rs1_value = 0, rs2_value = 0;
        uint32_t rs1_rob_idx = 0, rs2_rob_idx = 0;
        bool rs1_ready = true, rs2_ready = true;

        // 处理源操作数rs1
        if (rob_entry.rs1 != 0) {
            if (cpu.rat[rob_entry.rs1].busy) {
                // 有依赖关系，记录生产者的ROB索引
                rs1_rob_idx = cpu.rat[rob_entry.rs1].rob_idx;
                rs1_ready = cpu.rob[rs1_rob_idx].ready;
                if (rs1_ready) {
                    rs1_value = cpu.rob[rs1_rob_idx].value;
                }
            } else {
                // 从架构寄存器文件读取
                rs1_value = cpu.arf.regs[rob_entry.rs1];
            }
        }

        // 处理源操作数rs2
        if (rob_entry.rs2 != 0) {
            if (cpu.rat[rob_entry.rs2].busy) {
                rs2_rob_idx = cpu.rat[rob_entry.rs2].rob_idx;
                rs2_ready = cpu.rob[rs2_rob_idx].ready;
                if (rs2_ready) {
                    rs2_value = cpu.rob[rs2_rob_idx].value;
                }
            } else {
                rs2_value = cpu.arf.regs[rob_entry.rs2];
            }
        }

        // 更新RAT表：建立新的映射关系
        if (rob_entry.dest_reg != 0) {
            cpu.rat[rob_entry.dest_reg].busy = true;
            cpu.rat[rob_entry.dest_reg].rob_idx = idx;
        }

        // 准备分派信息
        rob_entry.state = InstrState::Execute;

        // 将操作数信息存储，准备分派
        // 这里我们需要一个临时的方式存储这些信息
        // 在实际的CPU中，这些信息会直接传递给分派逻辑

        renamed_count++;
    }
}

// 3. 分派阶段 - 真正的乱序分派
void Processor::dispatch(CPU_State &cpu) {
    // 找到所有可以分派的指令，根据执行单元的可用性进行分派
    int dispatch_width = 2;
    int dispatched_count = 0;

    for (int i = 0; i < ROB_SIZE && dispatched_count < dispatch_width; i++) {
        uint32_t idx = (cpu.rob_head + i) % ROB_SIZE;
        if (!cpu.rob[idx].busy || cpu.rob[idx].state != InstrState::Execute) {
            continue;
        }

        ROBEntry &rob_entry = cpu.rob[idx];
        bool dispatched = false;

        // 根据指令类型分派到不同的执行单元
        if (is_load_instruction(rob_entry.instr_type) ||
            is_store_instruction(rob_entry.instr_type)) {
            // 分派到LSQ
            uint32_t lsq_idx;
            if (allocate_lsq_entry(cpu, lsq_idx)) {
                LSQEntry &lsq = cpu.lsq[lsq_idx];
                setup_lsq_entry(cpu, lsq, rob_entry, idx);
                dispatched = true;
            }
        } else if (is_branch_instruction(rob_entry.instr_type)) {
            // 分派到分支预约站
            uint32_t rs_idx;
            if (allocate_rs_entry(cpu, rob_entry.instr_type, rs_idx)) {
                RSEntry &rs = cpu.rs_branch[rs_idx];
                setup_rs_entry(cpu, rs, rob_entry, idx);
                dispatched = true;
            }
        } else {
            // 分派到ALU预约站
            uint32_t rs_idx;
            if (allocate_rs_entry(cpu, rob_entry.instr_type, rs_idx)) {
                RSEntry &rs = cpu.rs_alu[rs_idx];
                setup_rs_entry(cpu, rs, rob_entry, idx);
                dispatched = true;
            }
        }

        if (dispatched) {
            rob_entry.state = InstrState::Writeback; // 等待执行完成
            dispatched_count++;
        }
    }
}

// 设置预约站条目 - 处理操作数依赖
void Processor::setup_rs_entry(CPU_State &cpu, RSEntry &rs, const ROBEntry &rob_entry,
                               uint32_t rob_idx) {
    rs.op = rob_entry.instr_type;
    rs.dest_rob_idx = rob_idx;
    rs.imm = rob_entry.imm;
    rs.execution_cycles_left = get_execution_cycles(rob_entry.instr_type);

    // 处理操作数依赖
    // 源操作数1
    if (rob_entry.rs1 != 0) {
        if (cpu.rat[rob_entry.rs1].busy) {
            uint32_t producer_rob = cpu.rat[rob_entry.rs1].rob_idx;
            if (cpu.rob[producer_rob].ready) {
                rs.Vj = cpu.rob[producer_rob].value;
                rs.Qj = 0;
            } else {
                rs.Vj = 0;
                rs.Qj = producer_rob;
            }
        } else {
            rs.Vj = cpu.arf.regs[rob_entry.rs1];
            rs.Qj = 0;
        }
    } else {
        rs.Vj = 0;
        rs.Qj = 0;
    }

    // 源操作数2
    if (rob_entry.rs2 != 0) {
        if (cpu.rat[rob_entry.rs2].busy) {
            uint32_t producer_rob = cpu.rat[rob_entry.rs2].rob_idx;
            if (cpu.rob[producer_rob].ready) {
                rs.Vk = cpu.rob[producer_rob].value;
                rs.Qk = 0;
            } else {
                rs.Vk = 0;
                rs.Qk = producer_rob;
            }
        } else {
            rs.Vk = cpu.arf.regs[rob_entry.rs2];
            rs.Qk = 0;
        }
    } else {
        rs.Vk = 0;
        rs.Qk = 0;
    }
}

// 设置LSQ条目
void Processor::setup_lsq_entry(CPU_State &cpu, LSQEntry &lsq, const ROBEntry &rob_entry,
                                uint32_t rob_idx) {
    lsq.op = rob_entry.instr_type;
    lsq.rob_idx = rob_idx;
    lsq.dest_rob_idx = rob_idx;
    lsq.offset = rob_entry.imm;

    // 处理基址寄存器依赖
    if (rob_entry.rs1 != 0) {
        if (cpu.rat[rob_entry.rs1].busy) {
            uint32_t producer_rob = cpu.rat[rob_entry.rs1].rob_idx;
            if (cpu.rob[producer_rob].ready) {
                lsq.base_value = cpu.rob[producer_rob].value;
                lsq.base_rob_idx = 0;
                lsq.address = lsq.base_value + lsq.offset;
                lsq.address_ready = true;
            } else {
                lsq.base_rob_idx = producer_rob;
                lsq.address_ready = false;
            }
        } else {
            lsq.base_value = cpu.arf.regs[rob_entry.rs1];
            lsq.base_rob_idx = 0;
            lsq.address = lsq.base_value + lsq.offset;
            lsq.address_ready = true;
        }
    } else {
        lsq.base_value = 0;
        lsq.base_rob_idx = 0;
        lsq.address = lsq.offset;
        lsq.address_ready = true;
    }

    // 处理STORE的数据值依赖
    if (is_store_instruction(rob_entry.instr_type)) {
        if (rob_entry.rs2 != 0) {
            if (cpu.rat[rob_entry.rs2].busy) {
                uint32_t producer_rob = cpu.rat[rob_entry.rs2].rob_idx;
                if (cpu.rob[producer_rob].ready) {
                    lsq.value = cpu.rob[producer_rob].value;
                    lsq.value_ready = true;
                } else {
                    lsq.value_ready = false;
                    // 需要等待数据就绪，这里可以添加一个等待队列
                }
            } else {
                lsq.value = cpu.arf.regs[rob_entry.rs2];
                lsq.value_ready = true;
            }
        }
    } else {
        lsq.value_ready = true; // LOAD不需要存储数据
    }
}

// 获取指令执行周期数
int Processor::get_execution_cycles(InstrType type) {
    switch (type) {
    case InstrType::ALU_ADD:
    case InstrType::ALU_SUB:
    case InstrType::ALU_AND:
    case InstrType::ALU_OR:
    case InstrType::ALU_XOR:
    case InstrType::ALU_SLL:
    case InstrType::ALU_SRL:
    case InstrType::ALU_SRA:
    case InstrType::ALU_SLT:
    case InstrType::ALU_SLTU:
    case InstrType::ALU_ADDI:
    case InstrType::ALU_ANDI:
    case InstrType::ALU_ORI:
    case InstrType::ALU_XORI:
    case InstrType::ALU_SLLI:
    case InstrType::ALU_SRLI:
    case InstrType::ALU_SRAI:
    case InstrType::ALU_SLTI:
    case InstrType::ALU_SLTIU:
    case InstrType::BRANCH_BEQ:
    case InstrType::BRANCH_BNE:
    case InstrType::BRANCH_BLT:
    case InstrType::BRANCH_BGE:
    case InstrType::BRANCH_BLTU:
    case InstrType::BRANCH_BGEU:
    case InstrType::JUMP_JAL:
    case InstrType::JUMP_JALR:
    case InstrType::LUI:
    case InstrType::AUIPC:
        return 1; // 一般操作一周期
    case InstrType::LOAD_LW:
    case InstrType::LOAD_LH:
    case InstrType::LOAD_LB:
    case InstrType::LOAD_LBU:
    case InstrType::LOAD_LHU:
    case InstrType::STORE_SW:
    case InstrType::STORE_SH:
    case InstrType::STORE_SB:
        return 3; //内存操作 3 周期

    default:
        return 1;
    }
}

// 4. 执行阶段 - 真正的并行乱序执行
void Processor::execute(CPU_State &cpu) {
    cdb_result.valid = false;

    // 并行执行多个ALU预约站中的指令
    bool cdb_used = false;

    // ALU执行单元 - 可以并行执行多条指令
    for (int i = 0; i < RS_SIZE && !cdb_used; i++) {
        RSEntry &rs = cpu.rs_alu[i];
        if (!rs.busy || !rs.operands_ready())
            continue;

        if (rs.execution_cycles_left > 0) {
            rs.execution_cycles_left--;

            if (rs.execution_cycles_left == 0) {
                // 执行完成，计算结果
                uint32_t result = execute_alu_operation(rs);

                // 广播到CDB
                broadcast_cdb_result(cpu, rs.dest_rob_idx, result);
                rs.busy = false;
                cdb_used = true; // 每周期只能有一次CDB广播
            }
        }
    }

    // 分支执行单元
    if (!cdb_used) {
        for (int i = 0; i < RS_SIZE / 2; i++) {
            RSEntry &rs = cpu.rs_branch[i];
            if (!rs.busy || !rs.operands_ready())
                continue;

            if (rs.execution_cycles_left > 0) {
                rs.execution_cycles_left--;

                if (rs.execution_cycles_left == 0) {
                    execute_branch_instruction(cpu, rs, cpu.rob[rs.dest_rob_idx]);
                    rs.busy = false;
                    break; // 分支指令特殊处理
                }
            }
        }
    }

    // Load/Store执行单元 - 处理内存访问
    execute_memory_operations(cpu);
}

// ALU操作执行
uint32_t Processor::execute_alu_operation(const RSEntry &rs) {
    uint32_t result = 0;

    switch (rs.op) {
    case InstrType::ALU_ADD:
    case InstrType::ALU_ADDI:
        result = rs.Vj + (rs.Qk == 0 ? rs.Vk : rs.imm);
        break;
    case InstrType::ALU_SUB:
        result = rs.Vj - rs.Vk;
        break;
    case InstrType::ALU_AND:
    case InstrType::ALU_ANDI:
        result = rs.Vj & (rs.Qk == 0 ? rs.Vk : rs.imm);
        break;
    case InstrType::ALU_OR:
    case InstrType::ALU_ORI:
        result = rs.Vj | (rs.Qk == 0 ? rs.Vk : rs.imm);
        break;
    case InstrType::ALU_XOR:
    case InstrType::ALU_XORI:
        result = rs.Vj ^ (rs.Qk == 0 ? rs.Vk : rs.imm);
        break;
    case InstrType::ALU_SLL:
    case InstrType::ALU_SLLI:
        result = rs.Vj << (rs.Qk == 0 ? (rs.Vk & 0x1F) : (rs.imm & 0x1F));
        break;
    case InstrType::ALU_SRL:
    case InstrType::ALU_SRLI:
        result = rs.Vj >> (rs.Qk == 0 ? (rs.Vk & 0x1F) : (rs.imm & 0x1F));
        break;
    case InstrType::ALU_SRA:
    case InstrType::ALU_SRAI:
        result = ((int32_t) rs.Vj) >> (rs.Qk == 0 ? (rs.Vk & 0x1F) : (rs.imm & 0x1F));
        break;
    case InstrType::ALU_SLT:
    case InstrType::ALU_SLTI:
        result = ((int32_t) rs.Vj < (int32_t) (rs.Qk == 0 ? rs.Vk : rs.imm)) ? 1 : 0;
        break;
    case InstrType::ALU_SLTU:
    case InstrType::ALU_SLTIU:
        result = (rs.Vj < (rs.Qk == 0 ? rs.Vk : rs.imm)) ? 1 : 0;
        break;
    case InstrType::LUI:
        result = rs.imm;
        break;
    case InstrType::AUIPC:
        // 需要从ROB获取PC值
        result = 0; // 简化实现
        break;
    default:
        result = rs.Vj + rs.Vk;
        break;
    }

    return result;
}

// 内存操作执行 - LSQ的核心逻辑
void Processor::execute_memory_operations(CPU_State &cpu) {
    // 处理所有LSQ中就绪的指令
    for (int i = 0; i < LSQ_SIZE; i++) {
        LSQEntry &lsq = cpu.lsq[i];
        if (!lsq.busy)
            continue;

        // 首先处理地址计算
        if (!lsq.address_ready && lsq.base_rob_idx == 0) {
            lsq.address = lsq.base_value + lsq.offset;
            lsq.address_ready = true;
        }

        if (!lsq.address_ready)
            continue;

        if (is_load_instruction(lsq.op)) {
            execute_load_with_dependencies(cpu, lsq);
        } else if (is_store_instruction(lsq.op)) {
            execute_store_with_dependencies(cpu, lsq);
        }
    }
}

// 考虑依赖关系的Load执行
void Processor::execute_load_with_dependencies(CPU_State &cpu, LSQEntry &lsq) {
    // 检查Store-to-Load转发
    uint32_t forwarded_value;
    if (check_memory_dependencies(cpu, lsq.address, lsq.rob_idx, forwarded_value)) {
        // 从前面的Store转发数据
        if (!cdb_result.valid) {
            broadcast_cdb_result(cpu, lsq.rob_idx, forwarded_value);
            lsq.busy = false;
        }
        return;
    }

    // 检查是否可以安全地从内存读取
    if (!can_load_proceed(cpu, lsq.address, lsq.rob_idx)) {
        return; // 等待前面的Store地址计算完成
    }

    // 从内存读取
    if (!cdb_result.valid) {
        uint32_t loaded_value = load_from_memory(cpu, lsq);
        broadcast_cdb_result(cpu, lsq.rob_idx, loaded_value);
        lsq.busy = false;
    }
}

// 考虑依赖关系的Store执行
void Processor::execute_store_with_dependencies(CPU_State &cpu, LSQEntry &lsq) {
    // Store只需要等待地址和数据都就绪
    if (lsq.address_ready && lsq.value_ready) {
        // 标记ROB条目为就绪，但不实际写入内存（直到commit）
        cpu.rob[lsq.rob_idx].ready = true;
        cpu.rob[lsq.rob_idx].state = InstrState::Writeback;
    }
}

// 从内存加载数据
uint32_t Processor::load_from_memory(CPU_State &cpu, const LSQEntry &lsq) {
    if (lsq.address >= MEMORY_SIZE)
        return 0;

    uint32_t value = 0;
    switch (lsq.op) {
    case InstrType::LOAD_LW:
        if (lsq.address + 3 < MEMORY_SIZE) {
            value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8) |
                    (cpu.memory[lsq.address + 2] << 16) | (cpu.memory[lsq.address + 3] << 24);
        }
        break;
    case InstrType::LOAD_LH:
        if (lsq.address + 1 < MEMORY_SIZE) {
            value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8);
            if (value & 0x8000)
                value |= 0xFFFF0000; // 符号扩展
        }
        break;
    case InstrType::LOAD_LB:
        value = cpu.memory[lsq.address];
        if (value & 0x80)
            value |= 0xFFFFFF00; // 符号扩展
        break;
    case InstrType::LOAD_LBU:
        value = cpu.memory[lsq.address];
        break;
    case InstrType::LOAD_LHU:
        if (lsq.address + 1 < MEMORY_SIZE) {
            value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8);
        }
        break;
    default:
        break;
    }
    return value;
}

// 5. 写回阶段
void Processor::writeback(CPU_State &cpu) {
    // CDB广播结果已经在execute阶段完成
    // 这里处理CDB结果的接收和转发

    if (cdb_result.valid) {
        // 更新ROB条目
        ROBEntry &rob = cpu.rob[cdb_result.rob_idx];
        rob.value = cdb_result.value;
        rob.ready = true;
        rob.state = InstrState::Writeback;

        // 更新所有等待这个结果的预约站
        for (int i = 0; i < RS_SIZE; i++) {
            RSEntry &rs = cpu.rs_alu[i];
            if (rs.busy) {
                if (rs.Qj == cdb_result.rob_idx) {
                    rs.Vj = cdb_result.value;
                    rs.Qj = 0;
                }
                if (rs.Qk == cdb_result.rob_idx) {
                    rs.Vk = cdb_result.value;
                    rs.Qk = 0;
                }
            }
        }

        for (int i = 0; i < RS_SIZE / 2; i++) {
            RSEntry &rs = cpu.rs_branch[i];
            if (rs.busy) {
                if (rs.Qj == cdb_result.rob_idx) {
                    rs.Vj = cdb_result.value;
                    rs.Qj = 0;
                }
                if (rs.Qk == cdb_result.rob_idx) {
                    rs.Vk = cdb_result.value;
                    rs.Qk = 0;
                }
            }
        }

        // 更新LSQ中等待的条目
        for (int i = 0; i < LSQ_SIZE; i++) {
            LSQEntry &lsq = cpu.lsq[i];
            if (lsq.busy) {
                if (lsq.base_rob_idx == cdb_result.rob_idx) {
                    lsq.base_value = cdb_result.value;
                    lsq.base_rob_idx = 0;
                    // 重新计算地址
                    lsq.address = lsq.base_value + lsq.offset;
                    lsq.address_ready = true;
                }
            }
        }
    }
}

// 6. 提交阶段
void Processor::commit(CPU_State &cpu) {
    // 永远只看ROB的头部
    if (cpu.rob_size == 0)
        return;

    ROBEntry &rob = cpu.rob[cpu.rob_head];
    if (!rob.busy || !rob.ready)
        return;

    // 检查是否是分支指令且预测错误
    if (rob.is_branch) {
        if (rob.predicted_taken != rob.actual_taken) {
            // 分支预测错误，需要冲刷流水线
            branch_mispredictions++;
            handle_branch_misprediction(cpu, rob.actual_taken ? rob.target_pc : rob.pc + 4);
            return;
        }
    }

    // 正常提交指令
    commit_instruction(cpu, rob);

    // 更新RAT
    if (rob.dest_reg != 0 && cpu.rat[rob.dest_reg].rob_idx == cpu.rob_head) {
        cpu.rat[rob.dest_reg].busy = false;
    }

    // 释放ROB条目
    rob.busy = false;
    cpu.rob_head = (cpu.rob_head + 1) % ROB_SIZE;
    cpu.rob_size--;
}

// 辅助函数实现
bool Processor::is_alu_instruction(InstrType type) {
    return type >= InstrType::ALU_ADD && type <= InstrType::ALU_SLTIU;
}

bool Processor::is_branch_instruction(InstrType type) {
    return type >= InstrType::BRANCH_BEQ && type <= InstrType::BRANCH_BGEU;
}

bool Processor::is_load_instruction(InstrType type) {
    return type >= InstrType::LOAD_LB && type <= InstrType::LOAD_LHU;
}

bool Processor::is_store_instruction(InstrType type) {
    return type >= InstrType::STORE_SB && type <= InstrType::STORE_SW;
}

void Processor::broadcast_cdb_result(CPU_State &cpu, uint32_t rob_index, uint32_t value) {
    cdb_result.valid = true;
    cdb_result.rob_idx = rob_index;
    cdb_result.value = value;
}

bool Processor::allocate_rob_entry(CPU_State &cpu, uint32_t &rob_idx) {
    if (cpu.rob_size >= ROB_SIZE)
        return false;

    rob_idx = cpu.rob_tail;
    cpu.rob_tail = (cpu.rob_tail + 1) % ROB_SIZE;
    cpu.rob_size++;
    return true;
}

bool Processor::allocate_rs_entry(CPU_State &cpu, InstrType type, uint32_t &rs_idx) {
    RSEntry *rs_array;
    int rs_size;

    if (is_branch_instruction(type)) {
        rs_array = cpu.rs_branch;
        rs_size = RS_SIZE / 2;
    } else {
        rs_array = cpu.rs_alu;
        rs_size = RS_SIZE;
    }

    for (int i = 0; i < rs_size; i++) {
        if (!rs_array[i].busy) {
            rs_array[i].busy = true;
            rs_idx = i;
            return true;
        }
    }
    return false;
}

bool Processor::allocate_lsq_entry(CPU_State &cpu, uint32_t &lsq_idx) {
    for (int i = 0; i < LSQ_SIZE; i++) {
        if (!cpu.lsq[i].busy) {
            cpu.lsq[i].busy = true;
            lsq_idx = i;
            return true;
        }
    }
    return false;
}

bool Processor::predict_branch(CPU_State &cpu, uint32_t pc) { return cpu.branch_predictor; }

void Processor::handle_branch_misprediction(CPU_State &cpu, uint32_t correct_pc) {
    flush_pipeline(cpu);
    cpu.pc = correct_pc;
}

void Processor::flush_pipeline(CPU_State &cpu) {
    // 清空ROB、RS、LSQ
    for (int i = 0; i < ROB_SIZE; i++) {
        cpu.rob[i].busy = false;
    }
    for (int i = 0; i < RS_SIZE; i++) {
        cpu.rs_alu[i].busy = false;
    }
    for (int i = 0; i < RS_SIZE / 2; i++) {
        cpu.rs_branch[i].busy = false;
    }
    for (int i = 0; i < LSQ_SIZE; i++) {
        cpu.lsq[i].busy = false;
    }

    // 清空RAT
    for (int i = 0; i < 32; i++) {
        cpu.rat[i].busy = false;
    }

    cpu.rob_head = 0;
    cpu.rob_tail = 0;
    cpu.rob_size = 0;
    cpu.pipeline_flushed = true;
}

// LSQ相关的关键函数实现
void Processor::execute_load(CPU_State &cpu, LSQEntry &lsq, ROBEntry &rob) {
    if (!lsq.address_ready)
        return;

    // 关键步骤：检查内存依赖
    uint32_t forwarded_value;
    if (check_memory_dependencies(cpu, lsq.address, lsq.rob_idx, forwarded_value)) {
        // 可以从Store-to-Load Forwarding获得数据
        broadcast_cdb_result(cpu, lsq.rob_idx, forwarded_value);
        lsq.busy = false;
        return;
    }

    // 检查是否可以进行Load操作
    if (!can_load_proceed(cpu, lsq.address, lsq.rob_idx)) {
        // 必须等待前面的Store地址计算完成
        return;
    }

    // 可以安全地从内存读取
    if (lsq.address < MEMORY_SIZE) {
        uint32_t loaded_value = 0;
        switch (lsq.op) {
        case InstrType::LOAD_LW:
            if (lsq.address + 3 < MEMORY_SIZE) {
                loaded_value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8) |
                               (cpu.memory[lsq.address + 2] << 16) |
                               (cpu.memory[lsq.address + 3] << 24);
            }
            break;
        case InstrType::LOAD_LH:
            if (lsq.address + 1 < MEMORY_SIZE) {
                loaded_value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8);
                // 符号扩展
                if (loaded_value & 0x8000) {
                    loaded_value |= 0xFFFF0000;
                }
            }
            break;
        case InstrType::LOAD_LB:
            loaded_value = cpu.memory[lsq.address];
            // 符号扩展
            if (loaded_value & 0x80) {
                loaded_value |= 0xFFFFFF00;
            }
            break;
        case InstrType::LOAD_LBU:
            loaded_value = cpu.memory[lsq.address];
            break;
        case InstrType::LOAD_LHU:
            if (lsq.address + 1 < MEMORY_SIZE) {
                loaded_value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8);
            }
            break;
        default:
            // 其他指令类型不是load指令
            break;
        }

        broadcast_cdb_result(cpu, lsq.rob_idx, loaded_value);
        lsq.busy = false;
    }
}

void Processor::execute_store(CPU_State &cpu, LSQEntry &lsq, ROBEntry &rob) {
    // STORE指令只计算地址和准备数据，不实际写入内存
    // 实际写入在commit阶段进行

    if (lsq.address_ready && lsq.value_ready) {
        // 地址和数据都准备好了，标记为就绪
        rob.ready = true;
        rob.state = InstrState::Writeback;
    }
}

bool Processor::check_memory_dependencies(const CPU_State &cpu, uint32_t load_address,
                                          uint32_t load_rob_idx, uint32_t &forwarded_value) {
    // 遍历LSQ中所有在load之前的store指令
    for (int i = 0; i < LSQ_SIZE; i++) {
        const LSQEntry &store_entry = cpu.lsq[i];
        if (!store_entry.busy || !is_store_instruction(store_entry.op))
            continue;

        // 检查是否是程序顺序上更早的store
        if (store_entry.rob_idx >= load_rob_idx)
            continue;

        // 如果store的地址还没准备好，保守地阻塞load
        if (!store_entry.address_ready) {
            return false; // 需要等待
        }

        // 检查地址是否匹配
        if (store_entry.address == load_address && store_entry.value_ready) {
            // Store-to-Load Forwarding
            forwarded_value = store_entry.value;
            return true;
        }
    }

    return false; // 没有匹配的store，可以从内存读取
}

bool Processor::can_load_proceed(const CPU_State &cpu, uint32_t load_address,
                                 uint32_t load_rob_idx) {
    // 检查是否有程序顺序上更早的store的地址还未计算
    for (int i = 0; i < LSQ_SIZE; i++) {
        const LSQEntry &store_entry = cpu.lsq[i];
        if (!store_entry.busy || !is_store_instruction(store_entry.op))
            continue;

        if (store_entry.rob_idx < load_rob_idx && !store_entry.address_ready) {
            // 有更早的store地址未知，必须等待
            return false;
        }
    }

    return true;
}

void Processor::commit_instruction(CPU_State &cpu, ROBEntry &rob) {
    if (rob.dest_reg != 0) {
        // 写入架构寄存器文件
        cpu.arf.regs[rob.dest_reg] = rob.value;
    }

    // 如果是store指令，实际写入内存
    if (is_store_instruction(rob.instr_type)) {
        // 找到对应的LSQ条目
        for (int i = 0; i < LSQ_SIZE; i++) {
            LSQEntry &lsq = cpu.lsq[i];
            if (lsq.busy && lsq.rob_idx == cpu.rob_head) {
                commit_store(cpu, rob, lsq);
                lsq.busy = false;
                break;
            }
        }
    }
}

void Processor::commit_store(CPU_State &cpu, const ROBEntry &rob, const LSQEntry &lsq) {
    // 现在才真正写入内存
    if (lsq.address < MEMORY_SIZE) {
        switch (lsq.op) {
        case InstrType::STORE_SW:
            if (lsq.address + 3 < MEMORY_SIZE) {
                cpu.memory[lsq.address] = lsq.value & 0xFF;
                cpu.memory[lsq.address + 1] = (lsq.value >> 8) & 0xFF;
                cpu.memory[lsq.address + 2] = (lsq.value >> 16) & 0xFF;
                cpu.memory[lsq.address + 3] = (lsq.value >> 24) & 0xFF;
            }
            break;
        case InstrType::STORE_SH:
            if (lsq.address + 1 < MEMORY_SIZE) {
                cpu.memory[lsq.address] = lsq.value & 0xFF;
                cpu.memory[lsq.address + 1] = (lsq.value >> 8) & 0xFF;
            }
            break;
        case InstrType::STORE_SB:
            cpu.memory[lsq.address] = lsq.value & 0xFF;
            break;
        default:
            // 其他指令类型不是store指令
            break;
        }
    }
}

void Processor::execute_branch_instruction(CPU_State &cpu, RSEntry &rs, ROBEntry &rob) {
    bool taken = false;

    switch (rs.op) {
    case InstrType::BRANCH_BEQ:
        taken = (rs.Vj == rs.Vk);
        break;
    case InstrType::BRANCH_BNE:
        taken = (rs.Vj != rs.Vk);
        break;
    case InstrType::BRANCH_BLT:
        taken = ((int32_t) rs.Vj < (int32_t) rs.Vk);
        break;
    case InstrType::BRANCH_BGE:
        taken = ((int32_t) rs.Vj >= (int32_t) rs.Vk);
        break;
    case InstrType::BRANCH_BLTU:
        taken = (rs.Vj < rs.Vk);
        break;
    case InstrType::BRANCH_BGEU:
        taken = (rs.Vj >= rs.Vk);
        break;
    default:
        // 其他指令类型不是分支指令
        taken = false;
        break;
    }

    rob.actual_taken = taken;
    rob.ready = true;
    rob.state = InstrState::Writeback;

    // 更新分支预测器
    cpu.branch_predictor = taken;
}

// 临时的decode_instruction实现（需要根据实际RISC-V指令格式完善）
DecodedInstruction Processor::decode_instruction(uint32_t instruction, uint32_t pc) {
    DecodedInstruction decoded;
    // 这里需要实现完整的RISC-V指令解码
    // 暂时返回基本结构
    decoded.raw_instr = instruction;
    decoded.pc = pc;
    decoded.rd = (instruction >> 7) & 0x1F;
    decoded.rs1 = (instruction >> 15) & 0x1F;
    decoded.rs2 = (instruction >> 20) & 0x1F;
    decoded.imm = 0; // 需要根据指令类型计算
    decoded.type = get_instruction_type(instruction);
    return decoded;
}

InstrType Processor::get_instruction_type(uint32_t instruction) {
    // 根据opcode和func字段确定指令类型
    uint32_t opcode = instruction & 0x7F;

    switch (opcode) {
    case 0x33:                     // R-type
        return InstrType::ALU_ADD; // 简化实现
    case 0x13:                     // I-type immediate
        return InstrType::ALU_ADDI;
    case 0x03: // Load
        return InstrType::LOAD_LW;
    case 0x23: // Store
        return InstrType::STORE_SW;
    case 0x63: // Branch
        return InstrType::BRANCH_BEQ;
    default:
        return InstrType::ALU_ADD;
    }
}

bool Processor::rob_full(const CPU_State &cpu) { return cpu.rob_size >= ROB_SIZE; }

bool Processor::rob_empty(const CPU_State &cpu) { return cpu.rob_size == 0; }
