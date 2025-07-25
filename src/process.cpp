#include "../include/process.h"

#include <iostream>

CPUCore::CPUCore() : cycle_count_(0), instruction_count_(0), branch_mispredictions_(0) {}

void CPUCore::tick(CPU_State &cpu) {
    // 按倒序执行各个阶段，防止指令在一个周期内穿越多个阶段
    commit_stage(cpu);
    writeback_stage(cpu);
    execute_stage(cpu);
    dispatch_stage(cpu);
    decode_rename_stage(cpu);
    fetch_stage(cpu);

    ++cycle_count_;
}

void CPUCore::fetch_stage(CPU_State &cpu) {
    // 如果ROB满了或取指被阻塞，则停止取指
    if (rob_full(cpu) || cpu.fetch_stalled) {
        return;
    }

    // 如果取指缓存满了，则等待
    if (cpu.fetch_buffer_size >= FETCH_BUFFER_SIZE) {
        return;
    }
    

    // 从内存取指令
    if (cpu.pc < MEMORY_SIZE - 3) {
        // 手动组装32位指令（小端字节序）
        uint32_t instruction = cpu.memory[cpu.pc] | (cpu.memory[cpu.pc + 1] << 8) |
                               (cpu.memory[cpu.pc + 2] << 16) | (cpu.memory[cpu.pc + 3] << 24);

        // 添加到取指缓存
        FetchBufferEntry &entry = cpu.fetch_buffer[cpu.fetch_buffer_tail];
        entry.valid = true;
        entry.instruction = instruction;
        entry.pc = cpu.pc;

        cout << "Fetched instruction: " << std::hex << instruction << " at PC: " << cpu.pc
             << std::dec << std::endl;

        cpu.fetch_buffer_tail = (cpu.fetch_buffer_tail + 1) % FETCH_BUFFER_SIZE;
        cpu.fetch_buffer_size++;

        // 简单分支预测：预测不跳转，PC+4
        cpu.pc += 4;
    } else {
        // PC超出范围，停止取指
        cpu.fetch_stalled = true;
    }
}

void CPUCore::decode_rename_stage(CPU_State &cpu) {
    // 如果取指缓存为空，则无法解码
    if (cpu.fetch_buffer_size == 0) {
        return;
    }

    // 如果ROB或RS满了，则停止解码
    if (rob_full(cpu)) {
        return;
    }

    // 从取指缓存获取指令
    FetchBufferEntry &fetch_entry = cpu.fetch_buffer[cpu.fetch_buffer_head];
    if (!fetch_entry.valid) {
        return;
    }

    // 解码指令
    Instruction instr = InstructionProcessor::decode(fetch_entry.instruction, fetch_entry.pc);

    // 检查是否有可用的预约站
    if (InstructionProcessor::is_alu_type(instr.type) ||
        InstructionProcessor::is_branch_type(instr.type)) {
        if (!rs_available(cpu, instr.type)) {
            return; // 预约站满，等待
        }
    } else if (InstructionProcessor::is_load_type(instr.type) ||
               InstructionProcessor::is_store_type(instr.type)) {
        if (!lsq_available(cpu)) {
            return; // LSQ满，等待
        }
    }

    // 分配ROB条目
    uint32_t rob_idx = allocate_rob_entry(cpu);

    // 设置ROB条目
    ROBEntry &rob_entry = cpu.rob[rob_idx];
    rob_entry.busy = true;
    rob_entry.instr_type = instr.type;
    rob_entry.state = InstrState::Dispatch;
    rob_entry.dest_reg = instr.rd;
    rob_entry.pc = instr.pc;
    rob_entry.rs1 = instr.rs1;
    rob_entry.rs2 = instr.rs2;
    rob_entry.imm = instr.imm;
    rob_entry.ready = false;

    // 处理分支指令
    if (InstructionProcessor::is_branch_type(instr.type)) {
        rob_entry.is_branch = true;
        rob_entry.predicted_taken = predict_branch_taken(cpu);
        rob_entry.target_pc = instr.pc + instr.imm;
    }

    // 寄存器重命名
    rename_registers(cpu, instr, rob_idx);

    // 消费取指缓存条目
    fetch_entry.valid = false;
    cpu.fetch_buffer_head = (cpu.fetch_buffer_head + 1) % FETCH_BUFFER_SIZE;
    cpu.fetch_buffer_size--;

    ++instruction_count_;
}

