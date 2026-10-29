#!/usr/bin/env python3
"""Offline calibration helper for qm_battery.

The script intentionally uses only Python standard library modules so it can
run on lab and production PCs without installing extra packages.
"""

import argparse
import csv
import math
import statistics
from pathlib import Path


DEFAULT_SOC_POINTS = (0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100)


def read_csv_rows(path):
    with open(path, newline="", encoding="utf-8-sig") as fp:
        return list(csv.DictReader(fp))


def parse_float(row, name, default=None):
    value = row.get(name, "")
    if value is None or str(value).strip() == "":
        if default is None:
            raise ValueError("missing column value: %s" % name)
        return default
    return float(value)


def parse_optional_bool(row, name):
    value = row.get(name)
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    if text in ("1", "true", "yes", "y", "on", "charging", "charge"):
        return True
    if text in ("0", "false", "no", "n", "off", "discharging", "discharge"):
        return False
    raise ValueError("invalid boolean column value: %s=%s" % (name, value))


def linear_regression(xs, ys):
    if len(xs) != len(ys) or len(xs) < 2:
        raise ValueError("linear regression requires at least two samples")

    mean_x = statistics.fmean(xs)
    mean_y = statistics.fmean(ys)
    den = sum((x - mean_x) * (x - mean_x) for x in xs)
    if abs(den) < 1e-12:
        raise ValueError("linear regression input has no x variation")

    slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / den
    offset = mean_y - slope * mean_x
    return slope, offset


def interpolate(points, x):
    if not points:
        raise ValueError("empty interpolation table")

    points = sorted(points)
    if x <= points[0][0]:
        return points[0][1]
    if x >= points[-1][0]:
        return points[-1][1]

    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        if x0 <= x <= x1:
            if abs(x1 - x0) < 1e-6:
                return y0
            ratio = (x - x0) / (x1 - x0)
            return y0 + (y1 - y0) * ratio

    return points[-1][1]


def parse_soc_points(text):
    if not text:
        return DEFAULT_SOC_POINTS
    return tuple(float(item.strip()) for item in text.split(",") if item.strip())


def build_ocv_table(path, soc_points):
    if not path:
        return None

    rows = read_csv_rows(path)
    raw = []
    for row in rows:
        raw.append((parse_float(row, "soc"), parse_float(row, "voltage")))
    raw = sorted(raw)

    table = [(soc, interpolate(raw, soc)) for soc in soc_points]
    full_anchor = table[-1][1]
    empty_anchor = table[0][1]
    return {
        "table": table,
        "full_anchor_v": full_anchor,
        "empty_anchor_v": empty_anchor,
    }


def fit_adc(path, divider_ratio):
    if not path:
        return None

    rows = read_csv_rows(path)
    raw = []
    actual_adc_node = []
    for row in rows:
        raw_value = parse_float(row, "raw")
        battery_v = parse_float(row, "voltage")
        raw.append(raw_value)
        actual_adc_node.append(battery_v / divider_ratio)

    scale, offset = linear_regression(raw, actual_adc_node)
    errors = [
        ((r * scale + offset) * divider_ratio) - v
        for r, v in zip(raw, [parse_float(row, "voltage") for row in rows])
    ]
    max_abs_error = max(abs(err) for err in errors)
    rms_error = math.sqrt(statistics.fmean(err * err for err in errors))

    return {
        "scale": scale,
        "offset": offset,
        "divider_ratio": divider_ratio,
        "max_abs_error": max_abs_error,
        "rms_error": rms_error,
    }


def fit_resistance(path):
    if not path:
        return None

    rows = read_csv_rows(path)
    values = []
    for row in rows:
        v_before = parse_float(row, "v_before")
        v_after = parse_float(row, "v_after")
        i_before = parse_float(row, "i_before", 0.0)
        i_after = parse_float(row, "i_after")
        current_step = abs(i_after - i_before)
        if current_step < 1e-6:
            continue
        values.append(abs(v_before - v_after) / current_step)

    if not values:
        raise ValueError("resistance CSV has no valid current step")

    return {
        "nominal": statistics.fmean(values),
        "minimum": min(values),
        "maximum": max(values),
        "count": len(values),
    }


