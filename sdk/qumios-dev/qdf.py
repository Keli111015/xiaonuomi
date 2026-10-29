#!/usr/bin/env python
#encoding=utf-8

import sys,getopt
import os
import shutil
import re


RE_KCONFIG = r'^qm_kconfig(\.projbuild)?(\.in)?$'


def get_python_cmd():
    return sys.executable if sys.executable else 'python'


def collect_source_paths(kconfig_path):
    """
    提取 Kconfig 文件中通过 source 声明引入的配置路径。
    返回值为展开环境变量后的绝对路径集合，仅用于过滤主树中已显式包含的配置文件。
    """
    source_paths = set()
    if not os.path.isfile(kconfig_path):
        return source_paths

    source_pattern = re.compile(r'^\s*source\s+"([^"]+)"')
    with open(kconfig_path, 'r') as file:
        for line in file:
            match = source_pattern.match(line)
            if not match:
                continue
            expanded_path = os.path.expandvars(match.group(1))
            source_paths.add(os.path.abspath(expanded_path))
    return source_paths


def collect_main_tree_paths(qdf_path):
    """
    收集主配置树中已显式 source 的 Kconfig 路径。
    当前实现会读取根 qm_kconfig 以及其直接引用的一级聚合文件，用于近似判断哪些外部配置已在主树中。
    """
    root_kconfig = os.path.join(qdf_path, 'qm_kconfig')
    main_tree_paths = set([os.path.abspath(root_kconfig)])
    first_level_paths = collect_source_paths(root_kconfig)
    main_tree_paths.update(first_level_paths)

    for first_level_path in first_level_paths:
        main_tree_paths.update(collect_source_paths(first_level_path))

    return main_tree_paths


def collect_dynamic_kconfig_paths(work_dir, main_tree_paths):
    """
    扫描当前工作目录下所有可识别的 qm_kconfig* 文件。
    仅返回主配置树尚未显式包含的外部配置路径，并按路径排序以保持输出稳定。
    """
    dynamic_paths = set()
    qm_config_dir = os.path.abspath(os.path.join(work_dir, 'qm_config'))
    for root, dirs, files in os.walk(work_dir):
        abs_root = os.path.abspath(root)
        if abs_root.startswith(qm_config_dir):
            continue
        for file_name in files:
            if not re.match(RE_KCONFIG, file_name):
                continue
            abs_path = os.path.abspath(os.path.join(root, file_name))
            if abs_path in main_tree_paths:
                continue
            dynamic_paths.add(abs_path)
    return sorted(dynamic_paths)


def build_dynamic_kconfig(work_dir, qdf_path):
    """
    生成本次 menuconfig/genconfig 共用的临时顶层 Kconfig 入口。
    入口会先导入仓库主 qm_kconfig，再追加当前目录下主树未显式包含的外部配置项。
    """
    qm_config_dir = os.path.join(work_dir, 'qm_config')
    dynamic_kconfig_path = os.path.join(qm_config_dir, 'qm_kconfig.dynamic')
    main_tree_paths = collect_main_tree_paths(qdf_path)
    dynamic_paths = collect_dynamic_kconfig_paths(work_dir, main_tree_paths)

    with open(dynamic_kconfig_path, 'w') as file:
        file.write('# Automatically generated Kconfig entry. DO NOT EDIT.\n')
        file.write('source "${QDF_PATH}/qm_kconfig"\n')
        for dynamic_path in dynamic_paths:
            file.write('source "{}"\n'.format(dynamic_path))

    return dynamic_kconfig_path


def main(argv):
    option = sys.argv[1]
    python_cmd = get_python_cmd()
    qdf_path = os.environ.get("QDF_PATH")
    qdf_menuconfig_path = os.path.join(qdf_path, 'tools', 'kconfig', 'menuconfig.py')
    qdf_genconfig_path = os.path.join(qdf_path, 'tools', 'kconfig', 'genconfig.py')
    cur_work_dir  = os.getcwd()
    work_name = cur_work_dir.rsplit("/", 1)[-1]
    work_dir = '%s/apps/%s' %(qdf_path,work_name)
    dynamic_kconfig_path = os.path.abspath(os.path.join(cur_work_dir, 'qm_config', 'qm_kconfig.dynamic'))
    qdf_config_path = os.path.join(qdf_path, '.config')
    qdf_header_path = os.path.join(qdf_path, 'config.h')
    local_config_path = os.path.join('.', 'qm_config', '.config')
    local_header_path = os.path.join('.', 'qm_config', 'qm_config.h')
    
    if not os.path.exists("qm_config"):
        os.makedirs("qm_config")

    dynamic_kconfig_path = build_dynamic_kconfig(cur_work_dir, qdf_path)

    if option == "menuconfig":
        os.system('cd qm_config; "{}" "{}" "{}"'.format(python_cmd,
                                                        qdf_menuconfig_path,
                                                        dynamic_kconfig_path))
        if os.path.isfile(local_config_path):
            shutil.copy(local_config_path, '%s' %qdf_path)
            os.system('cd "{}"; "{}" "{}" "{}"'.format(qdf_path,
                                                       python_cmd,
                                                       qdf_genconfig_path,
                                                       dynamic_kconfig_path))
            if os.path.isfile(qdf_config_path):
                os.remove(qdf_config_path)
        if os.path.isfile(qdf_header_path):
            shutil.move(qdf_header_path, local_header_path)
    
    if option == "genconfig":
        if os.path.isfile(local_config_path):
            shutil.copy(local_config_path, '%s' %qdf_path)
            os.system('cd "{}"; "{}" "{}" "{}"'.format(qdf_path,
                                                       python_cmd,
                                                       qdf_genconfig_path,
                                                       dynamic_kconfig_path))
            if os.path.isfile(qdf_config_path):
                os.remove(qdf_config_path)
        if os.path.isfile(qdf_header_path):
            shutil.move(qdf_header_path, local_header_path)

if __name__ == '__main__':
    main(sys.argv[1:])