void CPUCore::dispatch_stage(CPU_State &cpu) {
    // 扫描ROB，找到处于Dispatch状态的指令进行分派
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        ROBEntry &rob_entry = cpu.rob[i];
        if (!rob_entry.busy || rob_entry.state != InstrState::Dispatch) {
            continue;
        }

        // ALU指令分派到预约站
        if (InstructionProcessor::is_alu_type(rob_entry.instr_type) ||
            InstructionProcessor::is_branch_type(rob_entry.instr_type)) {

            if (!rs_available(cpu, rob_entry.instr_type)) {
                continue; // 没有可用预约站，跳过
            }

            uint32_t rs_idx = allocate_rs_entry(cpu, rob_entry.instr_type);
            RSEntry &rs_entry = cpu.rs_alu[rs_idx];
            rs_entry.busy = true;
            rs_entry.op = rob_entry.instr_type;
            rs_entry.dest_rob_idx = i;
            rs_entry.imm = rob_entry.imm;

            // 读取第一个操作数
            rs_entry.Vj = read_operand(cpu, rob_entry.rs1, rs_entry.Qj);

            // 判断是否需要第二个操作数
            bool needs_rs2 = false;
            if (InstructionProcessor::is_alu_type(rob_entry.instr_type)) {
                // R-type指令需要rs2，I-type指令不需要
                needs_rs2 = (rob_entry.instr_type >= InstrType::ALU_ADD &&
                             rob_entry.instr_type <= InstrType::ALU_SLTU);
            } else if (InstructionProcessor::is_branch_type(rob_entry.instr_type)) {
                needs_rs2 = true; // 分支指令需要比较两个操作数
            }

            if (needs_rs2) {
                rs_entry.Vk = read_operand(cpu, rob_entry.rs2, rs_entry.Qk);
            } else {
                rs_entry.Vk = 0;
                rs_entry.Qk = 0;
            }

            rob_entry.state = InstrState::Execute;
        }
        // Load/Store指令分派到LSQ
        else if (InstructionProcessor::is_load_type(rob_entry.instr_type) ||
                 InstructionProcessor::is_store_type(rob_entry.instr_type)) {

            if (!lsq_available(cpu)) {
                continue; // LSQ满，跳过
            }

            uint32_t lsq_idx = allocate_lsq_entry(cpu);
            LSQEntry &lsq_entry = cpu.lsq[lsq_idx];
            lsq_entry.busy = true;
            lsq_entry.op = rob_entry.instr_type;
            lsq_entry.dest_rob_idx = i;
            lsq_entry.rob_idx = i;
            lsq_entry.offset = rob_entry.imm;

            // 读取基址寄存器
            lsq_entry.base_value = read_operand(cpu, rob_entry.rs1, lsq_entry.base_rob_idx);
            lsq_entry.address_ready = (lsq_entry.base_rob_idx == 0);

            // Store指令还需要读取要存储的值
            if (InstructionProcessor::is_store_type(rob_entry.instr_type)) {
                uint32_t value_rob_idx;
                lsq_entry.value = read_operand(cpu, rob_entry.rs2, value_rob_idx);
                lsq_entry.value_ready = (value_rob_idx == 0);

                // 如果value依赖于某个ROB条目，需要记录依赖关系
                // 这里简化处理，实际需要更复杂的依赖跟踪
            } else {
                lsq_entry.value_ready = true;
            }

            rob_entry.state = InstrState::Execute;
        }
        // 特殊指令处理（LUI, AUIPC, JAL, JALR, HALT）
        else if (rob_entry.instr_type == InstrType::LUI ||
                 rob_entry.instr_type == InstrType::AUIPC ||
                 rob_entry.instr_type == InstrType::JUMP_JAL ||
                 rob_entry.instr_type == InstrType::JUMP_JALR) {

            if (!rs_available(cpu, rob_entry.instr_type)) {
                continue; // 没有可用预约站，跳过
            }

            uint32_t rs_idx = allocate_rs_entry(cpu, rob_entry.instr_type);
            RSEntry &rs_entry = cpu.rs_alu[rs_idx];
            rs_entry.busy = true;
            rs_entry.op = rob_entry.instr_type;
            rs_entry.dest_rob_idx = i;
            rs_entry.imm = rob_entry.imm;

            // 对于JALR，需要读取rs1寄存器
            if (rob_entry.instr_type == InstrType::JUMP_JALR) {
                rs_entry.Vj = read_operand(cpu, rob_entry.rs1, rs_entry.Qj);
            } else {
                rs_entry.Vj = 0;
                rs_entry.Qj = 0;
            }

            rs_entry.Vk = 0;
            rs_entry.Qk = 0;

            rob_entry.state = InstrState::Execute;
        }
        // HALT指令处理
        else if (rob_entry.instr_type == InstrType::HALT) {
            cpu.fetch_stalled = true;
            rob_entry.ready = true;
            rob_entry.value = 0;
            rob_entry.state = InstrState::Writeback;
        }
    }
}

