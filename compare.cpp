#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// 去除一行中的所有空白字符（空格、制表符、换行等）
std::string stripInvisible(const std::string &s) {
    std::string result;
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            result += c;
        }
    }
    return result;
}

void printContext(const std::vector<std::string> &lines1, const std::vector<std::string> &lines2,
                  size_t index) {
    size_t start = (index >= 5) ? index - 5 : 0;
    size_t end = std::min(index + 5, std::min(lines1.size() - 1, lines2.size() - 1));

    std::cout << "Difference found at line " << index + 1
              << " (ignoring invisible characters):\n\n";
    for (size_t i = start; i <= end; ++i) {
        std::cout << "Line " << i + 1 << ":\n";
        std::cout << "  File1: " << (i < lines1.size() ? lines1[i] : "(no line)") << '\n';
        std::cout << "  File2: " << (i < lines2.size() ? lines2[i] : "(no line)") << "\n\n";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./compare file1.out file2.out\n";
        return 1;
    }

    std::ifstream file1(argv[1]), file2(argv[2]);
    if (!file1 || !file2) {
        std::cerr << "Error opening files.\n";
        return 1;
    }

    std::vector<std::string> lines1, lines2;
    std::string line;

    while (std::getline(file1, line))
        lines1.push_back(line);
    while (std::getline(file2, line))
        lines2.push_back(line);

    size_t minLines = std::min(lines1.size(), lines2.size());
    for (size_t i = 0; i < minLines; ++i) {
        if (stripInvisible(lines1[i]) != stripInvisible(lines2[i])) {
            printContext(lines1, lines2, i);
            return 0;
        }
    }

    if (lines1.size() != lines2.size()) {
        std::cout << "Files differ in length.\n";
        size_t diffStart = minLines;
        printContext(lines1, lines2, diffStart);
    } else {
        std::cout << "Files are identical (ignoring invisible characters).\n";
    }

    return 0;
}
