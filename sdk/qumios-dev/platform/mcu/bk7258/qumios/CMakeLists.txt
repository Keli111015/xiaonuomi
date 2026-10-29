# Armino component for QumIOS on BK7258 (CPU0 / ARMINO_SOC=bk7258 only).

set(BK7258_PLAT "${CMAKE_CURRENT_LIST_DIR}/..")
set(QUMIOS_ROOT "${BK7258_PLAT}/../../..")

if ("${ARMINO_SOC}" STREQUAL "bk7258")

set(incs
	"${QUMIOS_ROOT}/../main/qm_config"
	"${BK7258_PLAT}/qm_ble"
	"${BK7258_PLAT}/hal/wifi"
	"${BK7258_PLAT}/hal/net"
	"${BK7258_PLAT}/hal/ble"
	"${BK7258_PLAT}/hal/soc"
	"${QUMIOS_ROOT}/include/qmos"
	"${QUMIOS_ROOT}/include"
	"${QUMIOS_ROOT}/include/common"
	"${QUMIOS_ROOT}/include/hal"
	"${QUMIOS_ROOT}/include/hal/ota"
	"${QUMIOS_ROOT}/include/hal/wifi"
	"${QUMIOS_ROOT}/include/hal/net"
	"${QUMIOS_ROOT}/include/hal/ble"
	"${QUMIOS_ROOT}/include/hal/soc"
	"${QUMIOS_ROOT}/components/qm_db/include"
	"${QUMIOS_ROOT}/components/qm_db/hal"
	"${QUMIOS_ROOT}/components/wifi_manager/include"
	"${QUMIOS_ROOT}/components/qm_event/include"
	"${QUMIOS_ROOT}/components/qm_time/include"
	"${QUMIOS_ROOT}/components/qm_cloud/api"
	"${QUMIOS_ROOT}/components/qm_cloud/include"
	"${QUMIOS_ROOT}/components/qm_cloud/common"
	"${QUMIOS_ROOT}/components/qm_cloud/config"
	"${QUMIOS_ROOT}/components/qm_cloud/priv_include"
	"${QUMIOS_ROOT}/components/cjson"
	"${QUMIOS_ROOT}/components/qm_cloud/dynreg"
	"${QUMIOS_ROOT}/components/qm_cloud/mqtt"
	"${QUMIOS_ROOT}/components/qm_cloud/dev"
	"${QUMIOS_ROOT}/components/qm_cloud/pub"
	"${QUMIOS_ROOT}/components/qm_cloud/shadow"
	"${QUMIOS_ROOT}/components/qm_spec/include"
	"${QUMIOS_ROOT}/components/qm_provision/include"
	"${QUMIOS_ROOT}/components/qm_agent/include"
	"${QUMIOS_ROOT}/components/qm_agent/priv_include"
	"${QUMIOS_ROOT}/components/qm_audio/include"
	"${QUMIOS_ROOT}/components/qm_audio/priv_include"
	"${QUMIOS_ROOT}/components/joyinside_client/include"
	"${QUMIOS_ROOT}/components/joyinside_client/priv_include"
	"${QUMIOS_ROOT}/components/baidu_chat_agent/include"
	"${QUMIOS_ROOT}/components/lgeine_agent/include"
	"${QUMIOS_ROOT}/components/qm_websocket_client/include"
	"${QUMIOS_ROOT}/components/qm_ble/include"
	"${QUMIOS_ROOT}/components/qm_ble/api"
	"${QUMIOS_ROOT}/components/qm_ble/hal/include"
	"${QUMIOS_ROOT}/components/qm_http_client/include"
	"${QUMIOS_ROOT}/components/http"
	"${QUMIOS_ROOT}/components/network"
	"${QUMIOS_ROOT}/components/comtrace/include"
	"${QUMIOS_ROOT}/components/mdns/include"
	"${QUMIOS_ROOT}/components/mqtt"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket"
	"${QUMIOS_ROOT}/components/backoff/include"
	"${QUMIOS_ROOT}/utility/log"
	"${QUMIOS_ROOT}/utility/aes"
	"${QUMIOS_ROOT}/utility/sha256"
	"${QUMIOS_ROOT}/utility/sha1"
	"${QUMIOS_ROOT}/utility/md5"
	"${QUMIOS_ROOT}/utility/hmac"
	"${QUMIOS_ROOT}/utility/base64"
	"${QUMIOS_ROOT}/utility/string"
	"${QUMIOS_ROOT}/utility/ringqueue"
	"${QUMIOS_ROOT}/utility/sprintf"
	"${QUMIOS_ROOT}/utility/timer"
	"${QUMIOS_ROOT}/utility/list"
	"${QUMIOS_ROOT}/utility/json_parser"
	"${QUMIOS_ROOT}/components/tinycbor/include"
	"${QUMIOS_ROOT}/utility/uuid"
	"${QUMIOS_ROOT}/kernel/init"
)