void CPUCore::execute_stage(CPU_State &cpu) {
    // 执行预约站中就绪的指令
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        RSEntry &rs_entry = cpu.rs_alu[i];
        if (!rs_entry.busy || !rs_entry.operands_ready()) {
            continue;
        }

        // 开始或继续执行
        if (rs_entry.execution_cycles_left == 0) {
            rs_entry.execution_cycles_left =
                InstructionProcessor::get_execution_cycles(rs_entry.op);
        }

        rs_entry.execution_cycles_left--;

        // 执行完成
        if (rs_entry.execution_cycles_left == 0) {
            uint32_t result = 0;

            if (InstructionProcessor::is_alu_type(rs_entry.op) || rs_entry.op == InstrType::LUI ||
                rs_entry.op == InstrType::AUIPC || rs_entry.op == InstrType::JUMP_JAL ||
                rs_entry.op == InstrType::JUMP_JALR) {

                // 对于AUIPC、JAL、JALR，第一个操作数应该是PC
                if (rs_entry.op == InstrType::AUIPC || rs_entry.op == InstrType::JUMP_JAL ||
                    rs_entry.op == InstrType::JUMP_JALR) {
                    ROBEntry &rob_entry = cpu.rob[rs_entry.dest_rob_idx];
                    result = InstructionProcessor::execute_alu(rs_entry.op, rob_entry.pc,
                                                               rs_entry.Vk, rs_entry.imm);

                    // 跳转指令在这里计算目标地址，但不立即更新PC
                    // PC的更新应该在commit阶段进行
                    if (rs_entry.op == InstrType::JUMP_JAL) {
                        rob_entry.target_pc = rob_entry.pc + rs_entry.imm;
                        rob_entry.is_branch = true; // 标记为需要更新PC
                    } else if (rs_entry.op == InstrType::JUMP_JALR) {
                        rob_entry.target_pc = (rs_entry.Vj + rs_entry.imm) & ~1;
                        rob_entry.is_branch = true; // 标记为需要更新PC
                    }
                } else {
                    result = InstructionProcessor::execute_alu(rs_entry.op, rs_entry.Vj,
                                                               rs_entry.Vk, rs_entry.imm);
                }
            } else if (InstructionProcessor::is_branch_type(rs_entry.op)) {
                // 分支指令计算结果并检查预测
                bool taken = InstructionProcessor::check_branch_condition(rs_entry.op, rs_entry.Vj,
                                                                          rs_entry.Vk);
                ROBEntry &rob_entry = cpu.rob[rs_entry.dest_rob_idx];
                rob_entry.actual_taken = taken;

                // 计算实际目标地址
                if (taken) {
                    rob_entry.target_pc = rob_entry.pc + rob_entry.imm;
                } else {
                    rob_entry.target_pc = rob_entry.pc + 4;
                }

                // 检查分支预测
                if (rob_entry.predicted_taken != taken) {
                    handle_branch_misprediction(cpu, rob_entry.target_pc);
                }

                // 分支指令不需要写回寄存器，结果用于PC更新
                result = rob_entry.target_pc;
            }

            // 广播结果
            broadcast_result(cpu, rs_entry.dest_rob_idx, result);

            // 释放预约站
            rs_entry.busy = false;
        }
    }

    // 执行LSQ中的Load/Store指令
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        LSQEntry &lsq_entry = cpu.lsq[i];
        if (!lsq_entry.busy) {
            continue;
        }

        // 计算地址
        if (!lsq_entry.address_ready) {
            if (lsq_entry.base_rob_idx == 0) { // 基址就绪
                lsq_entry.address = lsq_entry.base_value + lsq_entry.offset;
                lsq_entry.address_ready = true;
            }
        }

        if (!lsq_entry.address_ready) {
            continue;
        }

        // 执行Load指令
        if (InstructionProcessor::is_load_type(lsq_entry.op)) {
            uint32_t forwarded_value;
            if (check_load_dependencies(cpu, lsq_entry.address, lsq_entry.rob_idx,
                                        forwarded_value)) {
                // Store-to-Load转发
                broadcast_result(cpu, lsq_entry.dest_rob_idx, forwarded_value);
                lsq_entry.busy = false;
            } else {
                // 从内存读取
                if (lsq_entry.address < MEMORY_SIZE) {
                    uint32_t value = 0;
                    switch (lsq_entry.op) {
                    case InstrType::LOAD_LB:
                        value = static_cast<int32_t>(
                            static_cast<int8_t>(cpu.memory[lsq_entry.address]));
                        break;
                    case InstrType::LOAD_LBU:
                        value = cpu.memory[lsq_entry.address];
                        break;
                    case InstrType::LOAD_LH:
                        value = static_cast<int32_t>(static_cast<int16_t>(
                            *reinterpret_cast<uint16_t *>(&cpu.memory[lsq_entry.address])));
                        break;
                    case InstrType::LOAD_LHU:
                        value = *reinterpret_cast<uint16_t *>(&cpu.memory[lsq_entry.address]);
                        break;
                    case InstrType::LOAD_LW:
                        value = *reinterpret_cast<uint32_t *>(&cpu.memory[lsq_entry.address]);
                        break;
                    default:
                        value = 0;
                        break;
                    }
                    broadcast_result(cpu, lsq_entry.dest_rob_idx, value);
                    lsq_entry.busy = false;
                }
            }
        }
        // Store指令在commit阶段才真正写入内存
    }
}

