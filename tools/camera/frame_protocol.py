"""Validated UART JPEG chunk reassembly; unrelated log lines are ignored."""
import base64
import re

def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value

class FrameParser:
    def __init__(self):
        self.pending = bytearray()
        self.frame_id = None
        self.data = bytearray()
        self.size = 0
        self.checksum = 0

    def feed(self, chunk):
        self.pending.extend(chunk)
        while b'\n' in self.pending:
            raw, _, rest = self.pending.partition(b'\n')
            self.pending = bytearray(rest)
            line = bytes(raw).rstrip(b'\r')
            begin = re.search(rb'JPG2_BEGIN (\d+) (\d+) ([0-9a-f]{8})$', line)
            if begin:
                self.frame_id, self.size = map(int, begin.groups()[:2])
                self.checksum = int(begin.group(3),16)
                if not 100 <= self.size <= 300000: raise ValueError('图像长度异常')
                self.data.clear()
                continue
            part = re.search(rb'JPG2_DATA (\d+) (\d+) ([A-Za-z0-9+/=]+)$',line)
            if part and int(part.group(1)) == self.frame_id:
                offset = int(part.group(2))
                decoded = base64.b64decode(part.group(3),validate=True)
                if offset != len(self.data) or not 1 <= len(decoded) <= 96:
                    raise ValueError('图像分段缺失或顺序错误')
                self.data.extend(decoded)
                if len(self.data) > self.size: raise ValueError('图像分段长度超限')
                continue
            end = re.search(rb'JPG2_END (\d+)$',line)
            if end and int(end.group(1)) == self.frame_id:
                data = bytes(self.data)
                if len(data) != self.size or fnv1a(data) != self.checksum:
                    raise ValueError('图像校验失败，已丢弃该帧')
                if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
                    raise ValueError('JPEG格式无效')
                self.frame_id = None
                return data
        if len(self.pending) > 8192: raise ValueError('串口单行超限')
        return None
