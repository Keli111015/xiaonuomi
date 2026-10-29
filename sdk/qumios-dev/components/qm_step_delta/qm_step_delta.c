#include "qm.h"
#include "qm_step_delta.h"

#define Q15_BASE  32768
#define Q15_SHIFT 15
#define Q14_BASE  16384
#define Q14_SHIFT 14

int qm_step_delta_init(qm_step_delta_t *dest, int start, int end, int step_cnt, curve_type_t curve)
{
    if (dest)
    {
        dest->start = start;
        dest->end = end;
        // 保护：防止除以0
        if (step_cnt <= 0)
        {
            step_cnt = 1;
        }
        dest->steps = step_cnt;
        dest->current_step = 1; // 从第1步开始
		dest->error_acc = 0;
		if (start == end)
		{
			// 起始值和结束值相同，直接设置为完成状态
			dest->current_step = step_cnt; 
		}
        dest->curve = curve;
        return 0;
    }
    return -1;
}
int qm_step_delta_get_next(qm_step_delta_t *dest)
{
    if (!dest) return 0;
    
    // 【防线1】: 严格的结束判定
    // 只要达到或超过总步数，立刻锁定终点，跳过所有计算
    // 防止 Dithering 在最后时刻把 0 变成 65535 (-1)
    if (dest->current_step >= dest->steps) {
        return dest->end;
    }

    // 2. 计算进度 t (Q14)
    // 乘法安全: steps < 260,000 时不会溢出 uint32
    uint32_t t = (uint32_t)dest->current_step * Q14_BASE / dest->steps;

    // 3. 曲线计算 (Q14 无损版)
    uint32_t curve_q14 = 0;
    
    switch (dest->curve)
    {
    case STEP_DELTA_CURVE_SMOOTHERSTEP: 
        // t^3 * (6t^2 - 15t + 10)
        {
            uint32_t term1 = 6 * t * t;                 // Q28
            uint32_t term2 = (15 * t) << Q14_SHIFT;     // Q28
            uint32_t term3 = 10u << 28;                 // Q28 (10 * 2^28)
            
            uint32_t poly_q28 = term1 + term3;
            if (poly_q28 > term2) poly_q28 -= term2; else poly_q28 = 0;
            
            uint32_t poly_q14 = poly_q28 >> Q14_SHIFT;  // Q14

            uint32_t t2_q14 = (t * t) >> Q14_SHIFT;     // Q14
            uint32_t t3_q14 = (t2_q14 * t) >> Q14_SHIFT;// Q14
            
            curve_q14 = (t3_q14 * poly_q14) >> Q14_SHIFT;
        }
        break;

    case STEP_DELTA_CURVE_SQUARE: 
        curve_q14 = (t * t) >> Q14_SHIFT;
        break;

    case STEP_DELTA_CURVE_CUBIC: 
        {
            uint32_t t2 = (t * t) >> Q14_SHIFT;
            curve_q14 = (t2 * t) >> Q14_SHIFT;
        }
        break;

    case STEP_DELTA_CURVE_LINEAR:
    default:
        curve_q14 = t;
        break;
    }

    // 4. 映射 (TotalDiff * Curve)
    int32_t total_diff = dest->end - dest->start; 
    int32_t delta_q14 = total_diff * (int32_t)curve_q14;
    
    // 5. Dithering
    int32_t integer_part = delta_q14 >> Q14_SHIFT;
    
    // 【防线2】: 终局前的静默
    // 如果是最后几步(比如最后 1%)，禁止 Dithering
    // 防止在目标附近震荡。32768/100 ≈ 327
    // if (dest->steps - dest->current_step < 2) 也可以
    
    if (dest->steps - dest->current_step > 1) {
        int32_t remainder = delta_q14 % Q14_BASE; 
        dest->error_acc += remainder;
        
        if (dest->error_acc >= Q14_BASE) {
            integer_part += 1;
            dest->error_acc -= Q14_BASE;
        } else if (dest->error_acc <= -Q14_BASE) {
            integer_part -= 1;
            dest->error_acc += Q14_BASE;
        }
    } else {
        // 最后一步，强制清空误差，不让它影响输出
        dest->error_acc = 0;
    }

    dest->current_step++;
    
    // 6. 输出
    int final_val = dest->start + integer_part;
    
    // 7. 钳位
    if (final_val > 65535) final_val = 65535;
    if (final_val < 0) final_val = 0;
    return final_val;
}// {
// 	if (!dest) return 0;
//
//     // 结束保护
//     if (dest->current_step >= dest->steps) {
//         return dest->end;
//     }
//
//     // 使用 64 位计算
//     unsigned long long total_diff;
//     unsigned long long base_val;
//     unsigned long long t; 
//     unsigned long long total = (unsigned long long)dest->steps;
//
//     unsigned long long num = 0;
//     unsigned long long den = 1;
//
//     // --- 1. 镜像逻辑 ---
//     // 统一转换为“从0向上增长”的模型，如果是减法，最后再反向处理
//     // 这样保证 Start->End 和 End->Start 的曲线形状完全对称
//     if (dest->end > dest->start) {
//         // 变亮 (Start -> End)
//         total_diff = (unsigned long long)(dest->end - dest->start);
//         base_val = (unsigned long long)dest->start;
//         t = (unsigned long long)dest->current_step;
//     } else {
//         // 变暗 (Start -> End)
//         // 关键：t 从 total 减小到 0，依然利用 SmootherStep 的下凹特性
//         total_diff = (unsigned long long)(dest->start - dest->end);
//         base_val = (unsigned long long)dest->end;
//         t = total - (unsigned long long)dest->current_step;
//     }
//
//     // --- 2. 曲线计算 (回归本真版) ---
//     // 去掉了中间除法，确保所有微小的余数都能进入 error_acc
//     switch (dest->curve)
//     {
//     case STEP_DELTA_CURVE_SMOOTHERSTEP: 
// 	{
//         // 公式: Diff * [ t^3 * (6t^2 - 15t*T + 10T^2) ] / T^5
// 		// 阈值判断：uint64 上限约为 1.84e19
//         // 溢出边界：steps 约为 780 步。为了安全，我们设阈值为 700。
//         if (dest->steps < 700) 
//         {
//             // [模式 A：高精模式] 适合短时调光 (呼吸灯、UI动画)
//             // 公式：Diff * t^3 * Inner
//             // 优点：余数保留完整，低亮度 Dithering 极佳
//             unsigned long long t3 = t * t * t;
//             unsigned long long inner = (6 * t * t) + (10 * total * total) - (15 * t * total);
//
//             num = total_diff * t3 * inner;
//             den = total * total * total * total * total;
//         }
//         else 
//         {
//             // [模式 B：安全模式] 适合超长调光 (氛围灯慢变、日出日落模拟)
//             // 公式：(Diff * t^3 / T^2) * Inner
//             // 优点：永不溢出 (支持 steps 高达 20000+)
//
//             unsigned long long t3 = t * t * t;
//             unsigned long long t2 = total * total;
//             unsigned long long inner = (6 * t * t) + (10 * t2) - (15 * t * total);
//
//             // 核心保护：先除以 T^2 给数据“瘦身”
//             // 加 t2/2 是为了四舍五入，减少精度损失
//             unsigned long long mid_val = (total_diff * t3 + (t2 / 2)) / t2;
//
//             num = mid_val * inner;
//             den = total * total * total; // 分母降阶为 T^3
//         }
//         break;
// 	}
//
//     case STEP_DELTA_CURVE_S: 
//         // 公式: Diff * [ t^2 * (3T - 2t) ] / T^3
//         {
//              unsigned long long inner = (3 * total) - (2 * t);
//              num = total_diff * t * t * inner;
//              den = total * total * total;
//         }
//         break;
//
//     case STEP_DELTA_CURVE_HYBRID: 
//         // (Linear + Square) / 2
//         {
//             unsigned long long p = (t * total) + (t * t);
//             num = total_diff * p;
//             den = 2 * total * total;
//         }
//         break;
//
//     case STEP_DELTA_CURVE_SQUARE: 
//         num = total_diff * t * t;
//         den = total * total;
//         break;
//
//     case STEP_DELTA_CURVE_CUBIC: 
//         num = total_diff * t * t * t;
//         den = total * total * total;
//         break;
//
//     case STEP_DELTA_CURVE_LINEAR:
//     default:
//         num = total_diff * t;
//         den = total;
//         break;
//     }
//
//     dest->current_step++;
//
//     // --- 3. 误差扩散 (Dithering) ---
//     // 只有在上一步没有丢失精度的情况下，这里的 remainder 才有意义
//     unsigned long long integer_part = num / den;
//     unsigned long long remainder = num % den;
//
//     dest->error_acc += remainder;
//
//     // 误差满 1.0 (即分母) 则进位
//     if (dest->error_acc >= den) {
//         integer_part += 1;
//         dest->error_acc -= den;
//     }
//
//     // --- 4. 输出 ---
//     // 始终基于 base_val 叠加
//     return (int)(base_val + integer_part);
// }

int qm_step_delta_get_end(qm_step_delta_t *dest)
{
    if (dest)
    {
        return dest->end;
    }
    return -1;
}

int qm_step_delta_left_steps(qm_step_delta_t *dest)
{
    if (dest)
    {
        if (dest->current_step >= dest->steps)
        {
            return 0;
        }
        return dest->steps - dest->current_step;
    }
    return -1;
}
