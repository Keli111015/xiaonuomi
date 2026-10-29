idf_component_register(
    INCLUDE_DIRS "." "include"
    REQUIRES lwip pthread
)

add_prebuilt_library(agora_rtsa "${CMAKE_CURRENT_SOURCE_DIR}/libs/librtsa.a"
                     REQUIRES ${COMPONENT_NAME})
add_prebuilt_library(agora_ahpl "${CMAKE_CURRENT_SOURCE_DIR}/libs/libahpl.a"
                     REQUIRES ${COMPONENT_NAME})
add_prebuilt_library(agora_cjson "${CMAKE_CURRENT_SOURCE_DIR}/libs/libagora-cjson.a"
                     REQUIRES ${COMPONENT_NAME})

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--start-group"
    agora_rtsa
    agora_ahpl
    agora_cjson
    "-Wl,--end-group"
)
