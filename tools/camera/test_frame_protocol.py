import base64,unittest
from frame_protocol import FrameParser,fnv1a

IMAGE=b'\xff\xd8'+bytes(range(256))*2+b'\xff\xd9'
def wire():
    lines=[f'JPG2_BEGIN 7 {len(IMAGE)} {fnv1a(IMAGE):08x}\r\n'.encode()]
    for off in range(0,len(IMAGE),96):
        lines.append(f'JPG2_DATA 7 {off} '.encode()+base64.b64encode(IMAGE[off:off+96])+b'\r\n')
    lines.append(b'JPG2_END 7\r\n')
    return lines
class ProtocolTest(unittest.TestCase):
    def test_logs_and_fragmented_reads(self):
        stream=b'[wifi] reconnect\r\n'.join(wire())
        parser=FrameParser();found=[]
        for i in range(0,len(stream),7):
            result=parser.feed(stream[i:i+7])
            if result:found.append(result)
        self.assertEqual(found,[IMAGE])
    def test_missing_chunk_rejected(self):
        lines=wire();del lines[2]
        with self.assertRaises(ValueError):FrameParser().feed(b''.join(lines))
    def test_corrupt_payload_rejected(self):
        lines=wire()
        prefix,payload=lines[2].rsplit(b' ',1)
        lines[2]=prefix+b' '+(b'A' if payload[:1]!=b'A' else b'B')+payload[1:]
        with self.assertRaises(ValueError):FrameParser().feed(b''.join(lines))
    def test_duplicate_chunk_rejected(self):
        lines=wire();lines.insert(2,lines[1])
        with self.assertRaises(ValueError):FrameParser().feed(b''.join(lines))
    def test_old_frame_ignored(self):
        lines=wire();lines.insert(2,b'JPG2_DATA 6 0 AAAA\r\n')
        self.assertEqual(FrameParser().feed(b''.join(lines)),IMAGE)
if __name__=='__main__':unittest.main()
