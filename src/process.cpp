#include "../include/process.h"

#include "../include/cpu_state.h"

#include <iostream>
#include <ostream>
int CNT = 0;

CPU::CPU() : cycle_count_(0), instruction_count_(0), branch_mispredictions_(0) {}

void CPU::tick(CPU_State &cpu) {
    CPU_Core next_state = cpu.core;

    commit_stage(cpu.core, next_state, cpu.memory);

    writeback_stage(cpu.core, next_state);
    execute_stage(cpu.core, next_state, cpu.memory);

    dispatch_stage(cpu.core, next_state);

    decode_rename_stage(cpu.core, next_state, cpu.memory);

    fetch_stage(cpu.core, next_state, cpu.memory);

    cpu.core = next_state;

    ++cycle_count_;
    // cout << "CYCLE:" << cycle_count_ << "\n";
}

void CPU::fetch_stage(const CPU_Core &now_state, CPU_Core &next_state, const uint8_t memory[]) {
    int pc = now_state.pc;
    if (now_state.clear_flag) {
        flush_pipeline(next_state);
        next_state.clear_flag = 0;
        next_state.next_pc = 0;
        next_state.pc = now_state.next_pc;
        pc = now_state.next_pc;
    }
    if (now_state.fetch_stalled) {
        return;
    }

    if (now_state.fetch_buffer_size >= FETCH_BUFFER_SIZE - 1) {
        return;
    }

    if (pc < MEMORY_SIZE - 3) {

        uint32_t instruction =
            memory[pc] | (memory[pc + 1] << 8) | (memory[pc + 2] << 16) | (memory[pc + 3] << 24);

        int tail = now_state.fetch_buffer_tail;
        if (now_state.clear_flag) {
            tail = 0;
            next_state.fetch_buffer_size = 0;
        }

        FetchBufferEntry &entry = next_state.fetch_buffer[tail];
        entry.valid = true;
        entry.instruction = instruction;
        entry.pc = pc;
        next_state.fetch_buffer_tail = (tail + 1) % FETCH_BUFFER_SIZE;
        next_state.fetch_buffer_size++;

        next_state.pc = pc + 4;
    } else {

        next_state.fetch_stalled = true;
    }
}

void CPU::decode_rename_stage(const CPU_Core &now_state, CPU_Core &next_state,
                              const uint8_t memory[]) {

    if (now_state.clear_flag) {
        return;
    } else {
        next_state.rob_size = now_state.rob_size - now_state.commit_flag;
    }
    if (now_state.fetch_buffer_size == 0) {
        return;
    }
    // cout << "DECODE:" << rob_full(now_state) << " " << now_state.fetch_buffer_size << "\n";
    if (rob_full(now_state)) {
        return;
    }
    FetchBufferEntry &fetch_entry = next_state.fetch_buffer[now_state.fetch_buffer_head];
    if (!fetch_entry.valid) {
        return;
    }

    Instruction instr = InstructionProcessor::decode(fetch_entry.instruction, fetch_entry.pc);

    // cout << "Decode"
    //      << " " << std::hex << " " << fetch_entry.pc << " " << std::dec <<
    //      Type_string(instr.type)
    //      << std::endl;

    if (InstructionProcessor::is_alu_type(instr.type) ||
        InstructionProcessor::is_branch_type(instr.type)) {
        if (!rs_available(now_state, instr.type)) {
            return;
        }
    } else if (InstructionProcessor::is_load_type(instr.type) ||
               InstructionProcessor::is_store_type(instr.type)) {
        if (!LSB_available(now_state)) {
            return;
        }
    }

    uint32_t rob_idx = now_state.rob_tail;
    if (now_state.clear_flag)
        rob_idx = 0;
    next_state.rob_tail = (rob_idx + 1) % ROB_SIZE;
    next_state.rob_size++;

    ROBEntry &rob_entry = next_state.rob[rob_idx];
    rob_entry.busy = true;
    rob_entry.instr_type = instr.type;
    rob_entry.state = InstrState::Dispatch;

    rob_entry.dest_reg = instr.rd;
    if (InstructionProcessor::is_branch_type(instr.type))
        rob_entry.dest_reg = 0;

    rob_entry.pc = instr.pc;
    rob_entry.rs1 = instr.rs1;
    rob_entry.rs2 = instr.rs2;
    rob_entry.imm = instr.imm;
    //   cout << "Decode" << rob_entry.rs1 << " " << rob_entry.rs2 << " " << rob_entry.imm << "\n ";
    if (InstructionProcessor::is_branch_type(instr.type)) {
        rob_entry.is_branch = true;
        rob_entry.predicted_taken = predict_branch_taken(now_state);
        rob_entry.target_pc = instr.pc + instr.imm;
    }

    fetch_entry.valid = false;
    next_state.fetch_buffer_head = (now_state.fetch_buffer_head + 1) % FETCH_BUFFER_SIZE;
    next_state.fetch_buffer_size--;

    ++instruction_count_;
}

