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

    cout << "Fetch stage: PC = " << std::hex << cpu.pc << " " << cpu.rob_size << " "
         << cpu.fetch_stalled << std::endl;
    if (rob_full(cpu) || cpu.fetch_stalled) {
        return;
    }

    // 如果取指缓存满了，则等待

    if (cpu.fetch_buffer_size >= FETCH_BUFFER_SIZE - 1) {
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
        if (!LSB_available(cpu)) {
            cout << "AAA";
            return; // LSB满，等待
        }
    }
    cout << "Decoded instruction: " << std::hex << instr.raw << " at PC: " << instr.pc << " "
         << Type_string(instr.type) << std::dec << " " << instr.rs1 << " " << instr.rs2 << " "
         << instr.rd << std::endl;
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
    cout << "Decoded instruction: " << std::hex << " at PC: " << instr.pc << " "
         << Type_string(instr.type) << std::dec << " " << rob_entry.busy << " " << rob_idx
         << std::endl;

    // 处理分支指令
    if (InstructionProcessor::is_branch_type(instr.type)) {
        rob_entry.is_branch = true;
        rob_entry.predicted_taken = predict_branch_taken(cpu);
        rob_entry.target_pc = instr.pc + instr.imm;
    }

    // 消费取指缓存条目
    fetch_entry.valid = false;
    cpu.fetch_buffer_head = (cpu.fetch_buffer_head + 1) % FETCH_BUFFER_SIZE;
    cpu.fetch_buffer_size--;

    ++instruction_count_;
}

void CPUCore::dispatch_stage(CPU_State &cpu) {

    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        ROBEntry &rob_entry = cpu.rob[i];
        if (!rob_entry.busy || rob_entry.state != InstrState::Dispatch) {
            continue;
        }
        if (InstructionProcessor::is_alu_type(rob_entry.instr_type) ||
            InstructionProcessor::is_branch_type(rob_entry.instr_type)) {

            if (!rs_available(cpu, rob_entry.instr_type)) {
                continue;
            }

            uint32_t rs_idx = allocate_rs_entry(cpu, rob_entry.instr_type);
            RSEntry &rs_entry = cpu.rs_alu[rs_idx];
            rs_entry.busy = true;
            rs_entry.op = rob_entry.instr_type;
            rs_entry.dest_rob_idx = i;
            rs_entry.imm = rob_entry.imm;

            rs_entry.Vj = read_operand(cpu, rob_entry.rs1, rs_entry.Qj);

            bool needs_rs2 = false;
            if (InstructionProcessor::is_alu_type(rob_entry.instr_type)) {

                needs_rs2 = (rob_entry.instr_type >= InstrType::ALU_ADD &&
                             rob_entry.instr_type <= InstrType::ALU_SLTU);

            } else if (InstructionProcessor::is_branch_type(rob_entry.instr_type)) {
                needs_rs2 = true;
            }

            if (needs_rs2) {
                rs_entry.Vk = read_operand(cpu, rob_entry.rs2, rs_entry.Qk);
            } else {

                rs_entry.Vk = 0;
                rs_entry.Qk = ROB_SIZE;
            }
            // cout << "RENAME:" << Type_string(rob_entry.instr_type)<<;
            rename_registers(cpu, rob_entry, i);
            rob_entry.state = InstrState::Execute;
        }

        else if (InstructionProcessor::is_load_type(rob_entry.instr_type) ||
                 InstructionProcessor::is_store_type(rob_entry.instr_type)) {

            if (!LSB_available(cpu)) {
                continue;
            }
            cout << "DISPATCH LSB pc"
                 << " " << std::hex << " " << rob_entry.pc << " "
                 << Type_string(rob_entry.instr_type) << std::dec << std::endl;
            uint32_t LSB_idx = allocate_LSB_entry(cpu);
            LSBEntry &LSB_entry = cpu.LSB[LSB_idx];

            LSB_entry.busy = true;
            LSB_entry.op = rob_entry.instr_type;
            LSB_entry.dest_rob_idx = i;
            LSB_entry.rob_idx = i;
            LSB_entry.offset = rob_entry.imm;
            LSB_entry.execute_completed = 0;
            LSB_entry.execution_cycles_left = 0;

            LSB_entry.base_value = read_operand(cpu, rob_entry.rs1, LSB_entry.base_rob_idx);
            LSB_entry.address_ready = (LSB_entry.base_rob_idx == ROB_SIZE);
            if (LSB_entry.address_ready)
                LSB_entry.address = LSB_entry.base_value + LSB_entry.offset;
            // cout << "ADDRESS"
            //       << " " << LSB_entry.base_value << " " << LSB_entry.base_rob_idx << std::endl;

            // Store指令还需要读取要存储的值
            if (InstructionProcessor::is_store_type(rob_entry.instr_type)) {
                LSB_entry.value = read_operand(cpu, rob_entry.rs2, LSB_entry.value_rob_idx);
                LSB_entry.value_ready = (LSB_entry.value_rob_idx == ROB_SIZE);
            } else {
                LSB_entry.value_ready = true;
                LSB_entry.value_rob_idx = ROB_SIZE;
            }

            if (InstructionProcessor::is_load_type(rob_entry.instr_type)) {
                rename_registers(cpu, rob_entry, i);
            }

            rob_entry.state = InstrState::Execute;
        }

        // 特殊指令处理（LUI, AUIPC, JAL, JALR, HALT）
        else if (rob_entry.instr_type == InstrType::LUI ||
                 rob_entry.instr_type == InstrType::AUIPC ||
                 rob_entry.instr_type == InstrType::JUMP_JAL ||
                 rob_entry.instr_type == InstrType::JUMP_JALR) {

            if (!rs_available(cpu, rob_entry.instr_type)) {
                continue;
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
                rs_entry.Qj = ROB_SIZE;
            }

            rs_entry.Vk = 0;
            rs_entry.Qk = ROB_SIZE;

            rename_registers(cpu, rob_entry, i);
            rob_entry.state = InstrState::Execute;
        }

        else if (rob_entry.instr_type == InstrType::HALT) {
            rob_entry.state = InstrState::Commit;
        }
    }
}

