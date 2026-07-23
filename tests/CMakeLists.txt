include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(VectronTests
    test_oscillator.cpp
    test_vector_engine.cpp
    test_vector_lfo.cpp
    test_svf_filter.cpp
    test_sub_oscillator.cpp
    test_noise_generator.cpp
    test_drive_shaper.cpp
    test_svf_cascade.cpp
    test_filter_math.cpp
    test_adsr_envelope.cpp
    test_mod_lfo.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/osc/VectorEngine.cpp
    ${CMAKE_SOURCE_DIR}/source/dsp/noise/NoiseGenerator.cpp)
target_include_directories(VectronTests PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(VectronTests PRIVATE cxx_std_20)
target_link_libraries(VectronTests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(VectronTests)
