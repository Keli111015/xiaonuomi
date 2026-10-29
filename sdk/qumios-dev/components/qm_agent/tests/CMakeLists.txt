cmake_minimum_required(VERSION 3.16)
project(qm_agent_audio_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
find_package(Threads REQUIRED)
include_directories("${QUMI_ROOT}/components/backoff/include")
option(QM_AGENT_TEST_SANITIZERS "Enable address and undefined-behavior checks" ON)

enable_testing()
foreach(lgeine 0 1)
    set(target test_provider_ingress_${lgeine})
    add_executable(${target} test_provider_ingress.c
        "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c")
    target_include_directories(${target} PRIVATE .
        "${QUMI_ROOT}/components/lgeine_agent/include"
        "${QUMI_ROOT}/components/baidu_chat_agent/include"
        "${QUMI_ROOT}/components/qm_websocket_client/include"
        "${QUMI_ROOT}/components/cjson"
        "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
        "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/sprintf")
    target_compile_definitions(${target} PRIVATE TEST_LGEINE=${lgeine}
        CONFIG_QM_SPRINTF_SUPPORT=1 CONFIG_QM_OS_TIMER_SUPPORT=1)
    # Existing protocol diagnostics compile out in this focused ingress test.
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG
        -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
        -Wno-unused-but-set-variable
        -ffunction-sections -fdata-sections)
    target_link_libraries(${target} PRIVATE Threads::Threads)
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
add_executable(test_websocket_queue test_websocket_queue.c
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c")
target_include_directories(test_websocket_queue PRIVATE .
    "${QUMI_ROOT}/components/qm_websocket_client/include"
    "${QUMI_ROOT}/components/backoff/include"
    "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
    "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
    "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer"
    "${QUMI_ROOT}/utility/base64" "${QUMI_ROOT}/utility/sha1"
    "${QUMI_ROOT}/utility/sprintf")
target_compile_options(test_websocket_queue PRIVATE -Wall -Wextra -Werror -UNDEBUG
    -ffunction-sections -fdata-sections)
target_link_libraries(test_websocket_queue PRIVATE Threads::Threads)
target_compile_definitions(test_websocket_queue PRIVATE CONFIG_QM_SPRINTF_SUPPORT=1)
if(APPLE)
    target_link_options(test_websocket_queue PRIVATE -Wl,-dead_strip)
else()
    target_link_options(test_websocket_queue PRIVATE -Wl,--gc-sections)
endif()
if(QM_AGENT_TEST_SANITIZERS)
    target_compile_options(test_websocket_queue PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_websocket_queue PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME test_websocket_queue COMMAND test_websocket_queue)
set_tests_properties(test_websocket_queue PROPERTIES TIMEOUT 10)
foreach(binary 0 1)
  foreach(manual 0 1)
    set(target test_joyinside_ingress_${binary}_${manual})
    add_executable(${target} test_joyinside_ingress.c test_cjson.c
        "${QUMI_ROOT}/components/joyinside_client/tests/test_support.c"
        "${QUMI_ROOT}/components/backoff/src/backoff_algorithm.c"
        "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
        "${QUMI_ROOT}/utility/string/qm_string.c"
        "${QUMI_ROOT}/utility/base64/qm_utils_base64.c"
        "${QUMI_ROOT}/utility/timer/qm_utils_timer.c")
    target_include_directories(${target} PRIVATE .
        "${QUMI_ROOT}/components/joyinside_client/include"
        "${QUMI_ROOT}/components/joyinside_client/priv_include"
        "${QUMI_ROOT}/components/qm_http_client/include"
        "${QUMI_ROOT}/components/qm_time/include"
        "${QUMI_ROOT}/components/qm_websocket_client/include"
        "${QUMI_ROOT}/components/cjson"
        "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
        "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer"
        "${QUMI_ROOT}/utility/base64" "${QUMI_ROOT}/utility/uuid")
    target_compile_definitions(${target} PRIVATE CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY=${binary}
        CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL=${manual} CONFIG_QM_OS_TIMER_SUPPORT=1
        CONFIG_JOYINSIDE_CLIENT_CORE_STOP_TIMEOUT_MS=30)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG
        -ffunction-sections -fdata-sections)
    target_link_libraries(${target} PRIVATE Threads::Threads)
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
endforeach()
foreach(decoder 0 1)
    set(target test_agent_audio_${decoder})
    add_executable(${target} test_audio.c
        "${QUMI_ROOT}/components/backoff/src/backoff_algorithm.c"
        "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
        "${QUMI_ROOT}/utility/timer/qm_utils_timer.c")
    target_include_directories(${target} PRIVATE . ../include ../priv_include
        "${QUMI_ROOT}/components/qm_audio/include"
        "${QUMI_ROOT}/components/qm_event/include"
        "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
        "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer")
    target_compile_definitions(${target} PRIVATE CONFIG_QM_AUDIO_DECODER_SUPPORT=${decoder})
    target_compile_features(${target} PRIVATE c_std_99)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG
        -ffunction-sections -fdata-sections)
    target_link_libraries(${target} PRIVATE Threads::Threads)
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

