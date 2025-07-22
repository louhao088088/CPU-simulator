#include <bitset>
#include <cstdint>
#include <iostream>
#include <string>

const int MEMORY_SIZE = 1024 * 1024;
const uint32_t HALT_INSTRUCTION = 0x0ff00513;

using std::cout;

struct CPU_State {
    uint32_t pc;
    uint32_t regs[32];
    uint8_t memory[MEMORY_SIZE];

    CPU_State() {
        pc = 0;
        for (int i = 0; i < 32; ++i) {
            regs[i] = 0;
        }
        for (int i = 0; i < MEMORY_SIZE; ++i) {
            memory[i] = 0;
        }
    }
};
int cnt = 0;
class RISCV_Simulator {
  private:
    CPU_State cpu;
    bool is_halted = false;

  public:
    RISCV_Simulator() = default;

    // load the instruction
    void load_program() {
        std::string line;
        unsigned int current_address = 0;

        char at_symbol;
        while (std::cin >> at_symbol) {
            if (at_symbol == '@') {
                std::cin >> std::hex >> current_address;
            } else {
                std::cin.putback(at_symbol);
                unsigned int byte_value;
                std::cin >> std::hex >> byte_value;
                if (current_address < MEMORY_SIZE) {
                    cpu.memory[current_address++] = static_cast<uint8_t>(byte_value);
                }
            }
        }
    }

    void run() {
        while (!is_halted) {
            tick();
        }
        print_result();
    }

  private:
    void tick() {

        uint32_t instruction = fetch_instruction();

        if (instruction == HALT_INSTRUCTION) {
            is_halted = true;
            return;
        }

        // DECODE & EXECUTE
        uint32_t next_pc = cpu.pc + 4;
        cout << instruction << " ";
        //    << " ";
        cout << std::hex << cpu.pc << " " << std::endl;
        cnt++;
        // if (cnt > 10)
        //    exit(0);
        decode_and_execute(instruction, next_pc);

        cpu.pc = next_pc;

        cpu.regs[0] = 0;
    }