void CPUCore::execute_stage(CPU_State &cpu) {

    uint32_t alu_units_used = 0;
    uint32_t load_units_used = 0;

    for (uint32_t i = 0; i < RS_SIZE && alu_units_used < MAX_ALU_UNITS; ++i) {
        RSEntry &rs_entry = cpu.rs_alu[i];
        if (rs_entry.busy)
            cout << "Executing instruction: " << std::hex << cpu.rs_alu[i].Qj << " "
                 << cpu.rs_alu[i].Qk << " " << Type_string(rs_entry.op) << std::dec << std::endl;
        if (!rs_entry.busy || !rs_entry.operands_ready()) {
            continue;
        }

        if (rs_entry.execution_cycles_left == 0) {
            rs_entry.execution_cycles_left =
                InstructionProcessor::get_execution_cycles(rs_entry.op);
            alu_units_used++;
        }

        rs_entry.execution_cycles_left--;

        if (rs_entry.execution_cycles_left == 0) {
            uint32_t result = 0;
            ROBEntry &rob_entry = cpu.rob[rs_entry.dest_rob_idx];

            cout << "Executing instruction: " << std::hex << Type_string(rob_entry.instr_type)
                 << " at PC: " << rob_entry.pc << std::dec << std::endl;

            if (InstructionProcessor::is_alu_type(rs_entry.op) || rs_entry.op == InstrType::LUI ||
                rs_entry.op == InstrType::AUIPC || rs_entry.op == InstrType::JUMP_JAL ||
                rs_entry.op == InstrType::JUMP_JALR) {

                // 对于AUIPC、JAL、JALR，第一个操作数应该是PC
                if (rs_entry.op == InstrType::AUIPC || rs_entry.op == InstrType::JUMP_JAL ||
                    rs_entry.op == InstrType::JUMP_JALR) {

                    if (rs_entry.op == InstrType::JUMP_JAL || rs_entry.op == InstrType::JUMP_JALR) {
                        result = rob_entry.pc + 4; // 返回地址写入rd寄存器

                        // 计算跳转目标地址
                        if (rs_entry.op == InstrType::JUMP_JAL) {
                            rob_entry.target_pc = rob_entry.pc + rs_entry.imm;
                        } else if (rs_entry.op == InstrType::JUMP_JALR) {
                            rob_entry.target_pc = (rs_entry.Vj + rs_entry.imm) & ~1;
                        }
                        rob_entry.is_branch = true;
                    } else if (rs_entry.op == InstrType::AUIPC) {
                        result = rob_entry.pc + rs_entry.imm;
                    }
                } else {
                    result = InstructionProcessor::execute_alu(rs_entry.op, rs_entry.Vj,
                                                               rs_entry.Vk, rs_entry.imm);
                }
            } else if (InstructionProcessor::is_branch_type(rs_entry.op)) {

                bool taken = InstructionProcessor::check_branch_condition(rs_entry.op, rs_entry.Vj,
                                                                          rs_entry.Vk);
                rob_entry.actual_taken = taken;

                if (taken) {
                    rob_entry.target_pc = rob_entry.pc + rob_entry.imm;
                } else {
                    rob_entry.target_pc = rob_entry.pc + 4;
                }

                result = taken ? 1 : 0;
            }

            rob_entry.value = result;
            rob_entry.state = InstrState::Writeback;

            rs_entry.busy = false;
        }
    }

    for (uint32_t i = 0; i < LSB_SIZE && load_units_used < MAX_LOAD_UNITS; ++i) {
        LSBEntry &LSB_entry = cpu.LSB[i];
        if (!LSB_entry.busy) {
            continue;
        }
        /*cout << "Hello Executing LSB instruction: " << std::hex << Type_string(LSB_entry.op)
             << " at address: " << std::dec << LSB_entry.address << std::dec << " "
             << LSB_entry.address_ready << " " << LSB_entry.execute_completed << std::endl;
        */
        if (!LSB_entry.address_ready) {
            if (LSB_entry.base_rob_idx == ROB_SIZE) {
                LSB_entry.address = LSB_entry.base_value + LSB_entry.offset;
                cout << "Address" << LSB_entry.base_value << " " << LSB_entry.offset << std::endl;
                LSB_entry.address_ready = true;
            }
        }

        if (!LSB_entry.address_ready) {
            continue;
        }

        if (LSB_entry.execute_completed) {
            continue;
        }


        // 执行Load指令 - 需要3个周期访问内存
        if (InstructionProcessor::is_load_type(LSB_entry.op)) {

           /* cout << "Executing LSB instruction: " << std::hex << Type_string(LSB_entry.op)
                 << " at address: " << std::dec << LSB_entry.address << " "
                 << LSB_entry.execution_cycles_left << std::endl;*/

            if (LSB_entry.execution_cycles_left == 0) {
                uint32_t forwarded_value;
                if (check_load_dependencies(cpu, LSB_entry.address, LSB_entry.rob_idx,
                                            forwarded_value)) {
                    ROBEntry &rob_entry = cpu.rob[LSB_entry.dest_rob_idx];
                    rob_entry.value = forwarded_value;
                    rob_entry.state = InstrState::Writeback;
                    LSB_entry.execute_completed = true;
                    LSB_entry.busy = false;
                    continue; 
                } else {
                    LSB_entry.execution_cycles_left = 3;
                }
            }

            if (LSB_entry.execution_cycles_left > 0) {
                LSB_entry.execution_cycles_left--;
                load_units_used++;

                if (LSB_entry.execution_cycles_left == 0) {
                    if (LSB_entry.address < MEMORY_SIZE) {
                        uint32_t value = 0;
                        switch (LSB_entry.op) {
                        case InstrType::LOAD_LB:
                            value = static_cast<int32_t>(
                                static_cast<int8_t>(cpu.memory[LSB_entry.address]));
                            break;
                        case InstrType::LOAD_LBU:
                            value = cpu.memory[LSB_entry.address];
                            break;
                        case InstrType::LOAD_LH:
                            value = static_cast<int32_t>(static_cast<int16_t>(
                                *reinterpret_cast<uint16_t *>(&cpu.memory[LSB_entry.address])));
                            break;
                        case InstrType::LOAD_LHU:
                            value = *reinterpret_cast<uint16_t *>(&cpu.memory[LSB_entry.address]);
                            break;
                        case InstrType::LOAD_LW:
                            value = *reinterpret_cast<uint32_t *>(&cpu.memory[LSB_entry.address]);
                            break;
                        default:
                            value = 0;
                            break;
                        }
                        ROBEntry &rob_entry = cpu.rob[LSB_entry.dest_rob_idx];
                        rob_entry.value = value;
                        rob_entry.state = InstrState::Writeback;
                        LSB_entry.execute_completed = true;
                        LSB_entry.busy = false;
                    }
                }
            }
        }
        // 执行Store指令 - 只准备数据，不花费周期
        else if (InstructionProcessor::is_store_type(LSB_entry.op)) {

            // 检查要存储的值是否就绪
            if (!LSB_entry.value_ready && LSB_entry.value_rob_idx != ROB_SIZE) {
                // 检查ROB中的值是否已经准备好
                ROBEntry &value_rob = cpu.rob[LSB_entry.value_rob_idx];
                if (value_rob.state >= InstrState::Writeback) {
                    LSB_entry.value = value_rob.value;
                    LSB_entry.value_ready = true;
                    LSB_entry.value_rob_idx = ROB_SIZE;
                }
            }

            if (LSB_entry.address_ready && LSB_entry.value_ready) {
                load_units_used++;
                ROBEntry &rob_entry = cpu.rob[LSB_entry.dest_rob_idx];
                rob_entry.value = 0;
                rob_entry.state = InstrState::Writeback;
                LSB_entry.execute_completed = true;

                cout << "Store instruction prepared: address=" << std::hex << LSB_entry.address
                     << " value=" << LSB_entry.value << std::dec << std::endl;
            }
        }
    }
}

