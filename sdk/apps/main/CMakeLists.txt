if(DEFINED ENV{ADF_PATH} AND EXISTS "$ENV{ADF_PATH}/qumios-dev")
    set(QDEV "$ENV{ADF_PATH}/qumios-dev")
else()
    get_filename_component(QDEV "${CMAKE_CURRENT_LIST_DIR}/../../qumios-dev" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${QDEV}" QDEV)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../components/agora_iot_sdk" AGORA_SDK)

file(READ "${CMAKE_CURRENT_LIST_DIR}/qm_config/qm_config.h" QM_CONFIG_H)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/qm_config/qm_config.h")
if(QM_CONFIG_H MATCHES "#define[ \t]+CONFIG_VOICE_BACKEND_AGORA[ \t]+1")
    set(VOICE_BACKEND_AGORA 1)
else()
    set(VOICE_BACKEND_AGORA 0)
endif()
message(STATUS "voice backend agora=${VOICE_BACKEND_AGORA}")

if(QM_CONFIG_H MATCHES "#define[ \t]+CONFIG_LJYD_PLAYBACK_SUPPORT[ \t]+1")
    set(LJYD_PLAYBACK_SUPPORT 1)
else()
    set(LJYD_PLAYBACK_SUPPORT 0)
endif()
message(STATUS "ljyd playback support=${LJYD_PLAYBACK_SUPPORT}")

if(QM_CONFIG_H MATCHES "#define[ \t]+CONFIG_QM_ALARM_SUPPORT[ \t]+1")
    set(QM_ALARM_SUPPORT 1)
else()
    set(QM_ALARM_SUPPORT 0)
endif()
message(STATUS "qm alarm support=${QM_ALARM_SUPPORT}")

if(QM_CONFIG_H MATCHES "#define[ \t]+CONFIG_QM_POSE_SUPPORT[ \t]+1")
    set(QM_POSE_SUPPORT 1)
else()
    set(QM_POSE_SUPPORT 0)
endif()
message(STATUS "qm pose support=${QM_POSE_SUPPORT}")

if(QM_CONFIG_H MATCHES "#define[ \t]+CONFIG_APP_CAMERA_TEST[ \t]+1")
    set(APP_CAMERA_TEST 1)
else()
    set(APP_CAMERA_TEST 0)
endif()
message(STATUS "app camera test=${APP_CAMERA_TEST}")

