cmake_minimum_required(VERSION 3.16)
project(gx8006_tests C)
set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
find_package(Threads REQUIRED)
enable_testing()
add_executable(test_gx8006 test_gx8006.c
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c")
target_include_directories(test_gx8006 PRIVATE . ../include
    "${QUMI_ROOT}/components/qm_audio/include"
    "${QUMI_ROOT}/components/qm_audio/include/adapter"
    "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
    "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
    "${QUMI_ROOT}/include/hal/soc" "${QUMI_ROOT}/utility/timer"
    "${QUMI_ROOT}/utility/string")
target_compile_definitions(test_gx8006 PRIVATE CONFIG_QM_GX8006_STOP_TIMEOUT_MS=40
    CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH=1)
target_compile_options(test_gx8006 PRIVATE -Wall -Wextra -Werror -g
    -fsanitize=address,undefined -UNDEBUG)
target_link_options(test_gx8006 PRIVATE -fsanitize=address,undefined)
target_link_libraries(test_gx8006 PRIVATE Threads::Threads)
add_test(NAME test_gx8006 COMMAND test_gx8006)
set_tests_properties(test_gx8006 PROPERTIES TIMEOUT 15)