void CPUCore::writeback_stage(CPU_State &cpu) {
    // 在writeback阶段进行CDB广播
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        ROBEntry &rob_entry = cpu.rob[i];
        if (rob_entry.busy && rob_entry.state == InstrState::Writeback) {

            broadcast_result(cpu, i, rob_entry.value);
            rob_entry.state = InstrState::Commit;

            cout << "Writeback instruction: " << std::hex << Type_string(rob_entry.instr_type)
                 << " at PC: " << rob_entry.pc << " value=" << rob_entry.value << std::dec
                 << std::endl;
        }
    }
}

void CPUCore::commit_stage(CPU_State &cpu) {

    if (rob_empty(cpu)) {
        return;
    }

    ROBEntry &rob_entry = cpu.rob[cpu.rob_head];
    cout << "commit" << cpu.rob_head << " " << rob_entry.busy << std::endl;

    if (!rob_entry.busy || rob_entry.state != InstrState::Commit) {
        return;
    }

    if (rob_entry.instr_type == InstrType::HALT) {
        cpu.fetch_stalled = true;
        return;
    }

    if (InstructionProcessor::is_store_type(rob_entry.instr_type)) {
        // 找到对应的LSB条目
        for (uint32_t i = 0; i < LSB_SIZE; ++i) {
            LSBEntry &LSB_entry = cpu.LSB[i];
            if (LSB_entry.busy && LSB_entry.rob_idx == cpu.rob_head &&
                LSB_entry.execute_completed) { // 确保execute阶段已完成

                // Store内存写入需要3个周期
                if (LSB_entry.execution_cycles_left == 0) {
                    // 开始内存写入
                    LSB_entry.execution_cycles_left = 3;
                }

                LSB_entry.execution_cycles_left--;

                // 内存写入完成
                if (LSB_entry.execution_cycles_left == 0) {
                    // 真正写入内存
                    if (LSB_entry.address < MEMORY_SIZE && LSB_entry.value_ready) {
                        switch (LSB_entry.op) {
                        case InstrType::STORE_SB:
                            cpu.memory[LSB_entry.address] = static_cast<uint8_t>(LSB_entry.value);
                            break;
                        case InstrType::STORE_SH:
                            *reinterpret_cast<uint16_t *>(&cpu.memory[LSB_entry.address]) =
                                static_cast<uint16_t>(LSB_entry.value);
                            break;
                        case InstrType::STORE_SW:
                            *reinterpret_cast<uint32_t *>(&cpu.memory[LSB_entry.address]) =
                                LSB_entry.value;
                            break;
                        default:
                            break;
                        }

                        cout << "Store instruction committed: address=" << std::hex
                             << LSB_entry.address << " value=" << LSB_entry.value << std::dec
                             << std::endl;
                    }
                    cout << "Store instruction committed: address=" << std::hex << LSB_entry.address
                         << " value=" << LSB_entry.value << std::dec << std::endl;
                    // 现在才释放LSB条目和ROB条目
                    LSB_entry.busy = false;
                    free_rob_entry(cpu);
                }
                return; // Store指令还在commit中，不能继续
            }
        }
    }

    cout << "Committing instruction: " << std::hex << Type_string(rob_entry.instr_type)
         << " at PC: " << rob_entry.pc << std::dec << std::endl;

    if (rob_entry.dest_reg != 0 && !InstructionProcessor::is_branch_type(rob_entry.instr_type)) {
        cpu.arf.regs[rob_entry.dest_reg] = rob_entry.value;

        if (cpu.rat[rob_entry.dest_reg].busy &&
            cpu.rat[rob_entry.dest_reg].rob_idx == cpu.rob_head) {
            cpu.rat[rob_entry.dest_reg].busy = false;
        }
    }

    // 处理分支指令的分支预测检查和PC更新
    if (rob_entry.is_branch && InstructionProcessor::is_branch_type(rob_entry.instr_type)) {
        // 检查分支预测是否正确
        if (rob_entry.predicted_taken != rob_entry.actual_taken) {
            handle_branch_misprediction(cpu, rob_entry.target_pc);
            return;
        }

        free_rob_entry(cpu);
        return;
    }

    // 处理跳转指令的PC更新
    if (rob_entry.is_branch && (rob_entry.instr_type == InstrType::JUMP_JAL ||
                                rob_entry.instr_type == InstrType::JUMP_JALR)) {

        handle_branch_misprediction(cpu, rob_entry.target_pc);
        cpu.pc = rob_entry.target_pc;
        return;
    }

    // 释放ROB条目
    free_rob_entry(cpu);
}

