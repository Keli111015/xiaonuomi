# Find all test files in the current directory
file(GLOB TEST_FILES "${CMAKE_CURRENT_LIST_DIR}/*_test.cmake")

# Run the test files
foreach(test_file ${TEST_FILES})
    message(STATUS "Running: ${test_file}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -P "${test_file}"
        RESULT_VARIABLE result
    )

    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Test failed: ${test_file}")
    endif()
endforeach()