set(COMPONENT_SRCS
    "./main.c"
    "./agent/agent_app.c"
    "./cloud/ljyd_cloud.c"
    "./cloud/ljyd_method.c"
    "./cloud/ljyd_provision.c"
    "./cloud/time_sync.c"
    "./lj_components/iot_control/iot_control.c"
    "./lj_components/iot_control/ctrl/iot_control_volume.c"
    "./lj_components/course/ljyd_course.c"
    "./lj_components/pomodoro/pomodoro.c"
    "${QDEV}/components/qm_pomodoro/src/qm_pomodoro.c"
    "./lj_components/morphcube/mc_study.c"
    "./lj_components/morphcube/morphcube_study_runtime.c"
    "./peripheral/bsp_io.c"
    "./peripheral/bsp_mpu6050.c"
    "./peripheral/bsp_servo.c"
    "./peripheral/bsp_vibrator.c"
    "./peripheral/bsp_ws2812.c"
    "./voice/voice.c"
    "./voice/boot_sound.c"

    "${QDEV}/components/cli/qm_cli.c"
    "${QDEV}/components/ntpdate/src/ntpdate.c"
    "${QDEV}/components/epochtime/src/qm_epochtime.c"
    "${QDEV}/platform/mcu/esp32s3/hal/net/qm_udp.c"
    "${QDEV}/components/qm_agent/src/core/qm_agent_core.c"
    "${QDEV}/components/qm_agent/src/core/qm_agent_event.c"
    "${QDEV}/components/qm_agent/src/core/qm_agent_module.c"
    "${QDEV}/components/qm_agent/src/core/qm_agent_control.c"
    "${QDEV}/components/qm_agent/src/audio/qm_agent_audio_engine.c"
    "${QDEV}/components/qm_agent/src/auth/qm_agent_auth.c"
    "${QDEV}/components/qm_agent/src/transport/qm_agent_transport.c"
    "${QDEV}/components/qm_websocket_client/src/qm_websocket_client.c"
    "${QDEV}/components/qm_websocket_client/src/qm_websocket_transport.c"
    "${QDEV}/components/qm_time/src/qm_time.c"
    "${QDEV}/platform/mcu/esp32s3/hal/soc/qm_rtc.c"
    "${QDEV}/utility/uuid/qm_utils_uuid.c"
    "${QDEV}/utility/sprintf/qm_sprintf.c"

    "${QDEV}/drivers/VB701N/src/vb701n.c"
    "${QDEV}/platform/mcu/esp32s3/qm_audio/qm_audio_device_vb701n.c"
    "${QDEV}/components/qm_audio/src/audio/qm_audio.c"
    "${QDEV}/components/qm_audio/src/capture/qm_audio_capture.c"
    "${QDEV}/components/qm_audio/src/control/qm_audio_control.c"
    "${QDEV}/components/qm_audio/src/engine/qm_audio_engine.c"
    "${QDEV}/components/qm_audio/src/event/qm_audio_event.c"
    "${QDEV}/components/qm_audio/src/pipeline/qm_audio_pipeline.c"
    "${QDEV}/components/qm_audio/src/service/qm_audio_service.c"
    "${QDEV}/components/qm_res_mgr/src/qm_res_mgr.c"
    "${QDEV}/components/qm_event/src/qm_event.c"
    "${QDEV}/components/qm_event/src/qm_work.c"
    "${QDEV}/components/qm_provision/src/qm_provision.c"
    "${QDEV}/components/qm_cloud/api/qm_iot_api.c"
    "${QDEV}/components/qm_cloud/config/qm_iot_config.c"
    "${QDEV}/components/qm_cloud/dev/qm_iot_dev.c"
    "${QDEV}/components/qm_cloud/dynreg/qm_iot_dynreg.c"
    "${QDEV}/components/qm_cloud/mqtt/qm_iot_mqtt.c"
    "${QDEV}/components/qm_cloud/pub/qm_iot_pub.c"
    "${QDEV}/components/qm_cloud/shadow/qm_iot_shadow.c"
    "${QDEV}/components/qm_spec/src/qm_spec_api.c"
    "${QDEV}/components/qm_spec/src/qm_spec_core.c"
    "${QDEV}/components/qm_ble/api/qm_ble_async.c"
    "${QDEV}/components/qm_ble/api/qm_ble_log.c"
    "${QDEV}/components/qm_ble/api/qm_ble_export.c"
    "${QDEV}/components/qm_ble/core/qm_ble_auth.c"
    "${QDEV}/components/qm_ble/core/qm_ble_core.c"
    "${QDEV}/components/qm_ble/core/qm_ble_service.c"
    "${QDEV}/components/qm_ble/core/qm_ble_transport.c"
    "${QDEV}/components/qm_ble/core/qm_ble_utils.c"
    "${QDEV}/components/qm_db/src/qm_kv.c"
    "${QDEV}/components/qm_db/src/qm_db_log.c"
    "${QDEV}/components/wifi_manager/src/qm_wifi_mgnt.c"
    "${QDEV}/components/wifi_manager/src/qm_wifi_dev.c"
    "${QDEV}/components/qm_http_client/src/qm_http_client.c"
    "${QDEV}/components/qm_http_client/src/qm_http_client_transport.c"
    "${QDEV}/components/http/util_httpc.c"
    "${QDEV}/components/network/util_net.c"
    "${QDEV}/components/mqtt/iot_mqtt_client.c"
    "${QDEV}/components/mqtt/MQTTPacket/MQTTConnectClient.c"
    "${QDEV}/components/mqtt/MQTTPacket/MQTTDeserializePublish.c"
    "${QDEV}/components/mqtt/MQTTPacket/MQTTPacket.c"
    "${QDEV}/components/mqtt/MQTTPacket/MQTTSerializePublish.c"
    "${QDEV}/components/mqtt/MQTTPacket/MQTTSubscribeClient.c"
    "${QDEV}/components/mqtt/MQTTPacket/MQTTUnsubscribeClient.c"
    "${QDEV}/components/backoff/src/backoff_algorithm.c"
    "${QDEV}/components/cjson/cJSON.c"
    "${QDEV}/utility/log/qm_log.c"
    "${QDEV}/utility/string/qm_string.c"
    "${QDEV}/utility/string/qm_utils_string.c"
    "${QDEV}/utility/list/qm_utils_list.c"
    "${QDEV}/utility/ringbuf/qm_ringbuf.c"
    "${QDEV}/utility/ringqueue/qm_ring_queue.c"
    "${QDEV}/utility/timer/qm_utils_timer.c"
    "${QDEV}/utility/json_parser/json_parser.c"
    "${QDEV}/utility/hmac/qm_utils_hmac.c"
    "${QDEV}/utility/base64/qm_utils_base64.c"
    "${QDEV}/utility/md5/qm_utils_md5.c"
    "${QDEV}/utility/sha256/qm_utils_sha256.c"
    "${QDEV}/utility/sha1/qm_utils_sha1.c"
    "${QDEV}/utility/aes/qm_utils_aes.c"
    "${QDEV}/components/qm_time/src/qm_time.c"
    "${QDEV}/platform/mcu/esp32s3/qmos/qmos_freertos.c"
    "${QDEV}/platform/mcu/esp32s3/qmos/qm_main.c"
    "${QDEV}/kernel/init/qm_init.c"
    "${QDEV}/platform/mcu/esp32s3/hal/soc/qm_uart.c"
    "${QDEV}/platform/mcu/esp32s3/hal/soc/qm_gpio.c"
    "${QDEV}/platform/mcu/esp32s3/hal/soc/qm_pwm.c"
    "${QDEV}/platform/mcu/esp32s3/hal/wifi/qm_wifi.c"
    "${QDEV}/platform/mcu/esp32s3/hal/net/qm_tcp.c"
    "${QDEV}/platform/mcu/esp32s3/hal/net/qm_tls.c"
    "${QDEV}/platform/mcu/esp32s3/hal/ble/qm_ble_gap.c"
    "${QDEV}/platform/mcu/esp32s3/qm_ble/qm_ble_hal_ble.c"
    "${QDEV}/platform/mcu/esp32s3/qm_ble/qm_ble_hal_os.c"
    "${QDEV}/platform/mcu/esp32s3/qm_ble/qm_ble_hal_ota.c"
    "${QDEV}/platform/mcu/esp32s3/qm_ble/qm_ble_hal_sec.c"
    "${QDEV}/platform/mcu/esp32s3/qm_db/qm_hal_kv.c"
)