void CPU::dispatch_stage(const CPU_Core &now_state, CPU_Core &next_state) {
    if (now_state.clear_flag) {
        return;
    }

    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        const ROBEntry rob_entry_now = now_state.rob[i];

        if (!rob_entry_now.busy || rob_entry_now.state != InstrState::Dispatch) {
            continue;
        }
        if (InstructionProcessor::is_alu_type(rob_entry_now.instr_type) ||
            InstructionProcessor::is_branch_type(rob_entry_now.instr_type)) {

            if (!rs_available(now_state, rob_entry_now.instr_type)) {
                continue;
            }

            uint32_t rs_idx = allocate_rs_entry(now_state, rob_entry_now.instr_type);
            RSEntry &rs_entry = next_state.rs_alu[rs_idx];
            rs_entry.busy = true;
            rs_entry.op = rob_entry_now.instr_type;
            rs_entry.dest_rob_idx = i;
            rs_entry.imm = rob_entry_now.imm;
            bool ready;
            rs_entry.Vj = read_operand(now_state, now_state.rob[i].rs1, rs_entry.Qj, ready);

            bool needs_rs2 = false;
            if (InstructionProcessor::is_alu_type(rob_entry_now.instr_type)) {

                needs_rs2 = (rob_entry_now.instr_type >= InstrType::ALU_ADD &&
                             rob_entry_now.instr_type <= InstrType::ALU_SLTU);

            } else if (InstructionProcessor::is_branch_type(rob_entry_now.instr_type)) {
                needs_rs2 = true;
            }

            if (needs_rs2) {
                bool ready;
                rs_entry.Vk = read_operand(now_state, now_state.rob[i].rs2, rs_entry.Qk, ready);
            } else {

                rs_entry.Vk = 0;
                rs_entry.Qk = ROB_SIZE;
            }

            rename_registers(next_state, now_state.rob[i], i);
            next_state.rob[i].state = InstrState::Execute;
        }

        else if (InstructionProcessor::is_load_type(rob_entry_now.instr_type) ||
                 InstructionProcessor::is_store_type(rob_entry_now.instr_type)) {

            if (!LSB_available(now_state)) {
                continue;
            }

            const uint32_t LSB_idx = allocate_LSB_entry(now_state);
            LSBEntry &LSB_entry = next_state.LSB[LSB_idx];

            LSB_entry.busy = true;
            LSB_entry.op = rob_entry_now.instr_type;
            LSB_entry.dest_rob_idx = i;
            LSB_entry.rob_idx = i;
            LSB_entry.offset = rob_entry_now.imm;
            LSB_entry.execute_completed = 0;
            LSB_entry.execution_cycles_left = 0;
            bool ready = 0;
            LSB_entry.base_value =
                read_operand(now_state, now_state.rob[i].rs1, LSB_entry.base_rob_idx, ready);

            LSB_entry.address_ready = ready;

            if (ready) {
                if (now_state.Regs.is_busy(now_state.rob[i].rs1)) {
                    uint32_t rob_idx = now_state.Regs.get_rob_index(now_state.rob[i].rs1);

                    LSB_entry.address = now_state.rob[rob_idx].value + rob_entry_now.imm;
                } else {
                    LSB_entry.address =
                        now_state.Regs.get_value(now_state.rob[i].rs1) + rob_entry_now.imm;
                }

                //    cout << "ADDR:" << Type_string(LSB_entry.op) << " " << LSB_entry.address << "
                //    "
                //           << LSB_entry.base_value << " " << rob_entry_now.imm << " "
                //             << now_state.rob[i].rs1 << std::endl;
            }

            if (InstructionProcessor::is_store_type(rob_entry_now.instr_type)) {
                bool ready;
                LSB_entry.value =
                    read_operand(now_state, now_state.rob[i].rs2, LSB_entry.value_rob_idx, ready);

            } else {
                LSB_entry.value_rob_idx = ROB_SIZE;
            }

            if (InstructionProcessor::is_load_type(rob_entry_now.instr_type)) {
                rename_registers(next_state, now_state.rob[i], i);
            }

            next_state.rob[i].state = InstrState::Execute;
        }

        else if (rob_entry_now.instr_type == InstrType::LUI ||
                 rob_entry_now.instr_type == InstrType::AUIPC ||
                 rob_entry_now.instr_type == InstrType::JUMP_JAL ||
                 rob_entry_now.instr_type == InstrType::JUMP_JALR) {

            if (!rs_available(now_state, rob_entry_now.instr_type)) {
                continue;
            }

            const uint32_t rs_idx = allocate_rs_entry(now_state, rob_entry_now.instr_type);
            RSEntry &rs_entry = next_state.rs_alu[rs_idx];
            rs_entry.busy = true;
            rs_entry.op = rob_entry_now.instr_type;
            rs_entry.dest_rob_idx = i;
            rs_entry.imm = rob_entry_now.imm;

            if (rob_entry_now.instr_type == InstrType::JUMP_JALR) {
                bool ready;
                rs_entry.Vj = read_operand(now_state, rob_entry_now.rs1, rs_entry.Qj, ready);
            } else {
                rs_entry.Vj = 0;
                rs_entry.Qj = ROB_SIZE;
            }

            rs_entry.Vk = 0;
            rs_entry.Qk = ROB_SIZE;

            rename_registers(next_state, rob_entry_now, i);
            next_state.rob[i].state = InstrState::Execute;
        }

        else if (next_state.rob[i].instr_type == InstrType::HALT) {
            next_state.rob[i].state = InstrState::Commit;
        }
    }
}

