cmake_minimum_required(VERSION 3.16)
project(qm_ring_queue_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
option(QM_RING_QUEUE_TEST_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer for ring queue tests"
       ON)

add_executable(test_ring_queue test_ring_queue.c ../qm_ring_queue.c)
target_include_directories(test_ring_queue PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}" "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/utility/ringqueue" "${QUMI_ROOT}/utility/string")
target_compile_features(test_ring_queue PRIVATE c_std_99)
target_compile_options(test_ring_queue PRIVATE -Wall -Wextra -Werror -UNDEBUG)
if(QM_RING_QUEUE_TEST_SANITIZERS)
    target_compile_options(test_ring_queue PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_ring_queue PRIVATE -fsanitize=address,undefined)
endif()

enable_testing()
add_test(NAME test_ring_queue COMMAND test_ring_queue)
set_tests_properties(test_ring_queue PROPERTIES TIMEOUT 15)
