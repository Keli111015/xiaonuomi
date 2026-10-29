cmake_minimum_required(VERSION 3.16)
project(qm_agent_transport_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
option(QM_AGENT_TEST_SANITIZERS "Enable address and undefined-behavior checks" ON)
enable_testing()

foreach(transport JOYINSIDE LGEINE BAIDU AGORA)
    string(TOLOWER "${transport}" adapter)
    set(target test_agent_transport_${adapter})
    add_executable(${target} ../test_transport.c)
    target_include_directories(${target} PRIVATE .. ../../include ../../priv_include
        "${QUMI_ROOT}/components/backoff/include"
        "${QUMI_ROOT}/components/qm_audio/include"
        "${QUMI_ROOT}/components/qm_event/include"
        "${QUMI_ROOT}/components/joyinside_client/include"
        "${QUMI_ROOT}/components/lgeine_agent/include"
        "${QUMI_ROOT}/components/baidu_chat_agent/include"
        "${QUMI_ROOT}/platform/mcu/esp32s3/qm_agent/agora_iot_sdk/include"
        "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
        "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer")
    target_compile_definitions(${target} PRIVATE CONFIG_QM_AGENT_TRANSPORT_${transport}=1)
    if(transport STREQUAL "JOYINSIDE")
        target_compile_definitions(${target} PRIVATE CONFIG_JOYINSIDE_CLIENT_SUPPORT=1)
    elseif(transport STREQUAL "LGEINE")
        target_compile_definitions(${target} PRIVATE CONFIG_LGEINE_AGENT_SUPPORT=1)
    elseif(transport STREQUAL "BAIDU")
        target_compile_definitions(${target} PRIVATE CONFIG_BAIDU_CHAT_AGENT_SUPPORT=1)
    endif()
    target_compile_features(${target} PRIVATE c_std_99)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG
        -ffunction-sections -fdata-sections)
    if(transport STREQUAL "AGORA")
        # Existing Agora diagnostics compile out when host logging is disabled.
        target_compile_options(${target} PRIVATE -Wno-unused-parameter
            -Wno-unused-variable -Wno-unused-function)
    endif()
    if(APPLE)
        target_link_options(${target} PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(${target} PRIVATE -Wl,--gc-sections)
    endif()
    if(QM_AGENT_TEST_SANITIZERS)
        target_compile_options(${target} PRIVATE -g -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 10)
endforeach()