void CPU::execute_stage(const CPU_Core &now_state, CPU_Core &next_state, const uint8_t memory[]) {
    if (now_state.clear_flag) {
        return;
    }

    uint32_t alu_units_used = 0;
    uint32_t load_units_used = 0;

    for (uint32_t i = 0; i < RS_SIZE && alu_units_used < MAX_ALU_UNITS; ++i) {
        RSEntry &rs_entry = next_state.rs_alu[i];
        const RSEntry rs_entry_now = now_state.rs_alu[i];

        if (!rs_entry_now.busy || !rs_entry_now.operands_ready()) {
            continue;
        }
        //  cout << "EXCUTE"
        //    << " " << Type_string(rs_entry_now.op) << "\n";
        if (rs_entry_now.execution_cycles_left == 0) {
            rs_entry.execution_cycles_left = 1;
            alu_units_used++;
        }

        rs_entry.execution_cycles_left--;

        if (rs_entry_now.execution_cycles_left == 0) {
            uint32_t result = 0;
            ROBEntry &rob_entry = next_state.rob[rs_entry_now.dest_rob_idx];
            const ROBEntry rob_entry_now = now_state.rob[rs_entry_now.dest_rob_idx];

            if (InstructionProcessor::is_alu_type(rs_entry_now.op) ||
                rs_entry_now.op == InstrType::LUI || rs_entry_now.op == InstrType::AUIPC ||
                rs_entry_now.op == InstrType::JUMP_JAL || rs_entry_now.op == InstrType::JUMP_JALR) {

                if (rs_entry_now.op == InstrType::AUIPC || rs_entry_now.op == InstrType::JUMP_JAL ||
                    rs_entry_now.op == InstrType::JUMP_JALR) {

                    if (rs_entry_now.op == InstrType::JUMP_JAL ||
                        rs_entry_now.op == InstrType::JUMP_JALR) {
                        result = rob_entry_now.pc + 4;

                        if (rs_entry_now.op == InstrType::JUMP_JAL) {
                            rob_entry.target_pc = rob_entry_now.pc + rs_entry_now.imm;
                        } else if (rs_entry_now.op == InstrType::JUMP_JALR) {
                            rob_entry.target_pc = (rs_entry_now.Vj + rs_entry_now.imm) & ~1;
                        }
                        rob_entry.is_branch = true;
                    } else if (rs_entry_now.op == InstrType::AUIPC) {
                        result = rob_entry_now.pc + rs_entry_now.imm;
                    }
                } else {
                    result = InstructionProcessor::execute_alu(rs_entry_now.op, rs_entry_now.Vj,
                                                               rs_entry_now.Vk, rs_entry_now.imm);
                    //      cout << "EXCUTE" << result << "\n ";
                }
            } else if (InstructionProcessor::is_branch_type(rs_entry_now.op)) {

                bool taken = InstructionProcessor::check_branch_condition(
                    rs_entry_now.op, rs_entry_now.Vj, rs_entry_now.Vk);
                rob_entry.actual_taken = taken;

                if (taken) {
                    rob_entry.target_pc = rob_entry_now.pc + rob_entry_now.imm;
                } else {
                    rob_entry.target_pc = rob_entry_now.pc + 4;
                }

                result = taken ? 1 : 0;
            }

            rob_entry.value = result;
            rob_entry.state = InstrState::Writeback;

            rs_entry.busy = false;
        }
    }

    for (uint32_t i = 0; i < LSB_SIZE && load_units_used < MAX_LOAD_UNITS; ++i) {
        LSBEntry &LSB_entry = next_state.LSB[i];
        const LSBEntry LSB_entry_now = now_state.LSB[i];
        if (!LSB_entry_now.busy) {
            continue;
        }
        bool address_ready = 0;
        if (!LSB_entry_now.address_ready) {
            if (LSB_entry_now.base_rob_idx == ROB_SIZE) {
                LSB_entry.address = LSB_entry_now.base_value + LSB_entry_now.offset;
                //     cout << "ADDR::" << Type_string(LSB_entry.op) << " " << LSB_entry.address <<
                //     " "
                //           << LSB_entry_now.offset << " " << LSB_entry_now.base_value <<
                //           std::endl;
                LSB_entry.address_ready = true;
                address_ready = true;
            }
        }

        if (!LSB_entry_now.address_ready || address_ready) {
            continue;
        }

        if (LSB_entry_now.execute_completed) {
            continue;
        }

        // cout << "EXCUTELSB::;"
        //       << " " << Type_string(LSB_entry_now.op) << " "
        //       << now_state.rob[LSB_entry_now.rob_idx].pc << " " << LSB_entry_now.rob_idx << "\n";
        if (InstructionProcessor::is_load_type(LSB_entry_now.op)) {
            //     cout << "GGG:" << LSB_entry_now.address_ready << " "
            //       << " " << LSB_entry_now.value_rob_idx << "\n";
            if (LSB_entry_now.execution_cycles_left == 0) {
                uint32_t forwarded_value;
                if (get_load_values(now_state, LSB_entry_now.address, LSB_entry_now.rob_idx,
                                    forwarded_value)) {
                    ROBEntry &rob_entry = next_state.rob[LSB_entry_now.dest_rob_idx];

                    rob_entry.value = forwarded_value;
                    rob_entry.state = InstrState::Writeback;
                    LSB_entry.execute_completed = true;
                    load_units_used++;
                    LSB_entry.busy = false;

                    continue;
                } else if (check_load_dependencies(now_state, LSB_entry_now.address,
                                                   LSB_entry_now.rob_idx, forwarded_value)) {
                    LSB_entry.execution_cycles_left = 3;
                } else
                    continue;
            }
            //      cout << "HHH:" << LSB_entry_now.address_ready << " "
            //          << " " << LSB_entry_now.value_rob_idx << "\n";
            if (LSB_entry_now.execution_cycles_left > 0) {
                LSB_entry.execution_cycles_left--;
                load_units_used++;

                if (LSB_entry_now.execution_cycles_left == 1) {
                    if (LSB_entry_now.address < MEMORY_SIZE) {
                        uint32_t value = 0;
                        switch (LSB_entry_now.op) {
                        case InstrType::LOAD_LB:
                            value = static_cast<int32_t>(
                                static_cast<int8_t>(memory[LSB_entry_now.address]));
                            break;
                        case InstrType::LOAD_LBU:
                            value = memory[LSB_entry_now.address];
                            break;
                        case InstrType::LOAD_LH:
                            value = static_cast<int32_t>(
                                static_cast<int16_t>(*reinterpret_cast<const uint16_t *>(
                                    &memory[LSB_entry_now.address])));
                            break;
                        case InstrType::LOAD_LHU:
                            value =
                                *reinterpret_cast<const uint16_t *>(&memory[LSB_entry_now.address]);
                            break;
                        case InstrType::LOAD_LW:
                            value =
                                *reinterpret_cast<const uint32_t *>(&memory[LSB_entry_now.address]);
                            //      cout << "LW"
                            //           << " " << LSB_entry_now.address << " " << value <<
                            //           std::endl;
                            break;
                        default:
                            value = 0;
                            break;
                        }

                        ROBEntry &rob_entry = next_state.rob[LSB_entry_now.dest_rob_idx];
                        rob_entry.value = value;
                        rob_entry.state = InstrState::Writeback;
                        LSB_entry.execute_completed = true;
                        LSB_entry.busy = false;
                    }
                }
            }
        } else if (InstructionProcessor::is_store_type(LSB_entry_now.op)) {

            bool value_ready = 0;
            if (LSB_entry_now.value_rob_idx != ROB_SIZE) {

                const ROBEntry value_rob = now_state.rob[LSB_entry_now.value_rob_idx];

                if (value_rob.state >= InstrState::Writeback) {

                    LSB_entry.value = value_rob.value;
                    LSB_entry.value_rob_idx = ROB_SIZE;
                    value_ready = 1;
                }
            }

            if (LSB_entry_now.address_ready &&
                (LSB_entry_now.value_rob_idx == ROB_SIZE || value_ready)) {
                load_units_used++;
                ROBEntry &rob_entry = next_state.rob[LSB_entry_now.dest_rob_idx];
                rob_entry.value = 0;
                rob_entry.state = InstrState::Writeback;
                LSB_entry.execute_completed = true;
            }
        }
    }
}

