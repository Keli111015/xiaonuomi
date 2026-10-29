import os
import sys
import subprocess

# idf.py / idf_tools.py refuse to run when MSYSTEM is present (Git Bash).  This
# launcher runs as a native Windows process, strips the MSYS variables, and then
# spawns idf.py so the child (and its own subprocesses) never see them.
for key in list(os.environ):
    if key == 'MSYSTEM' or key.startswith('MSYS') or key.startswith('MSYS2'):
        os.environ.pop(key, None)

IDF_PATH = 'D:/heikesong/ljyd_xjwb03_agent_public_developer/esp-idf'
ADF_PATH = 'D:/heikesong/ljyd_xjwb03_agent_public_developer'
TOOLS = 'D:/heikesong/esp-tools'
PYENV = TOOLS + '/python_env/idf5.4_py3.12_env'

os.environ['IDF_PATH'] = IDF_PATH
os.environ['ADF_PATH'] = ADF_PATH
os.environ['IDF_TOOLS_PATH'] = TOOLS
os.environ['IDF_PYTHON_ENV_PATH'] = PYENV
os.environ['ESP_ROM_ELF_DIR'] = TOOLS + '/tools/esp-rom-elfs/20241011'

bin_dirs = [
    TOOLS + '/tools/cmake/3.30.2/bin',
    TOOLS + '/tools/ninja/1.12.1',
    TOOLS + '/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin',
    TOOLS + '/tools/esp32ulp-elf/2.38_20240113/esp32ulp-elf/bin',
    TOOLS + '/tools/riscv32-esp-elf/esp-14.2.0_20241119/riscv32-esp-elf/bin',
    TOOLS + '/tools/idf-exe/1.0.3',
    PYENV + '/Scripts',
]
os.environ['PATH'] = ';'.join(bin_dirs) + ';' + os.environ.get('PATH', '')

idf = IDF_PATH + '/tools/idf.py'
apps = ADF_PATH + '/apps'

rc = subprocess.call([sys.executable, idf, '-C', apps, 'build'], env=os.environ)
sys.exit(rc)