set(srcs
	# platform
	"${BK7258_PLAT}/qmos/qmos_bk.c"
	"${BK7258_PLAT}/qm_db/qm_hal_kv.c"
	"${BK7258_PLAT}/hal/wifi/qm_wifi.c"
	"${BK7258_PLAT}/hal/net/qm_tcp.c"
	"${BK7258_PLAT}/hal/net/qm_tls.c"
	"${BK7258_PLAT}/hal/net/qm_udp.c"
	"${BK7258_PLAT}/hal/ble/qm_ble_gap.c"
	"${BK7258_PLAT}/hal/soc/qm_rtc.c"
	"${BK7258_PLAT}/qm_ble/qm_ble_hal_ble.c"
	"${BK7258_PLAT}/qm_ble/qm_ble_hal_os.c"
	"${BK7258_PLAT}/qm_ble/qm_ble_hal_sec.c"
	"${BK7258_PLAT}/qm_ble/qm_ble_hal_ota.c"

	# kernel
	"${QUMIOS_ROOT}/kernel/init/qm_init.c"

	# utility
	"${QUMIOS_ROOT}/utility/log/qm_log.c"
	"${QUMIOS_ROOT}/utility/type/qm_types.c"
	"${QUMIOS_ROOT}/utility/aes/qm_utils_aes.c"
	"${QUMIOS_ROOT}/utility/sha256/qm_utils_sha256.c"
	"${QUMIOS_ROOT}/utility/sha1/qm_utils_sha1.c"
	"${QUMIOS_ROOT}/utility/md5/qm_utils_md5.c"
	"${QUMIOS_ROOT}/utility/hmac/qm_utils_hmac.c"
	"${QUMIOS_ROOT}/utility/base64/qm_utils_base64.c"
	"${QUMIOS_ROOT}/components/network/util_net.c"
	"${QUMIOS_ROOT}/utility/string/qm_string.c"
	"${QUMIOS_ROOT}/utility/ringqueue/qm_ring_queue.c"
	"${QUMIOS_ROOT}/utility/string/qm_utils_string.c"
	"${QUMIOS_ROOT}/utility/timer/qm_utils_timer.c"
	"${QUMIOS_ROOT}/utility/list/qm_utils_list.c"
	"${QUMIOS_ROOT}/utility/json_parser/json_parser.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborencoder.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborencoder_close_container_checked.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborencoder_float.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborerrorstrings.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborparser.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborparser_dup_string.c"
	"${QUMIOS_ROOT}/components/tinycbor/src/cborparser_float.c"
	"${QUMIOS_ROOT}/utility/uuid/qm_utils_uuid.c"
	"${QUMIOS_ROOT}/utility/sprintf/qm_sprintf.c"

	# kv / wifi / event / time
	"${QUMIOS_ROOT}/components/qm_db/src/qm_kv.c"
	"${QUMIOS_ROOT}/components/qm_db/src/qm_db_log.c"
	"${QUMIOS_ROOT}/components/wifi_manager/src/qm_wifi_dev.c"
	"${QUMIOS_ROOT}/components/wifi_manager/src/qm_wifi_mgnt.c"
	"${QUMIOS_ROOT}/components/qm_event/src/qm_event.c"
	"${QUMIOS_ROOT}/components/qm_event/src/qm_work.c"
	"${QUMIOS_ROOT}/components/qm_time/src/qm_time.c"

	# ble + provision
	"${QUMIOS_ROOT}/components/qm_ble/core/qm_ble_core.c"
	"${QUMIOS_ROOT}/components/qm_ble/core/qm_ble_service.c"
	"${QUMIOS_ROOT}/components/qm_ble/core/qm_ble_transport.c"
	"${QUMIOS_ROOT}/components/qm_ble/core/qm_ble_auth.c"
	"${QUMIOS_ROOT}/components/qm_ble/core/qm_ble_utils.c"
	"${QUMIOS_ROOT}/components/qm_ble/api/qm_ble_export.c"
	"${QUMIOS_ROOT}/components/qm_ble/api/qm_ble_async.c"
	"${QUMIOS_ROOT}/components/qm_ble/api/qm_ble_log.c"
	"${QUMIOS_ROOT}/components/qm_provision/src/qm_provision.c"

	# http / mqtt / backoff
	"${QUMIOS_ROOT}/components/qm_http_client/src/qm_http_client.c"
	"${QUMIOS_ROOT}/components/qm_http_client/src/qm_http_client_transport.c"
	"${QUMIOS_ROOT}/components/http/util_httpc.c"
	"${QUMIOS_ROOT}/components/mqtt/iot_mqtt_client.c"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket/MQTTConnectClient.c"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket/MQTTDeserializePublish.c"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket/MQTTPacket.c"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket/MQTTSerializePublish.c"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket/MQTTSubscribeClient.c"
	"${QUMIOS_ROOT}/components/mqtt/MQTTPacket/MQTTUnsubscribeClient.c"
	"${QUMIOS_ROOT}/components/backoff/src/backoff_algorithm.c"

	# cjson + cloud + spec
	"${QUMIOS_ROOT}/components/cjson/cJSON.c"
	"${QUMIOS_ROOT}/components/qm_cloud/api/qm_iot_api.c"
	"${QUMIOS_ROOT}/components/qm_cloud/config/qm_iot_config.c"
	"${QUMIOS_ROOT}/components/qm_cloud/mqtt/qm_iot_mqtt.c"
	"${QUMIOS_ROOT}/components/qm_cloud/dynreg/qm_iot_dynreg.c"
	"${QUMIOS_ROOT}/components/qm_cloud/dev/qm_iot_dev.c"
	"${QUMIOS_ROOT}/components/qm_cloud/pub/qm_iot_pub.c"
	"${QUMIOS_ROOT}/components/qm_cloud/shadow/qm_iot_shadow.c"
	"${QUMIOS_ROOT}/components/qm_spec/src/qm_spec_api.c"
	"${QUMIOS_ROOT}/components/qm_spec/src/qm_spec_core.c"

	# websocket + joyinside + audio + qm_agent
	"${QUMIOS_ROOT}/components/qm_websocket_client/src/qm_websocket_client.c"
	"${QUMIOS_ROOT}/components/qm_websocket_client/src/qm_websocket_transport.c"
	"${QUMIOS_ROOT}/components/joyinside_client/src/joyinside_client.c"
	"${QUMIOS_ROOT}/components/joyinside_client/src/joyinside_client_auth.c"
	"${QUMIOS_ROOT}/components/joyinside_client/src/joyinside_client_vision.c"
	"${QUMIOS_ROOT}/components/joyinside_client/src/joyinside_client_audiobook.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/auth/qm_agent_auth.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/core/qm_agent_control.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/core/qm_agent_core.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/core/qm_agent_event.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/core/qm_agent_module.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/audio/qm_agent_audio_engine.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/transport/qm_agent_transport.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/transport/qm_agent_transport_joyinside.c"
	"${QUMIOS_ROOT}/components/qm_agent/src/transport/qm_agent_transport_lgeine.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/audio/qm_audio.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/engine/qm_audio_engine.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/capture/qm_audio_capture.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/control/qm_audio_control.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/event/qm_audio_event.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/service/qm_audio_service.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/pipeline/qm_audio_pipeline.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/cloud/qm_audio_cloud.c"
	"${QUMIOS_ROOT}/components/qm_audio/src/cloud/qm_audio_cloud_protocol.c"
)