void CPU::writeback_stage(const CPU_Core &now_state, CPU_Core &next_state) {
    if (now_state.clear_flag) {
        return;
    }
    for (uint32_t i = 0; i < ROB_SIZE; ++i) {

        const ROBEntry rob_entry_now = now_state.rob[i];
        if (rob_entry_now.busy && rob_entry_now.state == InstrState::Writeback) {

            broadcast_result(now_state, next_state, i, rob_entry_now.value);
        }
    }
}

void CPU::commit_stage(const CPU_Core &now_state, CPU_Core &next_state, uint8_t memory[]) {
    if (now_state.clear_flag) {
        return;
    }
    next_state.commit_flag = 0;
    if (rob_empty(now_state)) {
        return;
    }

    const ROBEntry rob_entry_now = now_state.rob[now_state.rob_head];

    if (!rob_entry_now.busy || rob_entry_now.state != InstrState::Commit) {
        return;
    }
    //  cout << "Commit:" << Type_string(rob_entry_now.instr_type) << "\n";
    if (rob_entry_now.instr_type == InstrType::HALT) {
        next_state.fetch_stalled = true;
        return;
    }

    if (InstructionProcessor::is_store_type(rob_entry_now.instr_type)) {

        for (uint32_t i = 0; i < LSB_SIZE; ++i) {
            const LSBEntry LSB_entry_now = now_state.LSB[i];
            LSBEntry &LSB_entry = next_state.LSB[i];
            if (LSB_entry_now.busy && LSB_entry_now.rob_idx == now_state.rob_head &&
                LSB_entry_now.execute_completed) {

                if (LSB_entry_now.execution_cycles_left == 0) {

                    LSB_entry.execution_cycles_left = 3;
                }

                LSB_entry.execution_cycles_left--;

                if (LSB_entry_now.execution_cycles_left == 1) {
                    if (LSB_entry_now.address < MEMORY_SIZE &&
                        LSB_entry_now.value_rob_idx == ROB_SIZE) {
                        //     cout << "store" << Type_string(LSB_entry_now.op) << " "
                        //         << LSB_entry_now.address << " " << LSB_entry_now.value <<
                        //         std::endl;
                        switch (LSB_entry_now.op) {
                        case InstrType::STORE_SB:
                            memory[LSB_entry_now.address] =
                                static_cast<uint8_t>(LSB_entry_now.value);
                            break;
                        case InstrType::STORE_SH:
                            *reinterpret_cast<uint16_t *>(&memory[LSB_entry_now.address]) =
                                static_cast<uint16_t>(LSB_entry_now.value);
                            break;
                        case InstrType::STORE_SW:
                            *reinterpret_cast<uint32_t *>(&memory[LSB_entry_now.address]) =
                                LSB_entry_now.value;
                            break;
                        default:
                            break;
                        }
                    }

                    LSB_entry.busy = false;
                    free_rob_entry(next_state);
                }
                return;
            }
        }
    }

    if (rob_entry_now.dest_reg != 0 &&
        !InstructionProcessor::is_branch_type(rob_entry_now.instr_type)) {
        next_state.Regs.set_value(rob_entry_now.dest_reg, rob_entry_now.value);
        //    cout << "COMMIT" << rob_entry_now.dest_reg << " " << rob_entry_now.value << std::endl;

        if (now_state.Regs.check_buzy(rob_entry_now.dest_reg, now_state.rob_head)) {
            next_state.Regs.clear_busy(rob_entry_now.dest_reg);
        }
    }

    if (rob_entry_now.is_branch && InstructionProcessor::is_branch_type(rob_entry_now.instr_type)) {
        if (rob_entry_now.predicted_taken != rob_entry_now.actual_taken) {
            handle_branch_misprediction(next_state, rob_entry_now.target_pc);
            return;
        }

        free_rob_entry(next_state);
        return;
    }

    if (rob_entry_now.is_branch && (rob_entry_now.instr_type == InstrType::JUMP_JAL ||
                                    rob_entry_now.instr_type == InstrType::JUMP_JALR)) {

        handle_branch_misprediction(next_state, rob_entry_now.target_pc);
        next_state.next_pc = rob_entry_now.target_pc;
        return;
    }
    free_rob_entry(next_state);
}