def fit_trace(path):
    if not path:
        return None

    rows = read_csv_rows(path)
    dvdt_values = []
    prev_t = None
    prev_v = None
    prev_charging = None
    sample_count = 0
    charging_samples = 0
    non_charging_samples = 0
    unknown_charging_samples = 0
    charge_full_samples = 0
    unknown_charge_full_samples = 0
    charge_transitions = 0
    skipped_transition_segments = 0
    skipped_charging_segments = 0
    used_segments = 0

    for row in rows:
        t = parse_float(row, "time_s")
        v = parse_float(row, "voltage")
        charging = parse_optional_bool(row, "charging")
        charge_full = parse_optional_bool(row, "charge_full")
        sample_count += 1
        if charging is True:
            charging_samples += 1
        elif charging is False:
            non_charging_samples += 1
        else:
            unknown_charging_samples += 1
        if charge_full is True:
            charge_full_samples += 1
        elif charge_full is None:
            unknown_charge_full_samples += 1
        if prev_t is not None and t > prev_t:
            charge_state_changed = (charging is not None and
                                    prev_charging is not None and
                                    charging != prev_charging)
            charge_segment = charging is True or prev_charging is True
            if charge_state_changed:
                charge_transitions += 1
                skipped_transition_segments += 1
            elif charge_segment:
                skipped_charging_segments += 1
            else:
                dvdt_values.append(abs((v - prev_v) / (t - prev_t)))
                used_segments += 1
        prev_t = t
        prev_v = v
        prev_charging = charging

    if not dvdt_values:
        return {
            "sample_count": sample_count,
            "charging_samples": charging_samples,
            "non_charging_samples": non_charging_samples,
            "unknown_charging_samples": unknown_charging_samples,
            "charge_full_samples": charge_full_samples,
            "unknown_charge_full_samples": unknown_charge_full_samples,
            "charge_transitions": charge_transitions,
            "skipped_transition_segments": skipped_transition_segments,
            "skipped_charging_segments": skipped_charging_segments,
            "used_segments": used_segments,
            "mean_dvdt": None,
            "max_dvdt": None,
            "k1": None,
            "rm_dynamic_scale": None,
            "rm_slope_gain": None,
        }

    mean_dvdt = statistics.fmean(dvdt_values)
    max_dvdt = max(dvdt_values)
    rm_dynamic_scale = max(2.0, min(12.0, 1.0 + max_dvdt * 100.0))
    rm_slope_gain = max(0.01, min(0.20, mean_dvdt * 8.0))
    k1 = -max(1.0, min(12.0, 3.0 + mean_dvdt * 80.0))

    return {
        "sample_count": sample_count,
        "charging_samples": charging_samples,
        "non_charging_samples": non_charging_samples,
        "unknown_charging_samples": unknown_charging_samples,
        "charge_full_samples": charge_full_samples,
        "unknown_charge_full_samples": unknown_charge_full_samples,
        "charge_transitions": charge_transitions,
        "skipped_transition_segments": skipped_transition_segments,
        "skipped_charging_segments": skipped_charging_segments,
        "used_segments": used_segments,
        "mean_dvdt": mean_dvdt,
        "max_dvdt": max_dvdt,
        "k1": k1,
        "rm_dynamic_scale": rm_dynamic_scale,
        "rm_slope_gain": rm_slope_gain,
    }


def print_c_table(ocv):
    if not ocv:
        return

    print("/* Generated OCV table for qm_battery. */")
    print("static const qm_battery_ocv_point_t s_product_ocv_table[] = {")
    for soc, voltage in ocv["table"]:
        print("    {%.1ff, %.4ff, 0.0f}," % (soc, voltage))
    print("};")
    print("")


def print_calibration(ocv, adc, resistance, charge_full_min_v):
    print("/* Suggested qm_battery calibration import. Call after qm_battery_default_config(&cfg). */")
    print("qm_battery_calibration_t calibration;")

    missing = []
    if not ocv:
        missing.append("OCV table")
    if not adc:
        missing.append("ADC conversion")
    if not resistance:
        missing.append("resistance baseline")
    if charge_full_min_v is None:
        missing.append("charge-full minimum voltage")
    if missing:
        print("/* Missing lab inputs: %s. Existing cfg defaults are kept as placeholders. */" %
              ", ".join(missing))

    if ocv:
        print("calibration.ocv_table = s_product_ocv_table;")
        print("calibration.ocv_table_count = sizeof(s_product_ocv_table) / sizeof(s_product_ocv_table[0]);")
        print("calibration.full_anchor_v = %.4ff;" % ocv["full_anchor_v"])
        print("calibration.empty_anchor_v = %.4ff;" % ocv["empty_anchor_v"])
    else:
        print("calibration.ocv_table = cfg.ocv_table;")
        print("calibration.ocv_table_count = cfg.ocv_table_count;")
        print("calibration.full_anchor_v = cfg.full_anchor_v;")
        print("calibration.empty_anchor_v = cfg.empty_anchor_v;")

    if adc:
        print("calibration.adc_scale_v_per_count = %.9ff;" % adc["scale"])
        print("calibration.adc_offset_v = %.6ff;" % adc["offset"])
        print("calibration.adc_divider_ratio = %.6ff;" % adc["divider_ratio"])
    else:
        print("calibration.adc_scale_v_per_count = cfg.adc_scale_v_per_count;")
        print("calibration.adc_offset_v = cfg.adc_offset_v;")
        print("calibration.adc_divider_ratio = cfg.adc_divider_ratio;")

    if resistance:
        print("calibration.r_nominal_ohm = %.6ff;" % resistance["nominal"])
    else:
        print("calibration.r_nominal_ohm = cfg.r_nominal_ohm;")

    if charge_full_min_v is not None:
        print("calibration.charge_full_min_v = %.4ff;" % charge_full_min_v)
    else:
        print("calibration.charge_full_min_v = cfg.charge_full_min_v;")

    print("if (qm_battery_apply_calibration(&cfg, &calibration) != QM_EOK) {")
    print("    /* handle invalid calibration */")
    print("}")
    print("")


