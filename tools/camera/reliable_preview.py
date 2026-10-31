"""Local viewer for the validated JPG2 protocol and calibrated pose diagnostics."""
import json,re,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlparse,parse_qs
from pathlib import Path
import serial
from frame_protocol import FrameParser

PORT=8765
lock=threading.Lock()
stop=threading.Event()
pending=threading.Event()
state=dict(frames=0,last=0,error='正在连接开发板',bad_frames=0,jpeg=None,
           pose_busy=False,pose_result='先调整画面方向，再坐正校准',rotation=0,action='pose')
uart=serial.Serial();uart.port='COM4';uart.baudrate=115200;uart.timeout=.15;uart.dtr=False;uart.rts=False
log_file=Path(__file__).with_name('reliable-preview-log.txt')

def capture():
    parser=FrameParser();uart.write(b'cam snap\r\n');deadline=time.monotonic()+20
    tail=bytearray()
    while time.monotonic()<deadline and not stop.is_set():
        data=uart.read(max(1,min(uart.in_waiting,8192)))
        tail.extend(data)
        if b'SNAP_ERROR' in tail: raise ValueError('没有有效摄像头帧，请检查连接')
        if len(tail)>2000:del tail[:-1000]
        image=parser.feed(data)
        if image is not None:return image
    raise TimeoutError('开发板抓拍没有回应，正在重启采集')

def pose_once(action,rotation):
    uart.reset_input_buffer()
    uart.write(f'cam {action} {rotation}\r\n'.encode())
    buf=bytearray();deadline=time.monotonic()+20
    while time.monotonic()<deadline and not stop.is_set():
        buf.extend(uart.read(max(1,min(uart.in_waiting,8192))))
        # Consume a complete line, including quality/calibration fields.
        match=re.search(rb'POSE_RESULT ([^\r\n]+)[\r\n]',buf)
        if match:
            text=match.group(1).decode('ascii','replace')
            with log_file.open('a',encoding='utf-8') as f:f.write(f'{time.time()} {action} {text}\n')
            return dict(re.findall(r'(\w+)=([^ ]+)',text))
        if b'camera pose request: ESP_ERR' in buf:raise RuntimeError('板端识别忙或内存不足，请稍后重试')
        if len(buf)>80000:del buf[:-10000]
    raise TimeoutError('板端识别没有返回结果')

def describe(result):
    ret=result.get('ret','unknown')
    if ret!='ESP_OK':return '未完成识别：摄像头未出图' if ret=='ESP_ERR_TIMEOUT' else '识别失败：'+ret
    if result.get('status')=='0':return '未检测到人体，无法判断坐姿'
    if result.get('valid')!='1':return '关键点不可靠：请确保脸、双眼、双肩完整入镜，画面正立且光线充足'
    if result.get('calibrated')!='1':return '图像关键点可用，请先点击“坐正校准”'
    if result.get('status')=='1':return '接近你的坐正基准（实验结果）'
    reasons={'2':'头部或身体倾斜','3':'比校准时明显靠近','4':'头部相对肩膀明显降低'}
    return '偏离坐正基准：'+reasons.get(result.get('err'),'无法判断')+'（实验结果）'

def run_detection(action,rotation):
    if action=='calibrate':
        message=''
        for i in range(3):
            with lock:state['pose_result']=f'坐正保持不动，正在采集校准样本 {i+1}/3…'
            result=pose_once('calibrate' if i==0 else 'pose',rotation)
            if result.get('ret')!='ESP_OK' or result.get('valid')!='1':
                message='校准未完成：'+describe(result);break
            count=int(result.get('cal_count','0'))
            if count>=3:
                message='坐正校准完成。摄像头位置、方向或使用者改变后需重新校准。';break
            message=f'当前积累 {count}/3 个稳定样本；请坐稳后重试校准'
            stop.wait(.5)
    else:
        result=pose_once('pose',rotation)
        message=describe(result)+f" · {int(result.get('elapsed_ms','0'))/1000:.2f}秒"
    with lock:state.update(pose_busy=False,pose_result=message)

def worker():
    while not stop.is_set():
        processing=False
        try:
            if not uart.is_open:
                uart.open();uart.write(b'cam start\r\n');stop.wait(1)
            if pending.is_set():
                pending.clear();processing=True
                with lock:action,rotation=state['action'],state['rotation']
                run_detection(action,rotation);processing=False
            uart.reset_input_buffer()
            image=capture()
            with lock:state.update(jpeg=image,frames=state['frames']+1,last=time.time(),error='')
        except Exception as exc:
            if isinstance(exc,serial.SerialException):
                try:uart.close()
                except Exception:pass
            elif isinstance(exc,TimeoutError) and uart.is_open:
                try:uart.write(b'cam start\r\n')
                except serial.SerialException:uart.close()
            with lock:
                state['error']=str(exc);state['bad_frames']+=1
                if processing:state.update(pose_busy=False,pose_result='检测未完成：'+str(exc))
        stop.wait(.4)