void CPUCore::writeback_stage(CPU_State &cpu) {
    // 检查并更新所有就绪但尚未writeback的ROB条目
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        ROBEntry &rob_entry = cpu.rob[i];
        if (rob_entry.busy && rob_entry.ready && rob_entry.state == InstrState::Execute) {
            rob_entry.state = InstrState::Writeback;
        }
    }

    // 处理LSQ中完成的Load指令的writeback
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        LSQEntry &lsq_entry = cpu.lsq[i];
        if (lsq_entry.busy && InstructionProcessor::is_load_type(lsq_entry.op)) {
            ROBEntry &rob_entry = cpu.rob[lsq_entry.dest_rob_idx];
            if (rob_entry.ready && rob_entry.state == InstrState::Execute) {
                rob_entry.state = InstrState::Writeback;
            }
        }
    }

    // 更新所有writeback完成的指令状态为可提交
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        ROBEntry &rob_entry = cpu.rob[i];
        if (rob_entry.busy && rob_entry.ready && rob_entry.state == InstrState::Writeback) {
            rob_entry.state = InstrState::Commit;
        }
    }
}

void CPUCore::commit_stage(CPU_State &cpu) {
    if (rob_empty(cpu)) {
        return;
    }

    ROBEntry &rob_entry = cpu.rob[cpu.rob_head];
    if (!rob_entry.busy || !rob_entry.ready || rob_entry.state != InstrState::Commit) {
        return;
    }

    // 提交指令
    if (rob_entry.instr_type == InstrType::HALT) {
        cpu.fetch_stalled = true; // 停止取指
        return;                   // HALT指令直接结束模拟
    }

    if (rob_entry.dest_reg != 0) { // x0寄存器不能写入
        cpu.arf.regs[rob_entry.dest_reg] = rob_entry.value;

        // 如果RAT仍然指向这个ROB条目，则清除
        if (cpu.rat[rob_entry.dest_reg].busy &&
            cpu.rat[rob_entry.dest_reg].rob_idx == cpu.rob_head) {
            cpu.rat[rob_entry.dest_reg].busy = false;
        }
    }

    // 处理跳转指令的PC更新
    if (rob_entry.is_branch && (rob_entry.instr_type == InstrType::JUMP_JAL ||
                                rob_entry.instr_type == InstrType::JUMP_JALR)) {
        cpu.pc = rob_entry.target_pc;
    }

    // 处理分支指令
    if (rob_entry.is_branch && InstructionProcessor::is_branch_type(rob_entry.instr_type)) {
        return; // 分支指令在execute阶段已经处理过了
    }

    // 处理Store指令的内存写入
    if (InstructionProcessor::is_store_type(rob_entry.instr_type)) {
        // 找到对应的LSQ条目
        for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
            LSQEntry &lsq_entry = cpu.lsq[i];
            if (lsq_entry.busy && lsq_entry.rob_idx == cpu.rob_head) {
                // 真正写入内存
                if (lsq_entry.address < MEMORY_SIZE && lsq_entry.value_ready) {
                    switch (lsq_entry.op) {
                    case InstrType::STORE_SB:
                        cpu.memory[lsq_entry.address] = static_cast<uint8_t>(lsq_entry.value);
                        break;
                    case InstrType::STORE_SH:
                        *reinterpret_cast<uint16_t *>(&cpu.memory[lsq_entry.address]) =
                            static_cast<uint16_t>(lsq_entry.value);
                        break;
                    case InstrType::STORE_SW:
                        *reinterpret_cast<uint32_t *>(&cpu.memory[lsq_entry.address]) =
                            lsq_entry.value;
                        break;
                    default:
                        break;
                    }
                }
                lsq_entry.busy = false;
                break;
            }
        }
    }

    // 释放ROB条目
    free_rob_entry(cpu);
}

