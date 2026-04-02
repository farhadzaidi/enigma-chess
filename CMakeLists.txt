# Settings
cmake_minimum_required(VERSION 3.16)
project(enigma LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to release build (optimized)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type." FORCE)
endif()

# --- Shared compiler/linker options ---

set(COMMON_COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:
    -O3 -march=native -mtune=native -DNDEBUG
    -ffunction-sections -fdata-sections
    -flto -fomit-frame-pointer -fno-exceptions -fno-rtti
    -Wl,--gc-sections
  >
  $<$<CXX_COMPILER_ID:MSVC>:
    /O2 /Ob3 /DNDEBUG /GL
  >
)

set(COMMON_LINK_OPTIONS
  $<$<CXX_COMPILER_ID:GNU>:
    -flto -Wl,--gc-sections
  >
  $<$<CXX_COMPILER_ID:Clang>:
    -flto=thin -Wl,--gc-sections
  >
  $<$<CXX_COMPILER_ID:MSVC>:
    /LTCG
  >
)

# --- Embed NNUE weights binary ---

set(NNUE_BIN ${CMAKE_SOURCE_DIR}/src/data/nnue.bin)
set(NNUE_OBJ ${CMAKE_BINARY_DIR}/nnue.o)

add_custom_command(
  OUTPUT ${NNUE_OBJ}
  COMMAND objcopy -I binary -O elf64-x86-64 -B i386:x86-64
    --add-section .note.GNU-stack=/dev/null
    --set-section-flags .note.GNU-stack=noload,readonly
    nnue.bin ${NNUE_OBJ}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/data
  DEPENDS ${NNUE_BIN}
)

# --- Engine library ---

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ENGINE_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

add_library(enigma-lib STATIC ${ENGINE_SOURCES} ${NNUE_OBJ})
target_include_directories(enigma-lib PRIVATE src)
target_compile_options(enigma-lib PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(enigma-lib PRIVATE ${COMMON_LINK_OPTIONS})

# --- Engine binary ---

add_executable(enigma src/main.cpp)
target_include_directories(enigma PRIVATE src)
target_link_libraries(enigma PRIVATE enigma-lib)
target_compile_options(enigma PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(enigma PRIVATE ${COMMON_LINK_OPTIONS})

# --- Dev binary (tests + bench) ---

file(GLOB_RECURSE DEV_SOURCES CONFIGURE_DEPENDS dev/*.cpp)
list(REMOVE_ITEM DEV_SOURCES "${CMAKE_SOURCE_DIR}/dev/main.cpp")
add_executable(enigma-dev dev/main.cpp ${DEV_SOURCES})
target_include_directories(enigma-dev PRIVATE src dev)
target_compile_definitions(enigma-dev PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(enigma-dev PRIVATE enigma-lib)
target_compile_options(enigma-dev PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(enigma-dev PRIVATE ${COMMON_LINK_OPTIONS})

# --- Interprocedural optimization ---

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
if(ipo_supported)
  set_property(TARGET enigma-lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET enigma PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET enigma-dev PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