if(LJYD_PLAYBACK_SUPPORT)
    list(APPEND COMPONENT_SRCS
        "./lj_components/playback/playback.c"
        "./lj_components/playback/qm_audio_encoder_opus.c"
    )
endif()

if(APP_CAMERA_TEST)
    list(APPEND COMPONENT_SRCS "./peripheral/bsp_camera.c")
endif()

# 闹钟：平台组件和应用层模块一起开关，CONFIG_QM_ALARM_SUPPORT 0 时两者都不编译
if(QM_ALARM_SUPPORT)
    list(APPEND COMPONENT_SRCS
        "./lj_components/alarm/app_alarm.c"
        "${QDEV}/components/qm_alarm/src/qm_alarm.c"
    )
endif()

if(QM_POSE_SUPPORT)
    list(APPEND COMPONENT_SRCS
        "./pose/yolo11_pose.cpp"
        "./pose/example_pose.c"
    )
endif()

list(APPEND COMPONENT_SRCS
    "./agent/private/opus.c"
    "${QDEV}/components/qm_agent/src/transport/qm_agent_transport_agora.c"
    "${QDEV}/components/qm_agent/src/transport/qm_agent_agora_stream.c"
)

set(COMPONENT_ADD_INCLUDEDIRS
    "./qm_config"
    "./lj_components/alarm"
    "./agent"
    "./agent/private"
    "./cloud"
    "./lj_components/iot_control"
    "./lj_components/iot_control/ctrl"
    "./lj_components/course"
    "./lj_components/playback"
    "./lj_components/pomodoro"
    "${QDEV}/components/qm_pomodoro/include"
    "./lj_components/morphcube"
    "./peripheral"
    "./voice"
    "${QDEV}/components/qm_alarm/include"
    "${QDEV}/components/cli"
    "${QDEV}/components/ntpdate/include"
    "${QDEV}/components/epochtime/include"
    "./pose"
    "${QDEV}/components/qm_agent/include"
    "${QDEV}/components/qm_agent/priv_include"
    "${QDEV}/components/qm_agent/src/transport"
    "${AGORA_SDK}/include"
    "${QDEV}/components/qm_websocket_client/include"
    "${QDEV}/components/qm_websocket_client/src"
    "${QDEV}/components/qm_time/include"
    "${QDEV}/utility/uuid"
    "${QDEV}/utility/sprintf"
    "${QDEV}/include"
    "${QDEV}/include/qmos"
    "${QDEV}/include/common"
    "${QDEV}/include/hal/soc"
    "${QDEV}/include/hal/wifi"
    "${QDEV}/include/hal/net"
    "${QDEV}/include/hal/ble"
    "${QDEV}/drivers/VB701N/include"
    "${QDEV}/components/qm_audio/include"
    "${QDEV}/components/qm_audio/priv_include"
    "${QDEV}/components/qm_res_mgr/include"
    "${QDEV}/components/qm_event/include"
    "${QDEV}/components/qm_provision/include"
    "${QDEV}/components/qm_cloud/include"
    "${QDEV}/components/qm_cloud/api"
    "${QDEV}/components/qm_cloud/common"
    "${QDEV}/components/qm_cloud/config"
    "${QDEV}/components/qm_cloud/dev"
    "${QDEV}/components/qm_cloud/dynreg"
    "${QDEV}/components/qm_cloud/mqtt"
    "${QDEV}/components/qm_cloud/pub"
    "${QDEV}/components/qm_cloud/shadow"
    "${QDEV}/components/qm_cloud/priv_include"
    "${QDEV}/components/qm_spec/include"
    "${QDEV}/components/qm_ble"
    "${QDEV}/components/qm_ble/api"
    "${QDEV}/components/qm_ble/core"
    "${QDEV}/components/qm_ble/include"
    "${QDEV}/components/qm_ble/hal/include"
    "${QDEV}/components/qm_db/include"
    "${QDEV}/components/qm_db/hal"
    "${QDEV}/components/wifi_manager/include"
    "${QDEV}/components/qm_http_client/include"
    "${QDEV}/components/http"
    "${QDEV}/components/network"
    "${QDEV}/components/mqtt"
    "${QDEV}/components/mqtt/MQTTPacket"
    "${QDEV}/components/backoff/include"
    "${QDEV}/components/cjson"
    "${QDEV}/utility/string"
    "${QDEV}/utility/list"
    "${QDEV}/utility/ringbuf"
    "${QDEV}/utility/ringqueue"
    "${QDEV}/utility/timer"
    "${QDEV}/utility/json_parser"
    "${QDEV}/utility/hmac"
    "${QDEV}/utility/base64"
    "${QDEV}/utility/md5"
    "${QDEV}/utility/sha256"
    "${QDEV}/utility/sha1"
    "${QDEV}/utility/aes"
    "${QDEV}/components/qm_time/include"
)

