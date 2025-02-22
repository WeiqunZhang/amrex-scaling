cmake_minimum_required(VERSION 3.24)

project(ffttest DESCRIPTION "Testing FFT" VERSION 1.0 LANGUAGES C CXX)

if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "\nIn-source builds are not allowed.\n")
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_CONFIGURATION_TYPES "Release;Debug;MinSizeRel;RelWithDebInfo")
    set(CMAKE_BUILD_TYPE Release
        CACHE STRING
        "Choose the build type, e.g. Release, Debug, or RelWithDebInfo." FORCE)
endif()

option(ENABLE_CUDA "Enable CUDA support" OFF)
if (ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    enable_language(CUDA)
endif()

option(ENABLE_HIP "Enable HIP support" OFF)
if (ENABLE_HIP)
    enable_language(HIP)
endif()

option(ENABLE_SYCL "Enable SYCL support" OFF)
if (ENABLE_SYCL)
    set(CMAKE_CXX_COMPILER icpx)
endif()

find_package(AMReX REQUIRED)

add_executable(ffttest)

target_sources(ffttest PRIVATE main.cpp)

target_link_libraries(ffttest PRIVATE AMReX::amrex)

target_compile_features(ffttest PRIVATE cxx_std_17)

get_target_property(ffttest_sources ffttest SOURCES)
list(FILTER ffttest_sources INCLUDE REGEX "\\.cpp$")

if (ENABLE_CUDA)
    set_source_files_properties(${ffttest_sources} PROPERTIES LANGUAGE CUDA)
    if (NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        message(STATUS "No CUDA architectures specified. Native will be used.")
        set(CMAKE_CUDA_ARCHITECTURES native)
    endif()
    set_target_properties(ffttest PROPERTIES
                          CUDA_ARCHITECTURES ${CMAKE_CUDA_ARCHITECTURES}
                          CUDA_SEPARABLE_COMPILATION ON)
    target_compile_features(ffttest PRIVATE cuda_std_17)
    target_compile_options(ffttest PRIVATE
                           $<$<COMPILE_LANGUAGE:CUDA>:-m64>
                           $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
                           $<$<COMPILE_LANGUAGE:CUDA>:--expt-extended-lambda>
                           $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>)
    target_link_libraries(ffttest PRIVATE CUDA::cudart)
endif()

if (ENABLE_HIP)
    set_source_files_properties(${ffttest_sources} PROPERTIES LANGUAGE HIP)
    if (NOT DEFINED CMAKE_HIP_ARCHITECTURES)
        message(STATUS "No HIP architectures specified. 'gfx90a' will be used.")
        set(CMAKE_HIP_ARCHITECTURES gfx90a)
    endif()
    set_target_properties(ffttest PROPERTIES HIP_ARCHITECTURES ${CMAKE_HIP_ARCHITECTURES})
    target_compile_features(ffttest PRIVATE hip_std_17)
endif()

if (ENABLE_SYCL)
    target_compile_options(ffttest PRIVATE -fsycl)
    target_link_options(ffttest PRIVATE -fsycl -fsycl-device-lib=libc,libm-fp32,libm-fp64)
endif()
