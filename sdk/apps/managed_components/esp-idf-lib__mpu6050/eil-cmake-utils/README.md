# eil-cmake-utils

Provides helper `cmake` utilities for `esp-idf-lib`, designed to simplify
cross-target and cross-version library development for the ESP-IDF framework.

<!-- vim-markdown-toc GFM -->

* [Usage](#usage)
* [Testing](#testing)

<!-- vim-markdown-toc -->

---

## Usage

When a source code requires `driver/gpio.h` and not all `esp-idf` versions do
not have `esp_driver_gpio`:

```cmake
# CMakeList.txt in component directory

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/common/cmake)
include(eil_check_idf_features)
eil_check_idf_has_esp_drivers()

# List of common ESP-IDF components required by this component
set(REQUIRED_COMPONENTS freertos esp_idf_lib_helpers)

if(EIL_IDF_HAS_ESP_DRIVERS)
    list(APPEND REQUIRED_COMPONENTS esp_driver_gpio)
else()
    list(APPEND REQUIRED_COMPONENTS driver)
endif()

idf_component_register(
    SRCS onewire.c
    INCLUDE_DIRS .
    REQUIRES ${REQUIRED_COMPONENTS}
)
```

## Testing

The project uses a custom test runner implemented in `tests/runner.cmake`.
This script automatically discovers and executes all tests located in the
`tests/` directory.

To run all unit tests:

```console
cmake -P tests/runner.cmake
-- Running: /home/trombik/github/trombik/esp-idf-lib/eil-cmake-utils/tests/eil_check_idf_features_test.cmake
-- Describe: EIL_IDF_HAS_ESP_DRIVERS
-- when version is 5.2
--   PASSED: when version is 5.2 EIL_IDF_HAS_ESP_DRIVERS is FALSE
-- when version is 5.3
--   PASSED: when version is 5.3 EIL_IDF_HAS_ESP_DRIVERS is TRUE
-- when version is 5.4
--   PASSED: when version is 5.4 EIL_IDF_HAS_ESP_DRIVERS is TRUE
-- when version is 6.0
--   PASSED: when version is 6.0 EIL_IDF_HAS_ESP_DRIVERS is TRUE
```

To run a test:

```console
cmake -P tests/foo_test.cmake
```

With verbose log:

```console
cmake -P tests/foo_test.cmake --log-level=DEBUG
```