if(LJYD_PLAYBACK_SUPPORT)
    list(APPEND COMPONENT_ADD_INCLUDEDIRS "./lj_components/playback")
endif()

set(COMPONENT_REQUIRES
    esp_driver_uart esp_driver_gpio esp_timer driver vfs
    esp_wifi esp_netif esp_event nvs_flash bt mbedtls esp-tls
    esp-idf-lib__mpu6050
    espressif__led_strip
)

if(QM_POSE_SUPPORT)
    list(APPEND COMPONENT_REQUIRES coco_pose esp-dl)
endif()

if(APP_CAMERA_TEST)
    list(APPEND COMPONENT_REQUIRES espressif__esp_video espressif__esp_cam_sensor espressif__esp_new_jpeg)
endif()

if(VOICE_BACKEND_AGORA)
    list(APPEND COMPONENT_REQUIRES pthread 78__esp-opus)
endif()

if(QM_POSE_SUPPORT)
    set(COMPONENT_EMBED_FILES "pose/bus.jpg")
endif()

register_component()

# 开机音效 PCM（16k/单声道/16bit）嵌进固件，生成 _binary_boot_sound_pcm_start/_end
target_add_binary_data(${COMPONENT_LIB} "voice/boot_sound.pcm" BINARY RENAME_TO boot_sound_pcm)
# 扣血音效 PCM（16k/单声道/16bit）嵌进固件，生成 _binary_hurt_sound_pcm_start/_end
target_add_binary_data(${COMPONENT_LIB} "voice/hurt_sound.pcm" BINARY RENAME_TO hurt_sound_pcm)

if(VOICE_BACKEND_AGORA)
    target_link_libraries(${COMPONENT_LIB} PUBLIC
        "${AGORA_SDK}/libs/librtsa.a"
        "${AGORA_SDK}/libs/libahpl.a"
        "${AGORA_SDK}/libs/libagora-cjson.a"
    )
endif()