void print(CPU_Core &cpu) {
    CNT++;
    cout << CNT << "\n";
    cpu.Regs.print_status();
}

bool CPU::rob_full(const CPU_Core &cpu) const {
    //   cout << "ROBFULL" << cpu.rob_size << " " << cpu.commit_flag << " "
    //        << "\n";
    return ((cpu.rob_size >= ROB_SIZE - 1) && (!cpu.commit_flag));
}

bool CPU::rob_empty(const CPU_Core &cpu) const { return (cpu.rob_size - cpu.commit_flag) == 0; }

void CPU::free_rob_entry(CPU_Core &cpu) {
    // print(cpu);
    cpu.commit_flag = 1;
    cpu.rob[cpu.rob_head].busy = false;
    cpu.rob_head = (cpu.rob_head + 1) % ROB_SIZE;
}

bool CPU::rs_available(const CPU_Core &cpu, InstrType type) const {
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        if (!cpu.rs_alu[i].busy) {
            return true;
        }
    }
    return false;
}

uint32_t CPU::allocate_rs_entry(const CPU_Core &cpu, InstrType type) {
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        if (!cpu.rs_alu[i].busy) {
            return i;
        }
    }
    return 0;
}

void CPU::free_rs_entry(CPU_Core &cpu, uint32_t rs_idx, InstrType type) {
    cpu.rs_alu[rs_idx].busy = false;
}