def print_config(trace):
    print("/* Suggested qm_battery strategy/tuning overrides. */")
    print("cfg.temp_comp_enable = QM_FALSE;")

    if trace and trace.get("k1") is not None:
        print("cfg.k1_soc_per_vps = %.4ff;" % trace["k1"])
        print("cfg.rm_dynamic_scale = %.4ff;" % trace["rm_dynamic_scale"])
        print("cfg.rm_slope_gain = %.6ff;" % trace["rm_slope_gain"])

    print("")


def print_report(ocv, adc, resistance, trace, charge_full_min_v):
    print("Calibration report:")
    if ocv:
        print("  full_anchor_v: %.4f V" % ocv["full_anchor_v"])
        print("  empty_anchor_v: %.4f V" % ocv["empty_anchor_v"])
        print("  ocv_points: %d" % len(ocv["table"]))
    if adc:
        print("  adc_scale_v_per_count: %.9f" % adc["scale"])
        print("  adc_offset_v: %.6f V at ADC node" % adc["offset"])
        print("  adc_divider_ratio: %.6f" % adc["divider_ratio"])
        print("  adc_max_abs_error: %.6f V" % adc["max_abs_error"])
        print("  adc_rms_error: %.6f V" % adc["rms_error"])
    if resistance:
        print("  r_nominal_ohm: %.6f" % resistance["nominal"])
        print("  r_range_ohm: %.6f .. %.6f, n=%d" %
              (resistance["minimum"], resistance["maximum"], resistance["count"]))
    if charge_full_min_v is not None:
        print("  charge_full_min_v: %.4f V" % charge_full_min_v)
    if trace:
        print("  trace_samples: %d" % trace["sample_count"])
        if trace["unknown_charging_samples"] == trace["sample_count"]:
            print("  trace_charging_column: not provided")
        else:
            print("  trace_charging_samples: %d, non_charging_samples: %d, unknown: %d" %
                  (trace["charging_samples"],
                   trace["non_charging_samples"],
                   trace["unknown_charging_samples"]))
            if trace["unknown_charge_full_samples"] != trace["sample_count"]:
                print("  trace_charge_full_samples: %d" %
                      trace["charge_full_samples"])
            print("  trace_charge_transitions: %d" % trace["charge_transitions"])
            print("  trace_skipped_transition_segments: %d" %
                  trace["skipped_transition_segments"])
            print("  trace_skipped_charging_segments: %d" %
                  trace["skipped_charging_segments"])
        print("  trace_used_dvdt_segments: %d" % trace["used_segments"])
        if trace.get("k1") is not None:
            print("  mean_dvdt: %.8f V/s" % trace["mean_dvdt"])
            print("  max_dvdt: %.8f V/s" % trace["max_dvdt"])
            print("  suggested_k1_soc_per_vps: %.4f" % trace["k1"])
        else:
            print("  trace_dynamic_suggestion: unavailable")
    print("")


def main():
    parser = argparse.ArgumentParser(description="qm_battery calibration helper")
    parser.add_argument("--ocv", help="CSV with columns: soc,voltage")
    parser.add_argument("--adc", help="CSV with columns: raw,voltage")
    parser.add_argument("--resistance", help="CSV with columns: v_before,v_after,i_before,i_after")
    parser.add_argument("--trace", help="CSV with columns: time_s,voltage[,charging[,charge_full]]")
    parser.add_argument("--divider-ratio", type=float, default=1.0,
                        help="Battery voltage / ADC node voltage ratio")
    parser.add_argument("--charge-full-min-v", type=float,
                        help="Minimum terminal voltage for accepting charger-full")
    parser.add_argument("--soc-points", default=",".join(str(v) for v in DEFAULT_SOC_POINTS),
                        help="Comma-separated SOC points for generated OCV table")
    args = parser.parse_args()

    for path in (args.ocv, args.adc, args.resistance, args.trace):
        if path and not Path(path).is_file():
            raise SystemExit("input file not found: %s" % path)

    ocv = build_ocv_table(args.ocv, parse_soc_points(args.soc_points))
    adc = fit_adc(args.adc, args.divider_ratio)
    resistance = fit_resistance(args.resistance)
    trace = fit_trace(args.trace)

    print_c_table(ocv)
    print_calibration(ocv, adc, resistance, args.charge_full_min_v)
    print_config(trace)
    print_report(ocv, adc, resistance, trace, args.charge_full_min_v)


if __name__ == "__main__":
    main()
