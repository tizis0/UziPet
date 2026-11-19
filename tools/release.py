# Выпуск: «что нового» в notes.txt, сборка и копия exe в dist/UziPet-<версия>.exe.
# Этот exe — всё сразу: запустить на чистом компе — поставится; кинуть на Узи или запустить при ней — обновит.
#   python tools/release.py "Что нового 1" "Что нового 2"
import os, re, shutil, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
notes = sys.argv[1:]
if notes:
    open(os.path.join(root, 'notes.txt'), 'w', encoding='utf-8').write('\n'.join(notes) + '\n')
r = subprocess.run(['cmd', '/c', os.path.join(root, 'build.bat')], cwd=root)
if r.returncode:
    sys.exit('build failed')
ver = re.search(r'UZI_VERSION L"([^"]+)"', open(os.path.join(root, 'src', 'version.h'), encoding='utf-8-sig').read()).group(1)
os.makedirs(os.path.join(root, 'dist'), exist_ok=True)
out = os.path.join(root, 'dist', 'UziPet-%s.exe' % ver)
shutil.copy2(os.path.join(root, 'bin', 'UziPet.exe'), out)
print(out, os.path.getsize(out))
