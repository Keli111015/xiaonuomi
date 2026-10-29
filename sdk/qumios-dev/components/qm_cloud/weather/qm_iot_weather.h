/**
 * @file qm_iot_weather.h
 * @brief weather模块头文件, 提供wifi设备获取当地天气的能力
 *
 * @copyright Copyright (C) 2024 Wells. All rights reserved.
 *
 */
#ifndef __QM_IOT_WEATHER_H__
#define __QM_IOT_WEATHER_H__

#include "qm_types.h"


#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief 天气类型
 */

 typedef enum {

    QM_IOT_WEATHER_TYPE_SUNNY_DAY = 0,              //晴-白天
    QM_IOT_WEATHER_TYPE_CLOUDY_DAY = 1,             //多云-白天
    QM_IOT_WEATHER_TYPE_OVERCAST = 2,               //阴
    QM_IOT_WEATHER_TYPE_SHOWERS_DAY = 3,            //陈雨-白天
    QM_IOT_WEATHER_TYPE_THUNDERSHOWER = 4,          //雷阵雨
    QM_IOT_WEATHER_TYPE_THUNDERSHOWER_WITH_HAIL = 5,//雷阵雨伴有冰雹
    QM_IOT_WEATHER_TYPE_SLEET = 6,                  //雨夹雪     
    QM_IOT_WEATHER_TYPE_LIGHT_RAIN = 7,             //小雨 
    QM_IOT_WEATHER_TYPE_MODERATE_RAIN = 8,          //中雨 
    QM_IOT_WEATHER_TYPE_HEAVY_RAIN = 9,             //大雨 
    QM_IOT_WEATHER_TYPE_RAINSTORM = 10,             //暴雨
    QM_IOT_WEATHER_TYPE_SNOW_SHOWERS_DAY = 13,      //陈雪-白天
    QM_IOT_WEATHER_TYPE_LIGHT_SNOW = 14,            //小雪
    QM_IOT_WEATHER_TYPE_MODERATE_SNOW = 15,         //中雪
    QM_IOT_WEATHER_TYPE_HEAVY_SNOW = 16,            //大雪
    QM_IOT_WEATHER_TYPE_BLIZZARD = 17,              //暴雪
    QM_IOT_WEATHER_TYPE_FOG_DAY = 18,               //雾-白天
    QM_IOT_WEATHER_TYPE_FREEZING_RAIN = 19,         //冻雨
    QM_IOT_WEATHER_TYPE_SANDSTORM_DAY = 20,         //沙尘暴-白天
    QM_IOT_WEATHER_TYPE_DUST_DAY = 29,              //浮尘-白天
    QM_IOT_WEATHER_TYPE_SUNNY_NIGHT = 30,           //晴-夜晚
    QM_IOT_WEATHER_TYPE_CLOUDY_NIGHT = 31,          //多云-夜晚
    QM_IOT_WEATHER_TYPE_FOG_NIGHT = 32,             //雾-夜晚
    QM_IOT_WEATHER_TYPE_SHOWERS_NIGHT = 33,         //陈雨-夜晚
    QM_IOT_WEATHER_TYPE_SNOW_SHOWERS_NIGHT = 34,    //陈雪-夜晚
    QM_IOT_WEATHER_TYPE_DUST_NIGHT = 35,            //浮尘-夜晚
    QM_IOT_WEATHER_TYPE_SANDSTORM_NIGHT = 36,       //沙尘暴-夜晚
    QM_IOT_WEATHER_TYPE_HAZE_DAY = 45,              //霾-白天
    QM_IOT_WEATHER_TYPE_HAZE_NIGHT = 46             //霾-夜晚

 } qm_iot_weather_type_t;
 

/**
 * @brief weather模块收到从网络上来的报文时, 通知用户的报文内容
 */

typedef struct 
{
    uint16_t msg_id;                         //请求消息ID；云端响应带ID时用于关联请求
    qm_iot_weather_type_t weather_type;     //天气类型
    int temp;                               //温度
    int humidity;                           //湿度
    int city_id;                            //城市ID
    
    char *condition;                        //天气状态
    int condition_len;

    char *country_name;                     //国家
    int country_name_len;                     //国家

    char *pname;                            //省份
    int pname_len;

    int secondaryname_len;
    char *secondaryname;                    //次级名称

    int name_len;
    char *name;                             //具体区域

    int sunset_len;
    char *sunset;                           //日落时间

    int sunrise_len;
    char *sunrise;                          //日升时间
} qm_iot_weather_recv_t;

