#ifndef _SUNRISE_SUNSET_H_
#define _SUNRISE_SUNSET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

#include "qm_log.h"


#define KV_LAT_LONG_KEY  ("lat_long")

#define LAT_LONG_STR_MAX_LEN (64U)
#define SUNRISE_STR_MAX_LEN (20U)

#define MIN_PER_HOUR (2025)

typedef struct {
    double longitude;
    double latitude;
} Coordinate;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
}user_data_time_t;

typedef struct {
    user_data_time_t data_time;
    uint8_t sunrise_hour;
    uint8_t sunrise_min;
    uint8_t sunset_hour;
    uint8_t sunset_min;
    double glat;    //纬度
    double glong;   //经度
    uint8_t sunrise_str[SUNRISE_STR_MAX_LEN];
    uint8_t lat_long_buf[LAT_LONG_STR_MAX_LEN];
} sunrise_set_t;

int sunrise_set_get_handle(sunrise_set_t *sunrise_set);

#ifdef __cplusplus
}
#endif  

#endif /* GENERIC_SERIAL_H */











#ifdef __cplusplus
}
#endif

#endif
