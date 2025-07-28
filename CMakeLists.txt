cmake_minimum_required(VERSION 3.22.1)
project(CPU-Simulator)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wall)
add_compile_options(-O2)
add_compile_options(-g)

include_directories(include)

add_executable(code 
    src/cpu_state.cpp
    src/instruction.cpp
    src/process.cpp
    src/riscv_simulator.cpp
    main.cpp
)

target_compile_options(code PRIVATE -fsanitize=address,leak,undefined)
target_link_libraries(code PRIVATE -fsanitize=address,leak,undefined)