#ifndef _QM_STEP_DELTA_H_
#define _QM_STEP_DELTA_H_

#include "qm.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

// 定义曲线类型
typedef enum {
    STEP_DELTA_CURVE_LINEAR = 0, // 线性 (原版
    STEP_DELTA_CURVE_SQUARE,     // 平方曲线 (适合大多数普通调光，软启动)
    STEP_DELTA_CURVE_CUBIC,      // 立方曲线 (低亮度区域更细腻)
	STEP_DELTA_CURVE_HYBRID,      //混合曲线，平方曲线和直线的混合
	STEP_DELTA_CURVE_S,
	STEP_DELTA_CURVE_SMOOTHERSTEP,
} curve_type_t;

typedef struct {
    int start;
    int end;
    int steps;
    int current_step;
    curve_type_t curve; // 新增：曲线类型
	int32_t error_acc;
} qm_step_delta_t;

int qm_step_delta_init(qm_step_delta_t *dest, int start, int end, int step_cnt, curve_type_t curve);
int qm_step_delta_get_next(qm_step_delta_t *dest);
int qm_step_delta_get_end(qm_step_delta_t *dest);
int qm_step_delta_left_steps(qm_step_delta_t *dest);

#ifdef __cplusplus
}
#endif

#endif
