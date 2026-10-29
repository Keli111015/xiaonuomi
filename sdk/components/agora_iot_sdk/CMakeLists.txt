set(COMPONENT_ADD_INCLUDEDIRS include)

register_component(INCLUDE_DIRS "." "include")

target_link_libraries(${COMPONENT_TARGET} INTERFACE "-L ${CMAKE_CURRENT_SOURCE_DIR}/libs")

add_prebuilt_library(rtsa "${CMAKE_CURRENT_SOURCE_DIR}/libs/librtsa.a" PRIV_REQUIRES lwip pthread)
add_prebuilt_library(ahpl "${CMAKE_CURRENT_SOURCE_DIR}/libs/libahpl.a" PRIV_REQUIRES lwip pthread)
add_prebuilt_library(agora-cjson "${CMAKE_CURRENT_SOURCE_DIR}/libs/libagora-cjson.a")

target_link_libraries(${COMPONENT_TARGET} INTERFACE rtsa ahpl agora-cjson )