if(CONFIG_LGEINE_AGENT_SUPPORT)
	list(APPEND srcs "${QUMIOS_ROOT}/components/lgeine_agent/src/lgeine_agent.c")
endif()

if(CONFIG_QM_COMTRACE_SUPPORT)
	list(APPEND srcs "${QUMIOS_ROOT}/components/comtrace/src/comtrace_frame.c")
	if(CONFIG_QM_COMTRACE_LOG_SUPPORT)
		list(APPEND srcs "${QUMIOS_ROOT}/components/comtrace/src/comtrace_log.c")
	endif()
	if(CONFIG_QM_COMTRACE_OTA_SUPPORT)
		list(APPEND srcs "${QUMIOS_ROOT}/components/comtrace/src/comtrace_ota.c")
	endif()
	if(CONFIG_QM_COMTRACE_DISCOVERY_SUPPORT)
		list(APPEND srcs "${QUMIOS_ROOT}/components/comtrace/src/comtrace_discovery.c")
	endif()
endif()

if(CONFIG_MDNS_SUPPORT)
	list(APPEND srcs "${QUMIOS_ROOT}/components/mdns/src/mdns.c")
endif()

set(priv_requires
	bk_rtos
	lwip_intf_v2_1
	mbedtls
	bk_wifi
	bk_netif
	bk_event
	easy_flash
	bk_bluetooth
	bt
	bk_common
)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES ${priv_requires})

endif()
