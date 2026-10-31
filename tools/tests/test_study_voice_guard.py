"""Host unit test for study_voice_guard.c
"""
import pathlib
import subprocess
import tempfile
import unittest


class TestStudyVoiceGuard(unittest.TestCase):
    def test_voice_guard_logic(self):
        root = pathlib.Path(__file__).resolve().parents[2]
        guard_c = root / 'sdk/apps/main/agent/study_voice_guard.c'
        guard_h = root / 'sdk/apps/main/agent/study_voice_guard.h'

        with tempfile.TemporaryDirectory(prefix='voice-guard-') as tmp:
            p = pathlib.Path(tmp)
            (p / 'study_voice_guard.c').write_bytes(guard_c.read_bytes())
            (p / 'study_voice_guard.h').write_bytes(guard_h.read_bytes())

            test_c = p / 'test_guard.c'
            test_c.write_text(r"""
#include <assert.h>
#include <stdio.h>
#include "study_voice_guard.h"

int main(void) {
    /* Pomodoro command recognition */
    assert(study_voice_guard_is_pomodoro_cmd("开启番茄钟"));
    assert(study_voice_guard_is_pomodoro_cmd("帮我打开番茄时钟"));
    assert(study_voice_guard_is_pomodoro_cmd("暂停"));
    assert(study_voice_guard_is_pomodoro_cmd("暂停。"));
    assert(study_voice_guard_is_pomodoro_cmd("恢复"));
    assert(study_voice_guard_is_pomodoro_cmd("继续。"));
    assert(study_voice_guard_is_pomodoro_cmd("结束番茄"));
    assert(study_voice_guard_is_pomodoro_cmd("退出"));

    /* Chat / non-pomodoro commands */
    assert(!study_voice_guard_is_pomodoro_cmd("今天天气怎么样"));
    assert(!study_voice_guard_is_pomodoro_cmd("我想吃番茄炒蛋"));
    assert(!study_voice_guard_is_pomodoro_cmd("你好啊小诺米"));
    assert(!study_voice_guard_is_pomodoro_cmd(NULL));
    assert(!study_voice_guard_is_pomodoro_cmd(""));

    /* Status TTS */
    assert(study_voice_guard_is_status_tts("番茄时钟已暂停"));
    assert(study_voice_guard_is_status_tts("番茄钟已开启"));
    assert(!study_voice_guard_is_status_tts("好的，现在为您播报天气"));
    assert(!study_voice_guard_is_status_tts(NULL));

    puts("PASS: study_voice_guard assertion tests passed");
    return 0;
}
""", encoding='utf-8')

            exe = p / 'test_guard.exe'
            subprocess.run(['gcc', '-std=c11', '-I', str(p), str(p / 'study_voice_guard.c'), str(test_c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