bool CPU::LSB_available(const CPU_Core &cpu) const {
    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        if (!cpu.LSB[i].busy) {
            return true;
        }
    }
    return false;
}

uint32_t CPU::allocate_LSB_entry(const CPU_Core &cpu) {
    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        if (!cpu.LSB[i].busy) {
            return i;
        }
    }
    return 0;
}

void CPU::free_LSB_entry(CPU_Core &cpu, uint32_t LSB_idx) { cpu.LSB[LSB_idx].busy = false; }

void CPU::rename_registers(CPU_Core &cpu, const ROBEntry &rob_entry, uint32_t rob_idx) {
    if (rob_entry.dest_reg != 0) {
        cpu.Regs.set_busy(rob_entry.dest_reg, rob_idx);
    }
}

uint32_t CPU::read_operand(const CPU_Core &cpu, uint32_t reg_idx, uint32_t &rob_dependency,
                           bool &ready) {
    if (reg_idx == 0) {
        rob_dependency = ROB_SIZE;
        return 0;
    }

    if (cpu.Regs.is_busy(reg_idx)) {
        uint32_t rob_idx = cpu.Regs.get_rob_index(reg_idx);
        if (cpu.rob[rob_idx].state >= InstrState::Writeback) {
            rob_dependency = ROB_SIZE;
            ready = 1;
            return cpu.rob[rob_idx].value;
        } else {
            rob_dependency = rob_idx;
            return 0;
        }
    } else {
        rob_dependency = ROB_SIZE;
        ready = 1;
        return cpu.Regs.get_value(reg_idx);
    }
}

