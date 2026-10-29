list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/eil-cmake-utils/cmake)
include(eil_check_idf_features)
eil_check_idf_has_esp_drivers()

# List of common ESP-IDF components required by this component
set(REQUIRED_COMPONENTS log freertos esp_idf_lib_helpers)

# Add version-specific required componenets
if(EIL_IDF_HAS_ESP_DRIVERS)
    list(APPEND REQUIRED_COMPONENTS esp_driver_gpio esp_driver_i2c)
else()
    list(APPEND REQUIRED_COMPONENTS driver)
endif()

# ESP-IDF version detection for automatic driver selection
# Check for manual override via Kconfig
if(CONFIG_I2CDEV_USE_LEGACY_DRIVER)
    set(USE_LEGACY_DRIVER TRUE)
    message(STATUS "i2cdev: Manual override - using legacy driver (CONFIG_I2CDEV_USE_LEGACY_DRIVER=y)")
elseif(NOT DEFINED IDF_VERSION_MAJOR)
    # In case older ESP-IDF versions that don't define IDF_VERSION_MAJOR
    set(USE_LEGACY_DRIVER TRUE)
    message(STATUS "i2cdev: IDF_VERSION_MAJOR not defined, using legacy driver")
elseif(IDF_VERSION_MAJOR LESS 5)
    set(USE_LEGACY_DRIVER TRUE)
    message(STATUS "i2cdev: ESP-IDF v${IDF_VERSION_MAJOR}.x detected, using legacy driver")
elseif(IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR LESS 3)
    set(USE_LEGACY_DRIVER TRUE)
    message(STATUS "i2cdev: ESP-IDF v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} detected, using legacy driver")
else()
    set(USE_LEGACY_DRIVER FALSE)
    message(STATUS "i2cdev: ESP-IDF v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} detected, using new i2c_master driver")
endif()

# Conditionally set the source file based on version detection or Kconfig override
if(USE_LEGACY_DRIVER)
    set(SRCS "i2cdev_legacy.c")
    message(STATUS "i2cdev: Compiling with legacy I2C driver (i2cdev_legacy.c)")
else()
    set(SRCS "i2cdev.c")
    message(STATUS "i2cdev: Compiling with new I2C master driver (i2cdev.c)")
endif()

# Register the component
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES ${REQUIRED_COMPONENTS})

include(eil_ci)
