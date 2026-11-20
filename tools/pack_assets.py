# Ассеты и расширение кладутся внутрь UziPet.exe: один файл — и программа, и установщик, и обновление.
# Пишет obj/assets.pak (свой простой формат), obj/manifest.json (версия и «что нового») и obj/uzi.rc.
# Формат pak: u32 число файлов, дальше на каждый: u16 длина имени, имя в utf-8, u32 размер, байты.
import hashlib, io, json, os, re, struct
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
obj = os.path.join(root, 'obj')
os.makedirs(obj, exist_ok=True)
ver = re.search(r'UZI_VERSION L"([^"]+)"', open(os.path.join(root, 'src', 'version.h'), encoding='utf-8-sig').read()).group(1)
notes_path = os.path.join(root, 'notes.txt')
notes = [l.strip() for l in open(notes_path, encoding='utf-8-sig')] if os.path.exists(notes_path) else []
notes = [n for n in notes if n]

def python_runtime():
    # Official Windows x64 embeddable distribution, including its license.
    version = '3.14.10'
    sha256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
    name = f'python-{version}-embed-amd64.zip'
    cache = Path(obj) / name
    data = cache.read_bytes() if cache.is_file() else b''
    if hashlib.sha256(data).hexdigest() != sha256:
        with urlopen(f'https://www.python.org/ftp/python/{version}/{name}', timeout=45) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != sha256:
            raise ValueError(f'SHA-256 mismatch for {name}')
        cache.write_bytes(data)
    with ZipFile(io.BytesIO(data)) as archive:
        for entry in archive.infolist():
            if not entry.is_dir():
                yield 'python/' + entry.filename, archive.read(entry)
    yield 'python/embedded-version.txt', version.encode('ascii')


files = []
for top in ('assets', 'extension'):
    base = os.path.join(root, top)
    for d, _, names in os.walk(base):
        for n in sorted(names):
            if n == 'token.json' or n.endswith('.log'):
                continue
            full = os.path.join(d, n)
            files.append((os.path.relpath(full, root).replace('\\', '/'), Path(full).read_bytes()))
files.extend(python_runtime())

with open(os.path.join(obj, 'assets.pak'), 'wb') as f:
    f.write(struct.pack('<I', len(files)))
    for name, data in files:
        nb = name.encode('utf-8')
        f.write(struct.pack('<H', len(nb)) + nb + struct.pack('<I', len(data)) + data)

json.dump({'app': 'UziPet', 'version': ver, 'notes': notes}, open(os.path.join(obj, 'manifest.json'), 'w', encoding='utf-8'),
          ensure_ascii=False, indent=1)
esc = lambda p: p.replace('\\', '\\\\')

# «паспорт» exe: значок, версия, издатель и манифест. Без них неподписанный exe с упакованными ресурсами
# эвристика Defender принимает за загрузчик (11.11: Trojan:Win32/Wacatac.B!ml у Графуса)
ico = os.path.join(obj, 'app.ico')
try:
    from PIL import Image
    Image.open(os.path.join(root, 'extension', 'face3.png')).convert('RGBA').save(
        ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
except Exception as e:
    print('pack: no app icon (%s)' % e)
    ico = None
nums = [int(x) for x in re.findall(r'\d+', ver)[:3]] + [0, 0, 0]
vn = '%d,%d,%d,0' % tuple(nums[:3])
man = os.path.join(obj, 'app.manifest')
open(man, 'w', encoding='utf-8').write('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="Tizis.UziPet" version="%s" processorArchitecture="amd64"/>
  <description>UziPet - Uzi desktop companion</description>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application>
  </compatibility>
</assembly>
''' % vn.replace(',', '.'))
rc = 'ASSETS RCDATA "%s"\nUZIMANIFEST RCDATA "%s"\n' % (esc(os.path.join(obj, 'assets.pak')), esc(os.path.join(obj, 'manifest.json')))
if ico:
    rc += '1 ICON "%s"\n' % esc(ico)
rc += '1 24 "%s"\n' % esc(man)
rc += '''1 VERSIONINFO
FILEVERSION %s
PRODUCTVERSION %s
FILEOS 0x40004
FILETYPE 0x1
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "Tizis"
      VALUE "FileDescription", "UziPet - Uzi Doorman desktop companion"
      VALUE "FileVersion", "%s"
      VALUE "InternalName", "UziPet"
      VALUE "LegalCopyright", "Tizis"
      VALUE "OriginalFilename", "UziPet.exe"
      VALUE "ProductName", "UziPet"
      VALUE "ProductVersion", "%s"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
''' % (vn, vn, ver, ver)
open(os.path.join(obj, 'uzi.rc'), 'w', encoding='ascii').write(rc)
print('pack: %d files, version %s, %d notes' % (len(files), ver, len(notes)))