/**
 * @brief weather模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle weather会话句柄
 * @param[in] packet weather消息结构体, 存放收到的weather报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (* qm_iot_weather_recv_handler_t)(void *handle, const qm_iot_weather_recv_t *packet, void *userdata);

/**
 * @brief @ref qm_iot_weather_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_weather_setopt 中, data参数的数据类型
 *
 * 1. data的数据类型是void *时, 以配置@ref QM_IOT_WEATHEROPT_MQTT_HANDLE 为例:
 *
 *    void *iot_handle = qm_iot_init();
 *    void *mqtt_handle = qm_iot_get_mqtt_handle(iot_handle);
 *    qm_iot_weather_setopt(weather_handle, QM_IOT_WEATHEROPT_USERDATA, iot_handle);
 *    qm_iot_weather_setopt(weather_handle, QM_IOT_WEATHEROPT_MQTT_HANDLE, mqtt_handle);
 */
typedef enum {
    /**
     * @brief weather会话需要的MQTT句柄, 用于通过 device/up 发送天气请求
     *
     * @details
     *
     * 数据类型: (void *)
     */
    QM_IOT_WEATHEROPT_MQTT_HANDLE,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户
     * 
     * @details
     * 
     * 数据类型: ( @ref qm_iot_weather_recv_handler_t )
     */
    QM_IOT_WEATHEROPT_RECV_HANDLER,

    /**
     * @brief 用户需要SDK暂存的上下文
     *
     * @details 该上下文通常传入 @ref qm_iot_init 返回的IOT会话句柄, weather模块会通过它在
     * device/down 上注册 `weatherReply` 回调, 并在 @ref QM_IOT_WEATHEROPT_RECV_HANDLER 中原样回传给用户
     * 
     * 数据类型: (void *)
     */
    QM_IOT_WEATHEROPT_USERDATA,

    /**
     * @brief 销毁ntp实例时, 等待其他api执行完毕的时间
     *
     * @details
     *
     * 当调用@ref qm_iot_weather_deinit 销毁weather实例时, 若继续调用其他qm_iot_weather_xxx API, API会返回@ref -QM_ERROR 错误
     *
     * 此时, 用户应该停止调用其他qm_iot_weather_xxx API
     *
     * 数据类型: (uint32_t *) 默认值: (2 * 1000) ms
     */
    QM_IOT_WEATHEROPT_DEINIT_TIMEOUT_MS,
    QM_IOT_WEATHEROPT_MAX
} qm_iot_weather_option_t;

/**
 * @brief 创建weather会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL weather实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_weather_init(void);

/**
 * @brief 配置weather会话
 *
 * @details
 *
 * 常见的配置项如下
 *
 * + `QM_IOT_WEATHEROPT_MQTT_HANDLE`: 已建立连接的MQTT会话句柄, weather将通过 `device/up` 发送请求
 *
 * + `QM_IOT_WEATHEROPT_USERDATA`: IOT会话句柄/用户上下文, weather将通过它在 `device/down` 接收 `weatherReply`
 *
 * + `QM_IOT_WEATHEROPT_RECV_HANDLER`: 天气数据接收回调函数, SDK将天气信息转换完成后, 通过此回调函数输出
 *
 * @param[in] handle weather会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_weather_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_weather_option_t
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_weather_setopt(void *handle, qm_iot_weather_option_t option, void *data);

/**
 * @brief 结束weather会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向weather会话句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_weather_deinit(void **handle);

/**
 * @brief 发送天气请求
 *
 * @details
 *
 * 发送天气请求。当前实现通过 `device/up` 上报 `weather`, 并通过 `device/down` 接收 `weatherReply`。
 * 然后SDK会调用通过 @ref qm_iot_weather_setopt 配置的 @ref QM_IOT_WEATHEROPT_RECV_HANDLER 回调函数, 通知设备当地的天气信息
 *
 * @param handle weather会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_weather_request(void *handle);

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_WEATHER_H__ */
