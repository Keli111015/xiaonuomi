#include "sunrise_sunset.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_string.h"


#define PI           3.14159265358979323846
#define PI_2         1.57079632679489661923 // π/2
#define TAYLOR_TERMS 10                     // 泰勒展开项数

// 定义全局变量
int days_of_month_1[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
int days_of_month_2[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
// long
double h = -0.833;

// 预计算阶乘表，避免重复计算
static const double factorials[] = {
	1.0,      // 0!
	1.0,      // 1!
	2.0,      // 2!
	6.0,      // 3!
	24.0,     // 4!
	120.0,    // 5!
	720.0,    // 6!
	5040.0,   // 7!
	40320.0,  // 8!
	362880.0, // 9!
	3628800.0 // 10!
};

// 或者使用计算函数
double factorial(int n)
{
	double result = 1.0;
	for (int i = 2; i <= n; i++) {
		result *= i;
	}
	return result;
}

double my_sin(double x)
{
	// 将x归一化到[-π, π]区间
	while (x > PI) {
		x -= 2 * PI;
	}
	while (x < -PI) {
		x += 2 * PI;
	}

	double result = 0.0;
	double x_power = x;
	double sign = 1.0;

	for (int n = 0; n < TAYLOR_TERMS; n++) {
		int term = 2 * n + 1; // 1, 3, 5,...
		result += sign * x_power / factorial(term);

		// 准备下一项
		sign *= -1;
		x_power *= x * x;
	}

	return result;
}

double my_cos(double x)
{
	// 将x归一化到[-π, π]区间
	while (x > PI) {
		x -= 2 * PI;
	}
	while (x < -PI) {
		x += 2 * PI;
	}

	double result = 0.0;
	double x_power = 1.0; // x^0
	double sign = 1.0;

	for (int n = 0; n < TAYLOR_TERMS; n++) {
		int term = 2 * n; // 0, 2, 4,...
		result += sign * x_power / factorial(term);

		// 准备下一项
		sign *= -1;
		x_power *= x * x;
	}

	return result;
}

double my_asin(double x)
{
	// 确保输入在[-1,1]范围内
	if (x < -1.0 || x > 1.0) {
		return NAN;
	}

	double result = x;
	double x_power = x * x * x; // x^3
	double numerator = 1.0;
	double denominator = 2.0;

	for (int n = 1; n < TAYLOR_TERMS; n++) {
		// 系数计算: (2n)!/(4^n (n!)^2 (2n+1))
		double term = numerator * x_power / (denominator * (2 * n + 1));
		result += term;

		// 更新下一项的分子和分母
		numerator *= (2 * n) * (2 * n - 1);
		denominator *= 4 * n * (n + 1);
		x_power *= x * x;
	}

	return result;
}

double my_acos(double x)
{
	return PI_2 - my_asin(x);
}

// 简单的字符串转double实现（不支持科学计数法，仅支持正负号和小数点）
double my_strtod(const char *str, char **endptr)
{
	double result = 0.0;
	int sign = 1;
	int has_dot = 0;
	double frac = 0.1;

	// 跳过空格
	while (*str == ' ' || *str == '\t') {
		str++;
	}

	// 处理符号
	if (*str == '-') {
		sign = -1;
		str++;
	} else if (*str == '+') {
		str++;
	}

	// 主循环
	while ((*str >= '0' && *str <= '9') || *str == '.') {
		if (*str == '.') {
			has_dot = 1;
			str++;
			continue;
		}
		if (!has_dot) {
			result = result * 10 + (*str - '0');
		} else {
			result = result + (*str - '0') * frac;
			frac *= 0.1;
		}
		str++;
	}

	if (endptr) {
		*endptr = (char *)str;
	}
	return sign * result;
}

// 输入日期
void input_date(int *c)
{
	// printf("Enter the date (form: 2009 03 10):\n");
	scanf("%d %d %d", &c[0], &c[1], &c[2]);
}
// 输入纬度
void input_glat(int *c)
{
	// printf("Enter the degree of latitude(range: 0°- 60°,form: 40 40 40 (means
	// 40°40′40″)):\n");
	scanf("%d %d %d", &c[0], &c[1], &c[2]);
}
// 输入经度
void input_glong(int *c)
{
	// printf("Enter the degree of longitude(west is negativ,form: 40 40 40 (means
	// 40°40′40″)):\n");
	scanf("%d %d %d", &c[0], &c[1], &c[2]);
}
// 判断是否为闰年:若为闰年，返回1；若非闰年，返回0
int leap_year(int year)
{
	if ((year % 400 == 0) || ((year % 100 != 0) && (year % 4 == 0))) {
		return 1;
	} else {
		return 0;
	}
}
// 求从格林威治时间公元2000年1月1日到计算日天数days
int days(int year, int month, int date)
{
	int i, a = 0;
	for (i = 2000; i < year; i++) {
		if (leap_year(i)) {
			a = a + 366;
		} else {
			a = a + 365;
		}
	}
	if (leap_year(year)) {
		for (i = 0; i < month - 1; i++) {
			a = a + days_of_month_2[i];
		}
	} else {
		for (i = 0; i < month - 1; i++) {
			a = a + days_of_month_1[i];
		}
	}
	a = a + date;
	return a;
}
// 求格林威治时间公元2000年1月1日到计算日的世纪数t
double t_century(int days, double UTo)
{
	return ((double)days + UTo / 360) / 36525;
}
// 求太阳的平黄径
double L_sun(double t_century)
{
	return (280.460 + 36000.770 * t_century);
}
// 求太阳的平近点角
double G_sun(double t_century)
{
	return (357.528 + 35999.050 * t_century);
}
// 求黄道经度
double ecliptic_longitude(double L_sun, double G_sun)
{
	return (L_sun + 1.915 * my_sin(G_sun * PI / 180) + 0.02 * my_sin(2 * G_sun * PI / 180));
}
// 求地球倾角
double earth_tilt(double t_century)
{
	return (23.4393 - 0.0130 * t_century);
}
// 求太阳偏差
double sun_deviation(double earth_tilt, double ecliptic_longitude)
{
	return (180 / PI *
		my_asin(my_sin(PI / 180 * earth_tilt) * my_sin(PI / 180 * ecliptic_longitude)));
}
// 求格林威治时间的太阳时间角GHA
double GHA(double UTo, double G_sun, double ecliptic_longitude)
{
	return (UTo - 180 - 1.915 * my_sin(G_sun * PI / 180) - 0.02 * my_sin(2 * G_sun * PI / 180) +
		2.466 * my_sin(2 * ecliptic_longitude * PI / 180) -
		0.053 * my_sin(4 * ecliptic_longitude * PI / 180));
}
// 求修正值e
double e(double h, double glat, double sun_deviation)
{
	return 180 / PI *
	       my_acos((my_sin(h * PI / 180) -
			my_sin(glat * PI / 180) * my_sin(sun_deviation * PI / 180)) /
		       (my_cos(glat * PI / 180) * my_cos(sun_deviation * PI / 180)));
}
// 求日出时间
double UT_rise(double UTo, double GHA, double glong, double e)
{
	return (UTo - (GHA + glong + e));
}
// 求日落时间
double UT_set(double UTo, double GHA, double glong, double e)
{
	return (UTo - (GHA + glong - e));
}
// 判断并返回结果（日出）
double result_rise(double UT, double UTo, double glong, double glat, int year, int month, int date)
{
	double d;
	if (UT >= UTo) {
		d = UT - UTo;
	} else {
		d = UTo - UT;
	}
	if (d >= 0.1) {
		UTo = UT;
		UT = UT_rise(
			UTo,
			GHA(UTo, G_sun(t_century(days(year, month, date), UTo)),
			    ecliptic_longitude(L_sun(t_century(days(year, month, date), UTo)),
					       G_sun(t_century(days(year, month, date), UTo)))),
			glong,
			e(h, glat,
			  sun_deviation(earth_tilt(t_century(days(year, month, date), UTo)),
					ecliptic_longitude(
						L_sun(t_century(days(year, month, date), UTo)),
						G_sun(t_century(days(year, month, date), UTo))))));
		result_rise(UT, UTo, glong, glat, year, month, date);
	}
	return UT;
}
// 判断并返回结果（日落）
double result_set(double UT, double UTo, double glong, double glat, int year, int month, int date)
{
	double d;
	if (UT >= UTo) {
		d = UT - UTo;
	} else {
		d = UTo - UT;
	}
	if (d >= 0.1) {
		UTo = UT;
		UT = UT_set(
			UTo,
			GHA(UTo, G_sun(t_century(days(year, month, date), UTo)),
			    ecliptic_longitude(L_sun(t_century(days(year, month, date), UTo)),
					       G_sun(t_century(days(year, month, date), UTo)))),
			glong,
			e(h, glat,
			  sun_deviation(earth_tilt(t_century(days(year, month, date), UTo)),
					ecliptic_longitude(
						L_sun(t_century(days(year, month, date), UTo)),
						G_sun(t_century(days(year, month, date), UTo))))));
		result_set(UT, UTo, glong, glat, year, month, date);
	}
	return UT;
}
// 求时区
int Zone(double glong)
{
	int timeZone;
	int shangValue = (int)(glong / 15);
	double yushuValue = glong - shangValue * 15;
	//    printf("%lf\n",yushuValue);
	if (yushuValue <= 7.5) {
		timeZone = shangValue;
	} else {
		timeZone = shangValue + (glong > 0 ? 1 : -1);
	}
	return timeZone;
}

void output(double rise, double set, double glong, sunrise_set_t *sunrise_set)
{
	if (sunrise_set == NULL) {
		return;
	}
	if ((int)(60 * (rise / 15 + Zone(glong) - (int)(rise / 15 + Zone(glong)))) < 10) {

		sunrise_set->sunrise_hour = (int)(rise / 15 + Zone(glong));
		sunrise_set->sunrise_min =
			(int)(60 * (rise / 15 + Zone(glong) - (int)(rise / 15 + Zone(glong))));
		SERIAL_LOG("The time at which the sun rises is %d:0%d \n",
			   sunrise_set->sunrise_hour, sunrise_set->sunrise_min);
	} else {
		sunrise_set->sunrise_hour = (int)(rise / 15 + Zone(glong));
		sunrise_set->sunrise_min =
			(int)(60 * (rise / 15 + Zone(glong) - (int)(rise / 15 + Zone(glong))));
		SERIAL_LOG("The time at which the sun rises is %d:0%d \n",
			   sunrise_set->sunrise_hour, sunrise_set->sunrise_min);
	}

	if ((int)(60 * (set / 15 + Zone(glong) - (int)(set / 15 + Zone(glong)))) < 10) {
		sunrise_set->sunset_hour = (int)(set / 15 + Zone(glong));
		sunrise_set->sunset_min =
			(int)(60 * (set / 15 + Zone(glong) - (int)(set / 15 + Zone(glong))));
		SERIAL_LOG("The time at which the sun sets is %d:%d \n", sunrise_set->sunset_hour,
			   sunrise_set->sunset_min);
	} else {

		sunrise_set->sunset_hour = (int)(set / 15 + Zone(glong));
		sunrise_set->sunset_min =
			(int)(60 * (set / 15 + Zone(glong) - (int)(set / 15 + Zone(glong))));
		SERIAL_LOG("The time at which the sun sets is %d:%d \n", sunrise_set->sunset_hour,
			   sunrise_set->sunset_min);
	}
}

// input：要解析的字符串
// coord：输出的经纬度
int parse_coordinate(const char *input, Coordinate *coord)
{
	if (input == NULL || coord == NULL) {
		return -QM_EINVAL; // 无效参数
	}

	// 复制输入字符串以避免修改原始字符串
	char *str = qm_strdup(input);
	if (str == NULL) {
		return -QM_EINVAL; // 内存分配失败
	}

	// 查找逗号分隔符
	char *comma = strchr(str, ',');
	if (comma == NULL) {
		qm_free(str);
		return -QM_EINVAL; // 格式错误：缺少逗号
	}

	// 分割字符串为经度和纬度部分
	*comma = '\0';
	char *longitude_str = str;
	char *latitude_str = comma + 1;
	uint32_t longitude_len = qm_strlen(longitude_str);
	uint32_t latitude_len = qm_strlen(latitude_str);

	if (longitude_len == 0 || latitude_len == 0) {
		qm_free(str);
		return -QM_EINVAL;
	}

	// 去除前后空白字符
	char *endptr;
	for (endptr = longitude_str + longitude_len - 1;
	     endptr >= longitude_str && isspace(*endptr); endptr--) {
		*endptr = '\0';
	}
	for (endptr = latitude_str + latitude_len - 1;
	     endptr >= latitude_str && isspace(*endptr); endptr--) {
		*endptr = '\0';
	}

	// 解析经度
	// errno = 0;
	coord->longitude = my_strtod(longitude_str, &endptr);
	if (*endptr != '\0') {
		qm_free(str);
		return -QM_EINVAL; // 经度解析失败
	}

	// 解析纬度
	// errno = 0;
	coord->latitude = my_strtod(latitude_str, &endptr);
	if (*endptr != '\0') {
		qm_free(str);
		return -QM_EINVAL; // 纬度解析失败
	}

	qm_free(str);
	return QM_EOK; // 成功
}

int sunrise_set_get_handle(sunrise_set_t *sunrise_set)
{
	double rise, set;
	double UTo = 180.0;
	Coordinate coord = {0};
	if (sunrise_set == NULL) {
		return (-QM_EINVAL);
	}

	if (qm_strlen((char *)sunrise_set->lat_long_buf) == 0 ||
	    qm_strlen((char *)sunrise_set->lat_long_buf) > LAT_LONG_STR_MAX_LEN) {
		return (-QM_EINVAL);
	}

	if (sunrise_set->data_time.year < MIN_PER_HOUR) {
		return (-QM_EINVAL);
	}

	if (parse_coordinate(sunrise_set->lat_long_buf, &coord) != 0) {
		return (-QM_EINVAL);
	}
	rise = result_rise(
		UT_rise(UTo,
			GHA(UTo,
			    G_sun(t_century(days(sunrise_set->data_time.year,
						 sunrise_set->data_time.month,
						 sunrise_set->data_time.day),
					    UTo)),
			    ecliptic_longitude(L_sun(t_century(days(sunrise_set->data_time.year,
								    sunrise_set->data_time.month,
								    sunrise_set->data_time.day),
							       UTo)),
					       G_sun(t_century(days(sunrise_set->data_time.year,
								    sunrise_set->data_time.month,
								    sunrise_set->data_time.day),
							       UTo)))),
			coord.longitude,
			e(h, coord.latitude,
			  sun_deviation(earth_tilt(t_century(days(sunrise_set->data_time.year,
								  sunrise_set->data_time.month,
								  sunrise_set->data_time.day),
							     UTo)),
					ecliptic_longitude(
						L_sun(t_century(days(sunrise_set->data_time.year,
								     sunrise_set->data_time.month,
								     sunrise_set->data_time.day),
								UTo)),
						G_sun(t_century(days(sunrise_set->data_time.year,
								     sunrise_set->data_time.month,
								     sunrise_set->data_time.day),
								UTo)))))),
		UTo, coord.longitude, coord.latitude, sunrise_set->data_time.year,
		sunrise_set->data_time.month, sunrise_set->data_time.day);
	set = result_set(
		UT_set(UTo,
		       GHA(UTo,
			   G_sun(t_century(days(sunrise_set->data_time.year,
						sunrise_set->data_time.month,
						sunrise_set->data_time.day),
					   UTo)),
			   ecliptic_longitude(L_sun(t_century(days(sunrise_set->data_time.year,
								   sunrise_set->data_time.month,
								   sunrise_set->data_time.day),
							      UTo)),
					      G_sun(t_century(days(sunrise_set->data_time.year,
								   sunrise_set->data_time.month,
								   sunrise_set->data_time.day),
							      UTo)))),
		       coord.longitude,
		       e(h, coord.latitude,
			 sun_deviation(earth_tilt(t_century(days(sunrise_set->data_time.year,
								 sunrise_set->data_time.month,
								 sunrise_set->data_time.day),
							    UTo)),
				       ecliptic_longitude(
					       L_sun(t_century(days(sunrise_set->data_time.year,
								    sunrise_set->data_time.month,
								    sunrise_set->data_time.day),
							       UTo)),
					       G_sun(t_century(days(sunrise_set->data_time.year,
								    sunrise_set->data_time.month,
								    sunrise_set->data_time.day),
							       UTo)))))),
		UTo, coord.longitude, coord.latitude, sunrise_set->data_time.year,
		sunrise_set->data_time.month, sunrise_set->data_time.day);

	output(rise, set, coord.longitude, sunrise_set);
	return QM_EOK;
}

// // 打印结果
// int main()
// {
//     double UTo = 180.0;
//     int year, month, date;
//     double glat, glong;
//     int c[3];
//     //	input_date(c);
//     year = 2025;
//     month = 4;
//     date = 9;
//     printf("year=%d month=%d date=%d\n", year, month, date);
//     //	input_glat(c);
//     //	glat = c[0] + c[1] / 60 + c[2] / 3600;
//     glat = 28.026046;
//     printf("glat=%lf\n", glat);
//     //	input_glong(c);
//     //	glong = c[0] + c[1] / 60 + c[2] / 3600;
//     glong = 113.126078;
//     printf("glong=%lf\n", glong);

//     const char *input = "112.9021878892866,28.221809783876143";
//     Coordinate coord;

//     int result = parse_coordinate(input, &coord);
//     if (result != 0) {
//         printf("解析失败，错误码: %d\n", result);
//         return 1;
//     }

//     printf("经度: %.15f\n", coord.longitude);
//     printf("纬度: %.15f\n", coord.latitude);
//     glat = coord.latitude;
//     glong = coord.longitude;

//     double rise, set;
//     rise = result_rise(UT_rise(UTo, GHA(UTo, G_sun(t_century(days(year, month, date), UTo)),
//     ecliptic_longitude(L_sun(t_century(days(year, month, date), UTo)), G_sun(t_century(days(year,
//     month, date), UTo)))), glong, e(h, glat, sun_deviation(earth_tilt(t_century(days(year, month,
//     date), UTo)), ecliptic_longitude(L_sun(t_century(days(year, month, date), UTo)),
//     G_sun(t_century(days(year, month, date), UTo)))))), UTo, glong, glat, year, month, date); set
//     = result_set(UT_set(UTo, GHA(UTo, G_sun(t_century(days(year, month, date), UTo)),
//     ecliptic_longitude(L_sun(t_century(days(year, month, date), UTo)), G_sun(t_century(days(year,
//     month, date), UTo)))), glong, e(h, glat, sun_deviation(earth_tilt(t_century(days(year, month,
//     date), UTo)), ecliptic_longitude(L_sun(t_century(days(year, month, date), UTo)),
//     G_sun(t_century(days(year, month, date), UTo)))))), UTo, glong, glat, year, month, date);
//     output(rise, set, glong);
//     //	system("pause");

//     return 0;
// }
