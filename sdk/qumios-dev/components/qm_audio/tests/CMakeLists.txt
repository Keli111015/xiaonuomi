cmake_minimum_required(VERSION 3.16)
project(qm_audio_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
option(QM_AUDIO_CAPTURE_TEST_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" ON)
find_package(Threads REQUIRED)
enable_testing()
# Existing string helpers have warnings unrelated to Capture.
set_source_files_properties("${QUMI_ROOT}/utility/string/qm_string.c" PROPERTIES
    COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-parameter")

foreach(mode IN ITEMS push pull)
    add_executable(test_capture_${mode}
        test_capture.c
        "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
        "${QUMI_ROOT}/utility/timer/qm_utils_timer.c"
        "${QUMI_ROOT}/utility/ringqueue/qm_ring_queue.c"
        "${QUMI_ROOT}/utility/string/qm_string.c")
    target_include_directories(test_capture_${mode} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}" "${QUMI_ROOT}/include/common"
        "${QUMI_ROOT}/include/qmos" "${QUMI_ROOT}/utility/timer"
        "${QUMI_ROOT}/utility/ringqueue" "${QUMI_ROOT}/utility/string"
        "${CMAKE_CURRENT_LIST_DIR}/../include" "${CMAKE_CURRENT_LIST_DIR}/../priv_include")
    target_compile_features(test_capture_${mode} PRIVATE c_std_11)
    target_compile_options(test_capture_${mode} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(test_capture_${mode} PRIVATE Threads::Threads)
    if(mode STREQUAL "push")
        target_compile_definitions(test_capture_${mode} PRIVATE
            CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH=1 CONFIG_QM_AUDIO_CAPTURE_MODE_PULL=0)
    else()
        target_compile_definitions(test_capture_${mode} PRIVATE
            CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH=0 CONFIG_QM_AUDIO_CAPTURE_MODE_PULL=1)
    endif()
    if(QM_AUDIO_CAPTURE_TEST_SANITIZERS)
        target_compile_options(test_capture_${mode} PRIVATE -g -fsanitize=address,undefined)
        target_link_options(test_capture_${mode} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME test_capture_${mode} COMMAND test_capture_${mode})
    set_tests_properties(test_capture_${mode} PROPERTIES TIMEOUT 20)
endforeach()

add_executable(test_engine
    test_engine.c
    "${CMAKE_CURRENT_LIST_DIR}/../src/engine/qm_audio_engine.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/service/qm_audio_service.c"
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c"
    "${QUMI_ROOT}/utility/string/qm_string.c")
target_include_directories(test_engine PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}" "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/include/qmos" "${QUMI_ROOT}/utility/timer"
    "${QUMI_ROOT}/utility/string"
    "${CMAKE_CURRENT_LIST_DIR}/../include" "${CMAKE_CURRENT_LIST_DIR}/../priv_include")
target_compile_features(test_engine PRIVATE c_std_11)
target_compile_options(test_engine PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(test_engine PRIVATE Threads::Threads)
if(QM_AUDIO_CAPTURE_TEST_SANITIZERS)
    target_compile_options(test_engine PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_engine PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME test_engine COMMAND test_engine)
set_tests_properties(test_engine PROPERTIES TIMEOUT 20)