PAGE='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>小诺米摄像头与坐姿校准</title>
<style>body{margin:0;background:#edf4ef;color:#213b36;font:16px system-ui;text-align:center}main{max-width:800px;margin:28px auto;padding:20px}section{background:white;border-radius:20px;padding:20px}#box{height:390px;display:grid;place-items:center;overflow:hidden}img{max-width:320px;max-height:320px;background:#172722;border-radius:10px}button,select{padding:10px;border-radius:8px;margin:6px}button{border:0;background:#2a6655;color:white}button:disabled{opacity:.45}p{line-height:1.7}.muted{color:#61776d;font-size:14px}</style>
<main><h2>小诺米 · 摄像头与坐姿校准</h2><section><div id="box"><img id="view" alt="等待有效图像"></div><p id="status">正在连接…</p>
<p>画面与识别方向：<select id="rotation"><option value="0">0°</option><option value="90">顺时针90°</option><option value="180">180°</option><option value="270">顺时针270°</option></select></p>
<p><button id="calibrate">坐正校准（3个样本）</button><button id="pose">检测一次坐姿</button><button id="pause">暂停预览</button></p>
<p id="result">先调到画面正立，让脸和双肩完整入镜，再坐正校准。</p><p class="muted">校准仅保存在开发板本次运行内，重启后需重做。检测期间预览暂缓刷新。<br>这是待验证的实验功能；无法判断时不判正常或异常，不扣血、不触发提醒。</p></section>
<p class="muted">USB串口抓拍，非流畅视频。仅显示校验通过的图像；本工具不上传图像，不录制。<br>调整方向会同时作用于显示和模型输入。关闭页面不会停止服务。</p></main>
<script>let paused=false,seen=0;const view=document.querySelector('#view'),rot=document.querySelector('#rotation');
rot.onchange=()=>{view.style.transform=`rotate(${rot.value}deg)`;document.querySelector('#result').textContent='方向已改变，请重新坐正校准'};
document.querySelector('#pause').onclick=e=>{paused=!paused;e.target.textContent=paused?'继续预览':'暂停预览'};
async function action(name){document.querySelector('#pose').disabled=true;document.querySelector('#calibrate').disabled=true;try{const r=await fetch('/'+name+'?rotate='+rot.value,{method:'POST'});if(!r.ok)throw Error('请求失败，请稍后重试')}catch(e){document.querySelector('#result').textContent=e.message}}
document.querySelector('#pose').onclick=()=>action('pose');document.querySelector('#calibrate').onclick=()=>action('calibrate');
async function poll(){try{const s=await(await fetch('/status',{cache:'no-store'})).json();const age=s.last?Math.round(Date.now()/1000-s.last):null;document.querySelector('#status').textContent=s.error||`已校验 ${s.frames} 帧 · 最新图像 ${age} 秒前`;view.style.opacity=age===null||age>10?'.3':'1';document.querySelector('#result').textContent=s.pose_result;for(const id of ['pose','calibrate','rotation'])document.getElementById(id).disabled=s.pose_busy;if(!paused&&s.frames!==seen){seen=s.frames;view.src='/frame.jpg?t='+seen}}catch(e){document.querySelector('#status').textContent='本机服务已断开'}setTimeout(poll,700)}poll();</script></html>'''

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def reply(self,code,body,kind):
        self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass
    def do_POST(self):
        if self.headers.get('Origin') not in (None,f'http://127.0.0.1:{PORT}',f'http://localhost:{PORT}'):
            self.send_error(403);return
        request=urlparse(self.path);rotation=parse_qs(request.query).get('rotate',['0'])[0]
        if request.path not in ('/pose','/calibrate') or rotation not in ('0','90','180','270'):
            self.send_error(400);return
        with lock:
            busy=state['pose_busy']
            if not busy:
                state.update(pose_busy=True,pose_result='请求已排队，正在等待串口…',rotation=int(rotation),action=request.path[1:]);pending.set()
        self.reply(409 if busy else 202,b'','text/plain')
    def do_GET(self):
        path=urlparse(self.path).path
        if path=='/':self.reply(200,PAGE.encode(),'text/html; charset=utf-8')
        elif path=='/status':
            with lock:payload={k:v for k,v in state.items() if k not in ('jpeg','action')}
            self.reply(200,json.dumps(payload).encode(),'application/json')
        elif path=='/frame.jpg':
            with lock:body=state['jpeg']
            self.reply(200 if body else 503,body or b'No valid frame','image/jpeg' if body else 'text/plain')
        else:self.send_error(404)

if __name__=='__main__':
    server=ThreadingHTTPServer(('127.0.0.1',PORT),Handler)
    try:
        threading.Thread(target=worker,daemon=True).start()
        print(f'Reliable preview: http://127.0.0.1:{PORT}',flush=True)
        server.serve_forever()
    finally:
        stop.set();server.server_close()