// ROB管理函数
bool CPUCore::rob_full(const CPU_State &cpu) const { return cpu.rob_size >= ROB_SIZE; }

bool CPUCore::rob_empty(const CPU_State &cpu) const { return cpu.rob_size == 0; }

uint32_t CPUCore::allocate_rob_entry(CPU_State &cpu) {
    uint32_t rob_idx = cpu.rob_tail;
    cpu.rob_tail = (cpu.rob_tail + 1) % ROB_SIZE;
    cpu.rob_size++;
    return rob_idx;
}

void CPUCore::free_rob_entry(CPU_State &cpu) {
    cpu.rob[cpu.rob_head].busy = false;
    cpu.rob_head = (cpu.rob_head + 1) % ROB_SIZE;
    cpu.rob_size--;
}

// 预约站管理
bool CPUCore::rs_available(const CPU_State &cpu, InstrType type) const {
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        if (!cpu.rs_alu[i].busy) {
            return true;
        }
    }
    return false;
}

uint32_t CPUCore::allocate_rs_entry(CPU_State &cpu, InstrType type) {
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        if (!cpu.rs_alu[i].busy) {
            return i;
        }
    }
    return 0; // 应该不会到这里，因为调用前已经检查过可用性
}

void CPUCore::free_rs_entry(CPU_State &cpu, uint32_t rs_idx, InstrType type) {
    cpu.rs_alu[rs_idx].busy = false;
}

// LSQ管理
bool CPUCore::lsq_available(const CPU_State &cpu) const {
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        if (!cpu.lsq[i].busy) {
            return true;
        }
    }
    return false;
}

uint32_t CPUCore::allocate_lsq_entry(CPU_State &cpu) {
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        if (!cpu.lsq[i].busy) {
            return i;
        }
    }
    return 0;
}

void CPUCore::free_lsq_entry(CPU_State &cpu, uint32_t lsq_idx) { cpu.lsq[lsq_idx].busy = false; }

// 寄存器重命名
void CPUCore::rename_registers(CPU_State &cpu, const Instruction &instr, uint32_t rob_idx) {
    // 更新目标寄存器的RAT条目
    if (instr.rd != 0) { // x0寄存器不重命名
        cpu.rat[instr.rd].busy = true;
        cpu.rat[instr.rd].rob_idx = rob_idx;
    }
}

