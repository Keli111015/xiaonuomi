"""Compile actual pose calibration/classifier code with synthetic keypoints."""
from pathlib import Path
import tempfile,subprocess,argparse
ap=argparse.ArgumentParser();ap.add_argument('--cxx',default='g++');a=ap.parse_args()
r=Path(__file__).resolve().parents[2]
s=(r/'sdk/apps/main/pose/yolo11_pose.cpp').read_text(encoding='utf-8');block=s[s.index('#define POSE_YAW_BAD_MAX'):s.index('extern "C" esp_err_t yolo11_pose_init')]
with tempfile.TemporaryDirectory() as t:
 p=Path(t)
 (p/'esp_err.h').write_text('typedef int esp_err_t;\n')
 (p/'yolo11_pose.h').write_bytes((r/'sdk/apps/main/pose/yolo11_pose.h').read_bytes())
 (p/'test.cpp').write_text(r"""
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <vector>
#include "yolo11_pose.h"
#define ESP_LOGI(...) ((void)0)
namespace dl {namespace detect {struct result_t {float score;std::vector<int> keypoint;};}}
"""+block+r"""
int main(){
 dl::detect::result_t r{.9f,std::vector<int>(34,0)};
 r.keypoint[0]=120;r.keypoint[1]=115;
 r.keypoint[2]=100;r.keypoint[3]=100;r.keypoint[4]=140;r.keypoint[5]=100;
 r.keypoint[10]=60;r.keypoint[11]=180;r.keypoint[12]=180;r.keypoint[13]=180;
 yolo11_pose_frame_result_t o{};
 for(int i=1;i<=3;i++){analyze_pose_result(r,320,240,&o);assert(yolo11_pose_calibration_count()==i);assert(o.calibrated==(i==3));assert(o.status==(i==3?YOLO11_POSE_FRAME_OK:YOLO11_POSE_FRAME_UNKNOWN));}
 assert(yolo11_pose_calibration_add(&o)==3);
 auto bad=r;bad.score=.2f;analyze_pose_result(bad,320,240,&o);assert(!o.valid && o.status==YOLO11_POSE_FRAME_UNKNOWN);
 bad=r;bad.keypoint[2]=0;bad.keypoint[3]=0;analyze_pose_result(bad,320,240,&o);assert(!o.valid);
 bad=r;bad.keypoint[0]=146;analyze_pose_result(bad,320,240,&o);assert(o.status==YOLO11_POSE_FRAME_BAD_POSTURE);
 yolo11_pose_calibration_reset();analyze_pose_result(r,320,240,&o);assert(yolo11_pose_calibration_count()==1);
 auto sample=o;sample.lean+=.2f;assert(yolo11_pose_calibration_add(&sample)==1);
 sample.eye_w_ratio=0;assert(yolo11_pose_calibration_add(&sample)==1);
 sample.eye_w_ratio=NAN;assert(yolo11_pose_calibration_add(&sample)==1);
 puts("PASS: three distinct frames, unknown before calibration, frozen baseline, low score/missing keypoint rejection, relative tilt, reset, lean stability, invalid calibration");
}
""",encoding='utf-8')
 subprocess.run([a.cxx,'-std=c++17','-static','-I',str(p),str(p/'test.cpp'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
