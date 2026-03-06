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

# --- Engine binary ---

add_executable(enigma src/main.cpp)
target_include_directories(enigma PRIVATE src)
target_compile_options(enigma PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(enigma PRIVATE ${COMMON_LINK_OPTIONS})

# --- Debug binary (tests + bench) ---

file(GLOB_RECURSE DEBUG_SOURCES CONFIGURE_DEPENDS debug/tests/*.cpp)
add_executable(enigma-debug debug/main.cpp ${DEBUG_SOURCES})
target_include_directories(enigma-debug PRIVATE src debug)
target_compile_definitions(enigma-debug PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(enigma-debug PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(enigma-debug PRIVATE ${COMMON_LINK_OPTIONS})

# --- Interprocedural optimization ---

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
if(ipo_supported)
  set_property(TARGET enigma PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET enigma-debug PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
