"""Apply the reviewed overlay to a clean, exact-version upstream SDK."""
import argparse,hashlib,json,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk',type=Path)
    parser.add_argument('--apply',action='store_true',help='Copy files; default only checks')
    args=parser.parse_args();sdk=args.sdk.resolve()
    manifest=json.loads((HERE/'overlay-manifest.json').read_text(encoding='utf-8'))
    head=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
    if head!=manifest['base_commit']:raise SystemExit('SDK version mismatch: expected '+manifest['base_commit'])
    root=HERE/'sdk-overlay'
    for entry in manifest['files']:
        relative=Path(entry['path']);source=root/relative;target=(sdk/relative).resolve()
        if not target.is_relative_to(sdk) or not source.resolve().is_relative_to(root.resolve()):raise SystemExit('Unsafe manifest path')
        if hashlib.sha256(source.read_bytes().replace(b'\r\n',b'\n')).hexdigest()!=entry['sha256']:raise SystemExit('Overlay hash mismatch: '+entry['path'])
    if not args.apply:
        print('Version and',len(manifest['files']),'file hashes verified. No SDK files changed.');return
    if subprocess.run(['git','-C',str(sdk),'diff','--quiet','HEAD']).returncode:raise SystemExit('SDK has tracked changes; preserve them before applying')
    for entry in manifest['files']:
        target=sdk/entry['path']
        if target.exists() and subprocess.run(['git','-C',str(sdk),'ls-files','--error-unmatch',entry['path']],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:raise SystemExit('Untracked target exists: '+entry['path'])
    for entry in manifest['files']:
        source=root/entry['path'];target=sdk/entry['path']
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source.read_bytes())
    exclude_path=subprocess.check_output(['git','-C',str(sdk),'rev-parse','--git-path','info/exclude'],text=True).strip()
    exclude=Path(exclude_path)
    if not exclude.is_absolute():exclude=sdk/exclude
    exclude.parent.mkdir(parents=True,exist_ok=True)
    pattern='/apps/main/cloud/ljyd_cloud_local.h'
    old=exclude.read_text(encoding='utf-8') if exclude.exists() else ''
    if pattern not in old.splitlines():exclude.write_text(old+'\n'+pattern+'\n',encoding='utf-8')
    print('Overlay applied. Create private ljyd_cloud_local.h from the example before building.')
if __name__=='__main__':main()
