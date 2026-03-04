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

# Compile src and test files
file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS tests/*.cpp)
add_executable(${PROJECT_NAME} src/main.cpp ${TESTS})
target_include_directories(${PROJECT_NAME} PRIVATE src tests)

# Expose project root to source code
add_compile_definitions(PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# Compiler options for maximum optimization
target_compile_options(${PROJECT_NAME} PRIVATE
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

# Linker options for maxmimum optimization
target_link_options(${PROJECT_NAME} PRIVATE
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

# Interprocedural optimization
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
if(ipo_supported)
  set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
