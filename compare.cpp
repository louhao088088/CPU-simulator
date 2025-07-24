#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./compare file1.out file2.out\n";
        return 1;
    }

    std::ifstream file1(argv[1]);
    std::ifstream file2(argv[2]);

    if (!file1.is_open() || !file2.is_open()) {
        std::cerr << "Error: Could not open one of the files.\n";
        return 1;
    }

    std::string line1, line2;
    int line_num = 1;
    while (std::getline(file1, line1) && std::getline(file2, line2)) {
        if (line1 != line2) {
            std::cout << "Difference found at line " << line_num << ":\n";
            std::cout << "File 1: " << line1 << "\n";
            std::cout << "File 2: " << line2 << "\n";
            return 0;
        }
        ++line_num;
    }

    // Check for extra lines
    if ((std::getline(file1, line1) && !file1.eof()) || 
        (std::getline(file2, line2) && !file2.eof())) {
        std::cout << "Files differ in length at line " << line_num << "\n";
    } else {
        std::cout << "Files are identical.\n";
    }

    return 0;
}