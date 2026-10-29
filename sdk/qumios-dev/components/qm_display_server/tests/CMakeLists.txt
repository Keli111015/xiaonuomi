cmake_minimum_required(VERSION 3.16)
project(qm_display_client_server_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

add_executable(qm_display_client_server_tests
    test_client_server.c
    "${QUMI_ROOT}/components/qm_display_client/src/qm_display_client.c"
    "${QUMI_ROOT}/components/qm_display_server/src/qm_display_server.c"
    "${QUMI_ROOT}/utility/crc/qm_crc.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c"
)

target_include_directories(qm_display_client_server_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${QUMI_ROOT}/components/qm_display_client/include"
    "${QUMI_ROOT}/components/qm_display_server/include"
    "${QUMI_ROOT}/utility/crc"
    "${QUMI_ROOT}/utility/timer"
    "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/include/qmos"
)

target_compile_features(qm_display_client_server_tests PRIVATE c_std_99)
target_compile_options(qm_display_client_server_tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME qm_display_client_server_tests COMMAND qm_display_client_server_tests)
