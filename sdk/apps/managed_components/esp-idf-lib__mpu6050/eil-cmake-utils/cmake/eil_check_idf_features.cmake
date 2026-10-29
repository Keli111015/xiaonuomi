# Prevent double inclusion
if(COMMAND eil_check_idf_features)
    return()
endif()

# @brief Executes all available IDF feature validation checks
#
# This macro invokes all feature validation checks defined in
# `eil_check_idf_features.cmake` and exports any variables they define.
#
# @note Intended primarily for testing purposes. In production code,
#       explicitly call only the specific checks your project requires.
macro(eil_check_idf_features)
    message(DEBUG "eil_check_idf_features")

    # Definition of features. implement eil_check_idf_* below
    set(features eil_check_idf_has_esp_drivers)
    foreach(feat ${features})
        if(COMMAND ${feat})
            cmake_language(CALL ${feat})
        else()
            message(FATAL, "BUG: Undefined call to ${feat}")
        endif()
    endforeach()
endmacro()

# @brief Checks if the esp-idf version supports `esp_driver_*` components. Exports a boolean variable,`EIL_IDF_HAS_ESP_DRIVERS`.
#
# Before version 5.3, the conponent for peripherials was a monolithic `driver`.
# Since version 5.3, each driver, such as `gpio` has its own component, i.e.,
# `esp_driver_gpio`.
#
# See
# [Migration from 5.2 to 5.3](https://docs.espressif.com/projects/esp-idf/en/release-v6.0/esp32/migration-guides/release-5.x/5.3/peripherals.html)
# for more details.
#
# When a source includes `driver/gpio.h":
#
# ```cmake
# include(eil_check_idf_features)
# eil_check_idf_has_esp_drivers()
#
# # List of common ESP-IDF components required by this component
# set(REQUIRED_COMPONENTS freertos esp_idf_lib_helpers)
#
# if(EIL_IDF_HAS_ESP_DRIVERS)
#     list(APPEND REQUIRED_COMPONENTS esp_driver_gpio)
# else()
#     list(APPEND REQUIRED_COMPONENTS driver)
# endif()
#
# idf_component_register(
#     SRCS src.c
#     INCLUDE_DIRS .
#     REQUIRES ${REQUIRED_COMPONENTS}
# )
# ```
#
# @note When `EIL_IDF_HAS_ESP_DRIVERS` is true, add `esp_driver_*` to `REQUIRES` or
#       `PRIV_REQUIRES` when calling `idf_component_register()`.
#       When false, add `driver` to REQUIRES or PRIV_REQUIRES.
#
# @note `REQUIRES` should be set to all components whose header files are
#       `#included` from the public header files of this component.
#
# @note `PRIV_REQUIRES` should be set to all components whose header files are
#       `#included` from any source files in this component, unless already listed
#       in REQUIRES.
#
# @note Dependencies, such as REQUIRES, cannot be nodified after
#       idf_component_register(). idf_component_set_property() cannot either.
macro(eil_check_idf_has_esp_drivers)
    # Check for esp_driver_* support (ESP-IDF ≥ 5.3)
    set(EIL_IDF_HAS_ESP_DRIVERS FALSE)
    if((IDF_VERSION_MAJOR GREATER_EQUAL 6) OR
       (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
       set(EIL_IDF_HAS_ESP_DRIVERS TRUE)
    endif()

    # CACHE store this entry in the cache file rather than keeping it as a
    #       local, temporary variable.
    set(EIL_IDF_HAS_ESP_DRIVERS "${EIL_IDF_HAS_ESP_DRIVERS}" CACHE INTERNAL "esp-idf supports esp_driver_ components")
    message(DEBUG " - EIL_IDF_HAS_ESP_DRIVERS: ${EIL_IDF_HAS_ESP_DRIVERS}")
endmacro()