add_executable(test_agent_core test_core.c
    "${QUMI_ROOT}/components/qm_agent/src/core/qm_agent_control.c"
    "${QUMI_ROOT}/components/qm_event/src/qm_event.c"
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c")
target_link_libraries(test_agent_core PRIVATE Threads::Threads)
target_include_directories(test_agent_core PRIVATE . ../include ../priv_include
    "${QUMI_ROOT}/components/qm_audio/include"
    "${QUMI_ROOT}/components/qm_event/include"
    "${QUMI_ROOT}/components/qm_cloud/api"
    "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
    "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
    "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer")
target_compile_options(test_agent_core PRIVATE -Wall -Wextra -Werror -UNDEBUG
    -ffunction-sections -fdata-sections)
if(APPLE)
    target_link_options(test_agent_core PRIVATE -Wl,-dead_strip)
else()
    target_link_options(test_agent_core PRIVATE -Wl,--gc-sections)
endif()
if(QM_AGENT_TEST_SANITIZERS)
    target_compile_options(test_agent_core PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_agent_core PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME test_agent_core COMMAND test_agent_core)
set_tests_properties(test_agent_core PROPERTIES TIMEOUT 10)

add_executable(test_agent_audio_integration test_core.c test_audio_integration.c
    "${QUMI_ROOT}/components/qm_agent/src/core/qm_agent_control.c"
    "${QUMI_ROOT}/components/qm_agent/src/audio/qm_agent_audio_engine.c"
    "${QUMI_ROOT}/components/qm_audio/src/control/qm_audio_control.c"
    "${QUMI_ROOT}/components/qm_audio/src/service/qm_audio_service.c"
    "${QUMI_ROOT}/components/qm_audio/src/pipeline/qm_audio_pipeline.c"
    "${QUMI_ROOT}/components/qm_audio/src/audio/qm_audio.c"
    "${QUMI_ROOT}/components/qm_audio/src/engine/qm_audio_engine.c"
    "${QUMI_ROOT}/components/qm_audio/src/capture/qm_audio_capture.c"
    "${QUMI_ROOT}/components/qm_audio/src/event/qm_audio_event.c"
    "${QUMI_ROOT}/components/qm_event/src/qm_event.c"
    "${QUMI_ROOT}/components/backoff/src/backoff_algorithm.c"
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c"
    "${QUMI_ROOT}/utility/timer/qm_utils_timer.c")
target_include_directories(test_agent_audio_integration PRIVATE . ../include ../priv_include
    "${QUMI_ROOT}/components/qm_audio/include" "${QUMI_ROOT}/components/qm_audio/priv_include"
    "${QUMI_ROOT}/components/qm_event/include" "${QUMI_ROOT}/components/qm_cloud/api"
    "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos" "${QUMI_ROOT}/include/common"
    "${QUMI_ROOT}/include/hal" "${QUMI_ROOT}/include/hal/fs"
    "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer"
    "${QUMI_ROOT}/utility/ringqueue")
target_sources(test_agent_audio_integration PRIVATE
    "${QUMI_ROOT}/utility/ringqueue/qm_ring_queue.c")
target_compile_definitions(test_agent_audio_integration PRIVATE QM_AGENT_TEST_REAL_AUDIO=1
    CONFIG_QM_AUDIO_DECODER_SUPPORT=0 CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT=0
    CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT=0 CONFIG_QM_AUDIO_CAPTURE_SUPPORT=1
    CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH=0 CONFIG_QM_AUDIO_CAPTURE_MODE_PULL=1
    CONFIG_QM_AGENT_AUDIO_OPERATION_TIMEOUT_MS=30)
target_compile_options(test_agent_audio_integration PRIVATE -Wall -Wextra -Werror -UNDEBUG
    -ffunction-sections -fdata-sections)
target_link_libraries(test_agent_audio_integration PRIVATE Threads::Threads)
if(APPLE)
    target_link_options(test_agent_audio_integration PRIVATE -Wl,-dead_strip)
else()
    target_link_options(test_agent_audio_integration PRIVATE -Wl,--gc-sections)
endif()
if(QM_AGENT_TEST_SANITIZERS)
    target_compile_options(test_agent_audio_integration PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_agent_audio_integration PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME test_agent_audio_integration COMMAND test_agent_audio_integration)
set_tests_properties(test_agent_audio_integration PROPERTIES TIMEOUT 15)

get_target_property(control_event_sources test_agent_audio_integration SOURCES)
list(REMOVE_ITEM control_event_sources test_audio_integration.c
    "${QUMI_ROOT}/components/qm_audio/src/event/qm_audio_event.c")
add_executable(test_agent_audio_control_event ${control_event_sources}
    "${QUMI_ROOT}/components/qm_audio/tests/test_control_event.c")
foreach(property IN ITEMS INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS
                          LINK_OPTIONS LINK_LIBRARIES)
    get_target_property(control_event_value test_agent_audio_integration ${property})
    if(control_event_value)
        set_property(TARGET test_agent_audio_control_event PROPERTY ${property}
                     "${control_event_value}")
    endif()
endforeach()
add_test(NAME test_agent_audio_control_event COMMAND test_agent_audio_control_event)
set_tests_properties(test_agent_audio_control_event PROPERTIES TIMEOUT 15)

add_executable(test_agent_module test_module.c test_cjson.c
    "${QUMI_ROOT}/utility/list/qm_utils_list.c"
    "${QUMI_ROOT}/utility/string/qm_string.c"
    "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c")
target_link_libraries(test_agent_module PRIVATE Threads::Threads)
target_include_directories(test_agent_module PRIVATE . ../include ../priv_include
    "${QUMI_ROOT}/components/qm_audio/include"
    "${QUMI_ROOT}/components/qm_event/include"
    "${QUMI_ROOT}/components/qm_cloud/api"
    "${QUMI_ROOT}/components/qm_cloud/mqtt"
    "${QUMI_ROOT}/components/qm_cloud/config"
    "${QUMI_ROOT}/components/qm_cloud/common"
    "${QUMI_ROOT}/components/qm_cloud/include"
    "${QUMI_ROOT}/components/qm_spec/include"
    "${QUMI_ROOT}/components/cjson"
    "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
    "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
    "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/list"
    "${QUMI_ROOT}/utility/timer")
target_compile_features(test_agent_module PRIVATE c_std_99)
target_compile_options(test_agent_module PRIVATE -Wall -Wextra -Werror -UNDEBUG
    -ffunction-sections -fdata-sections)
if(APPLE)
    target_link_options(test_agent_module PRIVATE -Wl,-dead_strip)
else()
    target_link_options(test_agent_module PRIVATE -Wl,--gc-sections)
endif()
if(QM_AGENT_TEST_SANITIZERS)
    target_compile_options(test_agent_module PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_agent_module PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME test_agent_module COMMAND test_agent_module)
set_tests_properties(test_agent_module PROPERTIES TIMEOUT 10)

add_subdirectory(transport)

foreach(channel 0 1)
    set(target test_agent_auth_${channel})
    add_executable(${target} test_auth.c test_cjson.c
        "${QUMI_ROOT}/components/backoff/src/backoff_algorithm.c"
        "${QUMI_ROOT}/utility/list/qm_utils_list.c"
        "${QUMI_ROOT}/utility/string/qm_string.c"
        "${QUMI_ROOT}/platform/mcu/macos/qmos/qm_os.c")
    target_include_directories(${target} PRIVATE . ../include ../priv_include
        "${QUMI_ROOT}/components/qm_audio/include"
        "${QUMI_ROOT}/components/qm_event/include"
        "${QUMI_ROOT}/components/qm_cloud/api"
        "${QUMI_ROOT}/components/qm_cloud/mqtt"
        "${QUMI_ROOT}/components/qm_cloud/config"
        "${QUMI_ROOT}/components/qm_cloud/common"
        "${QUMI_ROOT}/components/qm_cloud/include"
        "${QUMI_ROOT}/components/qm_spec/include"
        "${QUMI_ROOT}/components/qm_http_client/include"
        "${QUMI_ROOT}/components/backoff/include" "${QUMI_ROOT}/components/cjson"
        "${QUMI_ROOT}/include" "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/common" "${QUMI_ROOT}/include/hal"
        "${QUMI_ROOT}/utility/string" "${QUMI_ROOT}/utility/timer"
        "${QUMI_ROOT}/utility/list" "${QUMI_ROOT}/utility/hmac"
        "${QUMI_ROOT}/utility/md5" "${QUMI_ROOT}/utility/sha1"
        "${QUMI_ROOT}/utility/sha256")
    target_compile_definitions(${target} PRIVATE CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP=${channel})
    target_compile_features(${target} PRIVATE c_std_99)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG
        -ffunction-sections -fdata-sections)
    target_link_libraries(${target} PRIVATE Threads::Threads)
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

if(APPLE)
    set_source_files_properties(test_cjson.c PROPERTIES
        COMPILE_OPTIONS -Wno-deprecated-declarations)
endif()
set_source_files_properties("${QUMI_ROOT}/utility/string/qm_string.c" PROPERTIES
    COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-parameter")
set_source_files_properties("${QUMI_ROOT}/utility/base64/qm_utils_base64.c" PROPERTIES
    COMPILE_OPTIONS "-Wno-sign-compare")