// ROB管理函数
bool CPUCore::rob_full(const CPU_State &cpu) const { return cpu.rob_size >= ROB_SIZE - 1; }

bool CPUCore::rob_empty(const CPU_State &cpu) const { return cpu.rob_size == 0; }

uint32_t CPUCore::allocate_rob_entry(CPU_State &cpu) {
    uint32_t rob_idx = cpu.rob_tail;
    cpu.rob_tail = (cpu.rob_tail + 1) % ROB_SIZE;
    cpu.rob_size++;
    return rob_idx;
}

void CPUCore::free_rob_entry(CPU_State &cpu) {
    cout << "free_rob_entry: "
         << " " << cpu.rob_head << "\n";
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
    return 0;
}

void CPUCore::free_rs_entry(CPU_State &cpu, uint32_t rs_idx, InstrType type) {
    cpu.rs_alu[rs_idx].busy = false;
}

// LSB管理
bool CPUCore::LSB_available(const CPU_State &cpu) const {
    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        if (!cpu.LSB[i].busy) {
            return true;
        }
    }
    return false;
}

uint32_t CPUCore::allocate_LSB_entry(CPU_State &cpu) {
    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        if (!cpu.LSB[i].busy) {
            return i;
        }
    }
    return 0;
}

void CPUCore::free_LSB_entry(CPU_State &cpu, uint32_t LSB_idx) { cpu.LSB[LSB_idx].busy = false; }

