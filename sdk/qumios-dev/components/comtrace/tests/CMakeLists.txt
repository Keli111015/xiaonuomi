cmake_minimum_required(VERSION 3.16)
project(comtrace_tests C)

set(QUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

function(add_comtrace_ota_test target stream_verify abort_support)
    add_executable(${target}
        test_comtrace_ota.c
        "${QUMI_ROOT}/components/comtrace/src/comtrace_frame.c"
        "${QUMI_ROOT}/components/comtrace/src/comtrace_ota.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborencoder.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborencoder_close_container_checked.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborencoder_float.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborerrorstrings.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborparser.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborparser_dup_string.c"
        "${QUMI_ROOT}/components/tinycbor/src/cborparser_float.c"
        "${QUMI_ROOT}/utility/sha256/qm_utils_sha256.c"
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
        "${QUMI_ROOT}/components/comtrace/include"
        "${QUMI_ROOT}/components/comtrace/src"
        "${QUMI_ROOT}/components/tinycbor/include"
        "${QUMI_ROOT}/include"
        "${QUMI_ROOT}/include/common"
        "${QUMI_ROOT}/include/qmos"
        "${QUMI_ROOT}/include/hal/ota"
        "${QUMI_ROOT}/utility/sha256"
        "${QUMI_ROOT}/utility/string"
    )
    target_compile_definitions(${target} PRIVATE
        CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY=${stream_verify}
        CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT=${abort_support}
    )
    target_compile_features(${target} PRIVATE c_std_99)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
endfunction()

add_comtrace_ota_test(comtrace_tests 0 1)
add_comtrace_ota_test(comtrace_stream_tests 1 0)

enable_testing()
add_test(NAME comtrace_tests COMMAND comtrace_tests)
add_test(NAME comtrace_stream_tests COMMAND comtrace_stream_tests)