void CPU::broadcast_result(const CPU_Core &now_state, CPU_Core &next_state, uint32_t rob_idx,
                           uint32_t value) {
    next_state.rob[rob_idx].state = InstrState::Commit;
    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        RSEntry &rs = next_state.rs_alu[i];
        const RSEntry rs_now = now_state.rs_alu[i];
        if (rs_now.busy) {
            if (rs_now.Qj == rob_idx) {
                rs.Vj = value;
                rs.Qj = ROB_SIZE;
            }
            if (rs_now.Qk == rob_idx) {
                rs.Vk = value;
                rs.Qk = ROB_SIZE;
            }
        }
    }

    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        LSBEntry &LSB = next_state.LSB[i];
        const LSBEntry LSB_now = now_state.LSB[i];
        if (LSB.busy && LSB.base_rob_idx == rob_idx) {

            LSB.base_value = value;
            LSB.base_rob_idx = ROB_SIZE;
            LSB.address_ready = true;
            LSB.address = value + LSB_now.offset;
            //  cout << "ADDR:" << Type_string(LSB.op) << " " << LSB.address << " " << value << " "
            //       << LSB_now.offset << std::endl;
        }
        if (LSB.busy && LSB.value_rob_idx == rob_idx) {
            LSB.value = value;
            LSB.value_rob_idx = ROB_SIZE;
        }
    }
}

bool CPU::is_earlier_instruction(const CPU_Core &cpu, uint32_t rob_idx1, uint32_t rob_idx2) {

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

bool CPU::check_load_dependencies(const CPU_Core &cpu, uint32_t load_addr, uint32_t load_rob_idx,
                                  uint32_t &forwarded_value) {
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
            if (LSB.value_rob_idx == ROB_SIZE && LSB.execute_completed) {
                return true;
            } else {
                return false;
            }
        }
    }

    return true;
}

bool CPU::get_load_values(const CPU_Core &cpu, uint32_t load_addr, uint32_t load_rob_idx,
                          uint32_t &forwarded_value) {

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
            if (LSB.value_rob_idx == ROB_SIZE && LSB.execute_completed) {
                forwarded_value = LSB.value;
                return true;
            } else {
                return false;
            }
        }
    }

    return false;
}

bool CPU::predict_branch_taken(const CPU_Core &cpu) { return false; }

void CPU::handle_branch_misprediction(CPU_Core &cpu, uint32_t correct_pc) {
    // print(cpu);
    ++branch_mispredictions_;
    cpu.next_pc = correct_pc;
    flush_pipeline(cpu);
}

void CPU::flush_pipeline(CPU_Core &cpu) {
    // cout << "CLEAR\n";
    for (int i = 0; i < FETCH_BUFFER_SIZE; ++i) {
        cpu.fetch_buffer[i].valid = false;
    }
    cpu.fetch_buffer_head = 0;
    cpu.fetch_buffer_tail = 0;
    cpu.fetch_buffer_size = 0;

    for (uint32_t i = 0; i < RS_SIZE; ++i) {
        cpu.rs_alu[i].busy = false;
    }

    for (uint32_t i = 0; i < LSB_SIZE; ++i) {
        cpu.LSB[i].busy = false;
    }

    for (uint32_t i = 0; i < ROB_SIZE; ++i) {
        cpu.rob[i].busy = false;
    }
    cpu.rob_head = 0;
    cpu.rob_tail = 0;
    cpu.rob_size = 0;

    cpu.clear_flag = 1;

    cpu.Regs.flush();

    cpu.pipeline_flushed = true;
}
