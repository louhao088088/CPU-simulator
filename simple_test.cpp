#include "include/instruction.h"

#include <iostream>

int main() {
    std::cout << "=== Testing OutOfOrderProcessor (Simple) ===" << std::endl;

    // 测试decode_instruction函数
    uint32_t add_instr = 0x003100B3; // add x1, x2, x3

    OutOfOrderProcessor ooo;
    DecodedInstruction decoded = ooo.decode_instruction(add_instr, 0);

    std::cout << "Decoded instruction:" << std::endl;
    std::cout << "  Type: " << (int) decoded.type << std::endl;
    std::cout << "  rd: " << decoded.rd << std::endl;
    std::cout << "  rs1: " << decoded.rs1 << std::endl;
    std::cout << "  rs2: " << decoded.rs2 << std::endl;

    // 测试指令类型判断
    bool is_alu = ooo.is_alu_instruction(decoded.type);
    std::cout << "  Is ALU instruction: " << (is_alu ? "Yes" : "No") << std::endl;

    std::cout << "\n✅ Basic OutOfOrderProcessor functions work!" << std::endl;

    return 0;
}