uint32_t CPUCore::read_operand(const CPU_State &cpu, uint32_t reg_idx, uint32_t &rob_dependency) {
    if (reg_idx == 0) {
        rob_dependency = 0;
        return 0; // x0寄存器总是0
    }

    if (cpu.rat[reg_idx].busy) {
        // 寄存器被重命名，检查ROB是否有结果
        uint32_t rob_idx = cpu.rat[reg_idx].rob_idx;
        if (cpu.rob[rob_idx].ready) {
            rob_dependency = 0;
            return cpu.rob[rob_idx].value;
        } else {
            rob_dependency = rob_idx;
            return 0;
        }
    } else {
        // 从架构寄存器文件读取
        rob_dependency = 0;
        return cpu.arf.regs[reg_idx];
    }
}

// CDB广播
void CPUCore::broadcast_result(CPU_State &cpu, uint32_t rob_idx, uint32_t value) {
    // 更新ROB条目
    cpu.rob[rob_idx].value = value;
    cpu.rob[rob_idx].ready = true;
    cpu.rob[rob_idx].state = InstrState::Writeback;

    // 更新所有等待这个结果的预约站
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        RSEntry &rs = cpu.rs_alu[i];
        if (rs.busy) {
            if (rs.Qj == rob_idx) {
                rs.Vj = value;
                rs.Qj = 0;
            }
            if (rs.Qk == rob_idx) {
                rs.Vk = value;
                rs.Qk = 0;
            }
        }
    }

    // 更新LSQ中等待这个结果的条目
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        LSQEntry &lsq = cpu.lsq[i];
        if (lsq.busy && lsq.base_rob_idx == rob_idx) {
            lsq.base_value = value;
            lsq.base_rob_idx = 0;
        }
    }
}

// 内存依赖检查
bool CPUCore::check_load_dependencies(const CPU_State &cpu, uint32_t load_addr,
                                      uint32_t load_rob_idx, uint32_t &forwarded_value) {
    // 检查所有更早的Store指令
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        const LSQEntry &lsq = cpu.lsq[i];
        if (lsq.busy && InstructionProcessor::is_store_type(lsq.op) && lsq.rob_idx < load_rob_idx) {

            if (!lsq.address_ready) {
                // Store地址未准备好，Load必须等待
                return false;
            }

            if (lsq.address == load_addr && lsq.value_ready) {
                // 地址匹配且数据就绪，进行Store-to-Load转发
                forwarded_value = lsq.value;
                return true;
            }
        }
    }

    return false; // 没有依赖，可以从内存读取
}

// 分支预测
bool CPUCore::predict_branch_taken(const CPU_State &cpu) { return false; }

void CPUCore::handle_branch_misprediction(CPU_State &cpu, uint32_t correct_pc) {
    ++branch_mispredictions_;
    cpu.pc = correct_pc;
    flush_pipeline(cpu);
}

void CPUCore::flush_pipeline(CPU_State &cpu) {
    // 清空取指缓存
    for (int i = 0; i < FETCH_BUFFER_SIZE; ++i) {
        cpu.fetch_buffer[i].valid = false;
    }
    cpu.fetch_buffer_head = 0;
    cpu.fetch_buffer_tail = 0;
    cpu.fetch_buffer_size = 0;

    // 清空预约站
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        cpu.rs_alu[i].busy = false;
    }

    // 清空LSQ
    for (uint32_t i = 0; i < LSQ_SIZE; ++i) {
        cpu.lsq[i].busy = false;
    }

    // 清空ROB
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        if (cpu.rob[i].busy && !cpu.rob[i].ready) {
            cpu.rob[i].busy = false;
            if (i == cpu.rob_tail - 1 || (cpu.rob_tail == 0 && i == ROB_SIZE - 1)) {
                cpu.rob_tail = i;
                cpu.rob_size--;
            }
        }
    }

    // 清空RAT
    for (int i = 0; i < 32; ++i) {
        cpu.rat[i].busy = false;
        cpu.rat[i].rob_idx = 0;
    }

    cpu.pipeline_flushed = true;
}