    uint32_t fetch_instruction() {

        if (cpu.pc >= MEMORY_SIZE - 3) {
            std::cout << "Error: Program Counter out of bounds!" << std::endl;
            is_halted = true;
            return 0;
        }

        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 0]);
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 1]) << 8;
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 2]) << 16;
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 3]) << 24;
        return inst;
    }

    void decode_and_execute(uint32_t instruction, uint32_t &next_pc) {

        uint32_t opcode = instruction & 0x7F;

        std::cout << std::bitset<7>(opcode) << "\n";
        switch (opcode) {

        case 0b0110111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t imm = (instruction >> 12) & 0xFFFFF;
            if (rd != 0)
                cpu.regs[rd] = (imm << 12);

            std::cout << "lui"
                      << " " << rd << " " << imm << "\n";

            break;
        } // U-TYPE lui

        case 0b0010111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t imm = (instruction >> 12) & 0xFFFFF;
            if (rd != 0)
                cpu.regs[rd] = (imm << 12) + cpu.pc;

            std::cout << "auipc"
                      << " " << rd << " " << imm;

            break;
        } // U-TYPE auipc

        case 0b1101111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t imm_20 = (instruction >> 31) & 1;
            uint32_t imm_10_1 = (instruction >> 21) & 0x3FF;
            uint32_t imm_11 = (instruction >> 20) & 1;
            uint32_t imm_19_12 = (instruction >> 12) & 0xFF;

            uint32_t imm = ((imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1));
            int32_t offset;
            if (imm_20 == 1) {
                offset = static_cast<int32_t>(imm | 0xFFF00000);
            } else {
                offset = static_cast<int32_t>(imm);
            }
            if (rd != 0) {
                cpu.regs[rd] = cpu.pc + 4;
            }
            next_pc = cpu.pc + offset;

            cout << "jal"
                 << " " << offset << " " << next_pc << " " << rd << "\n";

            break;
        } // J-TYPE

        case 0b1100111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            int32_t imm = static_cast<int32_t>(instruction) >> 20;

            if (funct3 == 0b000) {
                next_pc = (cpu.regs[rs1] + imm) & (~1);
                if (rd != 0) {
                    cpu.regs[rd] = cpu.pc + 4;
                }
            } else {
                std::cout << "Error: Unknown J-type instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }

            cout << "jalr"
                 << " " << rd << " " << rs1 << " " << imm << "\n";
            break;
        } // I-TYPE JALR

        case 0b1100011: {
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            uint32_t rs2 = (instruction >> 20) & 0x1F;
            uint32_t imm = (((instruction >> 25) & 0x3F) << 5) | (((instruction >> 8) & 0xF) << 1) |
                           (((instruction >> 7) & 0x1) << 11) | (((instruction >> 31) & 0x1) << 12);
            int32_t offset;
            if (imm & 0x1000) {
                offset = static_cast<int32_t>(imm | 0xFFFFF000);

            } else {
                offset = static_cast<int32_t>(imm);
            }

            if (funct3 == 0b000) {
                if (cpu.regs[rs1] == cpu.regs[rs2]) {
                    next_pc = cpu.pc + 4 + offset;
                }
                cout << "beq ";
            } // Branch if Equal
            else if (funct3 == 0b001) {
                if (cpu.regs[rs1] != cpu.regs[rs2]) {
                    next_pc = cpu.pc + 4 + offset;
                }
                cout << "bne ";
            } // Branch if Not Equal
            else if (funct3 == 0b100) {
                if (static_cast<int32_t>(cpu.regs[rs1]) < static_cast<int32_t>(cpu.regs[rs2])) {
                    next_pc = cpu.pc + 4 + offset;
                }
                cout << "blt ";
            } // Branch if Less Than
            else if (funct3 == 0b101) {
                if (static_cast<int32_t>(cpu.regs[rs1]) >= static_cast<int32_t>(cpu.regs[rs2])) {
                    next_pc = cpu.pc + 4 + offset;
                }
                cout << "bltu ";
            } // Branch if Greater Than or Equal
            else if (funct3 == 0b110) {
                if (cpu.regs[rs1] < cpu.regs[rs2]) {
                    next_pc = cpu.pc + 4 + offset;
                }
                cout << "bge ";

            } // Branch if Less Than, Unsigned
            else if (funct3 == 0b111) {
                if (cpu.regs[rs1] >= cpu.regs[rs2]) {
                    next_pc = cpu.pc + 4 + offset;
                }
                cout << "bgeu ";
            } // Branch if Greater Than or Equal, Unsigned
            else {
                std::cout << "Error: Unknown branch instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }
            cout << " " << funct3 << " " << rs1 << " " << rs2 << " " << offset << "\n";
            break;
        } // B-TYPE

        case 0b0000011: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            int32_t offset = static_cast<int32_t>(instruction >> 20);

            if (funct3 == 0b000) {
                if (cpu.regs[rs1] + offset >= MEMORY_SIZE) {
                    std::cout << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = static_cast<int8_t>(cpu.memory[cpu.regs[rs1] + offset]);

                cout << "lb ";

            } // Load Byte
            else if (funct3 == 0b001) {
                if (cpu.regs[rs1] + offset + 1 >= MEMORY_SIZE) {
                    std::cout << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset + 1 << std::endl;
                    is_halted = true;
                    return;
                }
                if (cpu.regs[rs1] + offset % 2 == 1) {
                    std::cout << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = static_cast<int16_t>(
                        static_cast<uint16_t>(cpu.memory[cpu.regs[rs1] + offset]) |
                        (static_cast<uint16_t>(cpu.memory[cpu.regs[rs1] + offset + 1]) << 8));

                cout << "lh ";
            } // Load Halfword
            else if (funct3 == 0b010) {
                if (cpu.regs[rs1] + offset + 3 >= MEMORY_SIZE) {
                    std::cout << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset + 3 << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = cpu.memory[cpu.regs[rs1] + offset] |
                                   (cpu.memory[cpu.regs[rs1] + offset + 1] << 8) |
                                   (cpu.memory[cpu.regs[rs1] + offset + 2] << 16) |
                                   (cpu.memory[cpu.regs[rs1] + offset + 3] << 24);

                cout << "lw ";
            } // Load Word
            else if (funct3 == 0b100) {
                if (cpu.regs[rs1] + offset >= MEMORY_SIZE) {
                    std::cout << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset << std::endl;
                    is_halted = true;
                    return;
                }
                if (cpu.regs[rs1] + offset % 4 != 0) {
                    std::cout << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = cpu.memory[cpu.regs[rs1] + offset];

                cout << "lbu ";
            } // Load Byte, Unsigned
            else if (funct3 == 0b101) {
                if (cpu.regs[rs1] + offset + 1 >= MEMORY_SIZE) {
                    std::cout << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset + 1 << std::endl;
                    is_halted = true;
                    return;
                }
                if (cpu.regs[rs1] + offset % 2 == 1) {
                    std::cout << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << cpu.regs[rs1] + offset << std::endl;
                    is_halted = true;
                    return;
                }
                if (rd != 0)
                    cpu.regs[rd] = cpu.memory[cpu.regs[rs1] + offset] |
                                   (cpu.memory[cpu.regs[rs1] + offset + 1] << 8);

                cout << "lhu ";

            } // Load Halfword, Unsigned
            else {
                std::cout << "Error: Unknown load instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }

            cout << " " << rd << " " << rs1 << " " << offset << "\n";
            break;
        } // I-TYPE LOAD

        case 0b0100011: {
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            uint32_t rs2 = (instruction >> 20) & 0x1F;
            int32_t offset =
                static_cast<int32_t>(((instruction >> 25) << 5) | ((instruction >> 7) & 0x1F));

            if (funct3 == 0b000) {
                cpu.memory[cpu.regs[rs1] + offset] = static_cast<uint8_t>(cpu.regs[rs2]);

                cout << "sb ";
            } // Store Byte
            else if (funct3 == 0b001) {
                cpu.memory[cpu.regs[rs1] + offset] = static_cast<uint8_t>(cpu.regs[rs2]);
                cpu.memory[cpu.regs[rs1] + offset + 1] = static_cast<uint8_t>(cpu.regs[rs2] >> 8);
                cout << "sh ";
            } // Store Halfword
            else if (funct3 == 0b010) {
                cpu.memory[cpu.regs[rs1] + offset] = static_cast<uint8_t>(cpu.regs[rs2]);
                cpu.memory[cpu.regs[rs1] + offset + 1] = static_cast<uint8_t>(cpu.regs[rs2] >> 8);
                cpu.memory[cpu.regs[rs1] + offset + 2] = static_cast<uint8_t>(cpu.regs[rs2] >> 16);
                cpu.memory[cpu.regs[rs1] + offset + 3] = static_cast<uint8_t>(cpu.regs[rs2] >> 24);
                cout << "sw ";
            } // Store Word
            else {
                std::cout << "Error: Unknown store instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }
            cout << " " << rs1 << " " << rs2 << " " << offset << "\n";
            break;
        } // S-TYPE STORE
        case 0b0010011: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            int32_t imm = static_cast<int32_t>(instruction >> 20);

            if (funct3 == 0b000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] + imm;
                cout << "addi ";
            } // Add Immediate
            else if (funct3 == 0b010) {
                if (rd != 0)
                    cpu.regs[rd] = (static_cast<int32_t>(cpu.regs[rs1]) < (imm)) ? 1 : 0;
                cout << "slti ";
            } // Set if Less Than Immediate
            else if (funct3 == 0b011) {
                if (rd != 0)
                    cpu.regs[rd] = (cpu.regs[rs1] < static_cast<uint32_t>(imm)) ? 1 : 0;
                cout << "sltiu ";
            } // Set if Less Than Immediate, Unsigned
            else if (funct3 == 0b100) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] ^ imm;
                cout << "xori ";
            } // XORI
            else if (funct3 == 0b110) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] | imm;
                cout << "ori ";
            } // ORI
            else if (funct3 == 0b111) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] & imm;
                cout << "andi ";
            } // ANDI
            else if (funct3 == 0b001) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] << (imm & 0x1F);
                cout << "slli ";
            } // Shift Left Logical Immediate
            else if (funct3 == 0b101 && (instruction >> 30) == 0b000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] >> (imm & 0x1F);
                cout << "srli ";
            } // Shift Right Logical Immediate
            else if (funct3 == 0b101 && (instruction >> 30) == 0b0100000) {
                if (rd != 0)
                    cpu.regs[rd] =
                        static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) >> (imm & 0x1F));
                cout << "srai ";
            } // Shift Right Arithmetic Immediate
            else {
                std::cout << "Error: Unknown I-type instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }

            cout << " " << rd << " " << rs1 << " " << imm << "\n";
            break;
        } // I-TYPE Immediate

        case 0b0110011: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            uint32_t rs2 = (instruction >> 20) & 0x1F;
            uint32_t funct7 = (instruction >> 25) & 0x7F;

            if (funct3 == 0 && funct7 == 0) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] + cpu.regs[rs2];
                cout << "add ";
            } // add
            else if (funct3 == 0 && funct7 == 0b0100000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] - cpu.regs[rs2];
                cout << "sub ";
            } // sub
            else if (funct3 == 0b001 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] << (cpu.regs[rs2] & 0x1F);
                cout << "sll ";

            } // Shift Left Logical
            else if (funct3 == 0b010 && funct7 == 0b0000000) {
                if (static_cast<int32_t>(cpu.regs[rs1]) < static_cast<int32_t>(cpu.regs[rs2])) {
                    if (rd != 0)
                        cpu.regs[rd] = 1;
                } else {
                    if (rd != 0)
                        cpu.regs[rd] = 0;
                }
                cout << "slt ";

            } // Set if Less Than
            else if (funct3 == 0b011 && funct7 == 0b0000000) {
                if (cpu.regs[rs1] < cpu.regs[rs2]) {
                    if (rd != 0)
                        if (rd != 0)
                            cpu.regs[rd] = 1;
                } else {
                    if (rd != 0)
                        cpu.regs[rd] = 0;
                }
                cout << "sltu ";
            } // Set if Less Than, Unsigned
            else if (funct3 == 0b100 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] ^ cpu.regs[rs2];
                cout << "xor ";
            } // XOR
            else if (funct3 == 0b101 && funct7 == 0b0100000) {
                if (rd != 0)
                    cpu.regs[rd] = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) >>
                                                         (cpu.regs[rs2] & 0x1F));

                cout << "sra ";
            } // Shift Right Arithmetic
            else if (funct3 == 0b101 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] >> (cpu.regs[rs2] & 0x1F);

                cout << "srl ";

            } // Shift Right Logical
            else if (funct3 == 0b110 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] | cpu.regs[rs2];
                cout << "or ";
            } // OR
            else if (funct3 == 0b111 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] & cpu.regs[rs2];
                cout << "and ";
            } // AND
            else {
                std::cout << "Error: Unknown R-type instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }
            cout << " " << rd << " " << rs1 << " " << rs2 << " " << funct3 << " " << funct7 << "\n";

            break;

        } // R-TYPE
        default: {
            std::cout << "Error: Unknown instruction " << std::hex << instruction << " at pc "
                      << cpu.pc << std::endl;
            is_halted = true;
            break;
        }
        }
    }

    void print_result() {
        uint32_t result = cpu.regs[10] & 0xFF;
        std::cout << result << std::endl;
    }
};

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    RISCV_Simulator simulator;

    simulator.load_program();

    simulator.run();

    return 0;
}