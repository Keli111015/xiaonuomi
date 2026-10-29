cmake_minimum_required(VERSION 3.16)
project(mdns_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

add_executable(mdns_tests
    test_mdns.c
    "${QUMI_ROOT}/components/mdns/src/mdns.c"
)

target_include_directories(mdns_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${QUMI_ROOT}/components/mdns/include"
    "${QUMI_ROOT}/include"
    "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/include/hal/net"
    "${QUMI_ROOT}/utility/string"
)

target_compile_features(mdns_tests PRIVATE c_std_99)
target_compile_options(mdns_tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME mdns_tests COMMAND mdns_tests)