// 寄存器重命名
void CPUCore::rename_registers(CPU_State &cpu, const ROBEntry &rob_entry, uint32_t rob_idx) {
    // 更新目标寄存器的RAT条目
    if (rob_entry.dest_reg != 0) { // x0寄存器不重命名
        cpu.rat[rob_entry.dest_reg].busy = true;
        cpu.rat[rob_entry.dest_reg].rob_idx = rob_idx;
    }
}

uint32_t CPUCore::read_operand(const CPU_State &cpu, uint32_t reg_idx, uint32_t &rob_dependency) {
    if (reg_idx == 0) {
        rob_dependency = ROB_SIZE;
        return 0; // x0寄存器总是0
    }
    cout << "Reading operand from reg: " << reg_idx << " " << cpu.rat[reg_idx].busy << std::endl;
    if (cpu.rat[reg_idx].busy) {
        // 寄存器被重命名，检查ROB是否有结果
        uint32_t rob_idx = cpu.rat[reg_idx].rob_idx;
        if (cpu.rob[rob_idx].state >= InstrState::Writeback) {
            rob_dependency = ROB_SIZE;
            // cout << rob_idx << " " << cpu.rob[rob_idx].value << std::endl;
            return cpu.rob[rob_idx].value;
        } else {
            rob_dependency = rob_idx;
            return 0;
        }
    } else {
        // 从架构寄存器文件读取
        rob_dependency = ROB_SIZE;
        return cpu.arf.regs[reg_idx];
    }
}

