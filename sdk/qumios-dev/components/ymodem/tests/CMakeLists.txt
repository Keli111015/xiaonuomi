cmake_minimum_required(VERSION 3.16)
project(ymodem_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

add_executable(ymodem_tests
    test_ymodem.c
    "${QUMI_ROOT}/components/ymodem/src/ymodem.c"
    "${QUMI_ROOT}/utility/crc/qm_crc.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c"
)

target_include_directories(ymodem_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${QUMI_ROOT}/components/ymodem/include"
    "${QUMI_ROOT}/utility/crc"
    "${QUMI_ROOT}/utility/string"
    "${QUMI_ROOT}/utility/timer"
    "${QUMI_ROOT}/include"
    "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/include/qmos"
)

target_compile_features(ymodem_tests PRIVATE c_std_99)
target_compile_options(ymodem_tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME ymodem_tests COMMAND ymodem_tests)
