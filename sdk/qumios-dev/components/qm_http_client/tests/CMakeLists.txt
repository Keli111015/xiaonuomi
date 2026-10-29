cmake_minimum_required(VERSION 3.16)
project(qm_http_interrupt_test C)
set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_executable(test_interrupt test_interrupt.c ../src/qm_http_client.c
    "${QUMI_ROOT}/utility/string/qm_string.c"
    "${QUMI_ROOT}/utility/base64/qm_utils_base64.c"
    "${QUMI_ROOT}/utility/md5/qm_utils_md5.c"
    "${QUMI_ROOT}/utility/sha256/qm_utils_sha256.c")
target_include_directories(test_interrupt PRIVATE . ../include
    "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/include/qmos" "${QUMI_ROOT}/utility/string"
    "${QUMI_ROOT}/utility/base64" "${QUMI_ROOT}/utility/md5"
    "${QUMI_ROOT}/utility/sha256")
target_compile_features(test_interrupt PRIVATE c_std_99)
target_compile_options(test_interrupt PRIVATE -Wall -Wextra -UNDEBUG
    -g -fsanitize=address,undefined)
target_link_options(test_interrupt PRIVATE -fsanitize=address,undefined)
enable_testing()
add_test(NAME test_interrupt COMMAND test_interrupt)
set_tests_properties(test_interrupt PROPERTIES TIMEOUT 10)