// CDB广播
void CPUCore::broadcast_result(CPU_State &cpu, uint32_t rob_idx, uint32_t value) {

    cout << "Broadcasting result: at pc" << std::hex << cpu.rob[rob_idx].pc << " value=" << value
         << " to ROB index: " << rob_idx << std::dec << std::endl;
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        RSEntry &rs = cpu.rs_alu[i];
        if (rs.busy) {
            if (rs.Qj == rob_idx) {
                rs.Vj = value;
                rs.Qj = ROB_SIZE;
            }
            if (rs.Qk == rob_idx) {
                rs.Vk = value;
                rs.Qk = ROB_SIZE;
            }
        }
    }

    // 更新LSB中等待这个结果的条目
    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        LSBEntry &LSB = cpu.LSB[i];
        if (LSB.busy && LSB.base_rob_idx == rob_idx) {
            LSB.base_value = value;
            LSB.base_rob_idx = ROB_SIZE;
            LSB.address_ready = true;
        }
        if (LSB.busy && LSB.value_rob_idx == rob_idx) {
            LSB.value = value;
            LSB.value_rob_idx = ROB_SIZE;
            LSB.value_ready = true;
        }
    }
}

bool CPUCore::is_earlier_instruction(const CPU_State &cpu, uint32_t rob_idx1, uint32_t rob_idx2) {

    if (cpu.rob_size <= 1) {
        return rob_idx1 < rob_idx2;
    }

    uint32_t pos1, pos2;

    if (rob_idx1 >= cpu.rob_head) {
        pos1 = rob_idx1 - cpu.rob_head;
    } else {
        pos1 = (ROB_SIZE - cpu.rob_head) + rob_idx1;
    }

    if (rob_idx2 >= cpu.rob_head) {
        pos2 = rob_idx2 - cpu.rob_head;
    } else {
        pos2 = (ROB_SIZE - cpu.rob_head) + rob_idx2;
    }

    return pos1 < pos2;
}

bool CPUCore::check_load_dependencies(const CPU_State &cpu, uint32_t load_addr,
                                      uint32_t load_rob_idx, uint32_t &forwarded_value) {

    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        const LSBEntry &LSB = cpu.LSB[i];

        if (!LSB.busy || !InstructionProcessor::is_store_type(LSB.op)) {
            continue;
        }

        if (!is_earlier_instruction(cpu, LSB.rob_idx, load_rob_idx)) {
            continue;
        }

        if (!LSB.address_ready) {
            return false;
        }

        if (LSB.address == load_addr) {
            if (LSB.value_ready && LSB.execute_completed) {
                forwarded_value = LSB.value;
                return true;
            } else {
                return false;
            }
        }
    }

    return false;
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

    // 清空LSB
    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        cpu.LSB[i].busy = false;
    }

    // 清空ROB
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        cpu.rob[i].busy = false;
    }
    cpu.rob_head = 0;
    cpu.rob_tail = 0;
    cpu.rob_size = 0;

    // 清空RAT
    for (int i = 0; i < 32; ++i) {

        cpu.rat[i].busy = false;
        cpu.rat[i].rob_idx = ROB_SIZE;
    }

    cpu.pipeline_flushed = true;
}
