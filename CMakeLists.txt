cmake_minimum_required(VERSION 3.22.1)
project(CPU-Simulator)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wall)
add_compile_options(-Ofast)

include_directories(include)

add_executable(code 
    src/cpu_state.cpp
    src/instruction.cpp
    src/out_of_order_processor.cpp
    src/riscv_simulator.cpp
    main.cpp
)