cmake_minimum_required(VERSION 3.16)
project(joyinside_protocol_tests C)
set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
find_package(Threads REQUIRED)
enable_testing()
option(JOYINSIDE_TEST_SANITIZERS "Address and undefined behavior checks" ON)
set(common test_support.c test_cjson.c
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
    "${QUMI_ROOT}/utility/string/qm_string.c"
    "${QUMI_ROOT}/utility/base64/qm_utils_base64.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c"
    "${QUMI_ROOT}/components/backoff/src/backoff_algorithm.c")
function(configure_test name binary manual)
    target_include_directories(${name} PRIVATE . ../include ../priv_include
        "${QUMI_ROOT}/components/qm_websocket_client/include"
        "${QUMI_ROOT}/components/qm_http_client/include"
        "${QUMI_ROOT}/components/qm_time/include"
        "${QUMI_ROOT}/components/backoff/include" "${QUMI_ROOT}/components/cjson"
        "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
        "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer"
        "${QUMI_ROOT}/utility/base64" "${QUMI_ROOT}/utility/uuid"
        "${QUMI_ROOT}/utility/hmac" "${QUMI_ROOT}/utility/md5"
        "${QUMI_ROOT}/utility/sha1" "${QUMI_ROOT}/utility/sha256")
    target_compile_definitions(${name} PRIVATE CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY=${binary}
        CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL=${manual}
        CONFIG_QM_OS_TIMER_SUPPORT=1 CONFIG_QM_TIME_SUPPORT=1
        CONFIG_JOYINSIDE_CLIENT_CORE_STOP_TIMEOUT_MS=30)
    target_compile_features(${name} PRIVATE c_std_11)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    if(APPLE)
        target_link_options(${name} PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(${name} PRIVATE -Wl,--gc-sections)
    endif()
    if(JOYINSIDE_TEST_SANITIZERS)
        target_compile_options(${name} PRIVATE -g -fsanitize=address,undefined)
        target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 15)
endfunction()
foreach(binary 0 1 2)
    if(binary EQUAL 2)
        set(protocol_target test_protocol_no_media)
    else()
        set(protocol_target test_protocol_${binary})
    endif()
    add_executable(${protocol_target} test_protocol.c ${common}
        ../src/joyinside_client_auth.c ../src/joyinside_client_audiobook.c
        ../src/joyinside_client_print.c ../src/joyinside_client_media.c ../src/joyinside_client_vision.c
        "${QUMI_ROOT}/utility/hmac/qm_utils_hmac.c"
        "${QUMI_ROOT}/utility/md5/qm_utils_md5.c")
    if(binary EQUAL 2)
        configure_test(${protocol_target} 1 0)
        target_compile_definitions(${protocol_target} PRIVATE CONFIG_JOYINSIDE_CLIENT_MEDIA_SUPPORT=0)
        continue()
    endif()
    configure_test(${protocol_target} ${binary} 1)
    target_compile_definitions(${protocol_target} PRIVATE CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT=1)
    foreach(manual 0 1)
        add_executable(test_ingress_${binary}_${manual}
            "${QUMI_ROOT}/components/qm_agent/tests/test_joyinside_ingress.c" ${common})
        configure_test(test_ingress_${binary}_${manual} ${binary} ${manual})
    endforeach()
endforeach()
set_source_files_properties(test_cjson.c PROPERTIES COMPILE_OPTIONS -Wno-deprecated-declarations)
set_source_files_properties("${QUMI_ROOT}/utility/string/qm_string.c" PROPERTIES
    COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-parameter")
set_source_files_properties("${QUMI_ROOT}/utility/base64/qm_utils_base64.c" PROPERTIES COMPILE_OPTIONS -Wno-sign-compare)
