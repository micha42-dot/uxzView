import pathlib, struct, shutil, os, json
from build_heif import build as build_heif
from build_avif import build as build_avif
from runtime_files import runtime_files, clean_runtime
from bundle_tools import prepare as bundle_tools
from source_archive import documentation_files
root=pathlib.Path(__file__).resolve().parents[1]
sdk=root/'.deps/sdk/ucrt64'; dest=root/'dist/uxzView'
custom_heif = build_heif()
custom_avif = build_avif()
# Deployment is generated output. Start clean so removed plugins cannot linger.
clean_runtime(dest, root)
shutil.copy2(root/'build/uxzView.exe',dest/'uxzView.exe')
for module in ['ixrawpreview.dll','ixextensions.dll']:
    if (root/'build'/module).exists():shutil.copy2(root/'build'/module,dest/module)
plugins=sdk/'share/qt6/plugins'
# Explicit plugin selection keeps the distribution focused on image viewing.
for kind in ['platforms','styles','imageformats','printsupport','iconengines','tls','networkinformation']:
    source=plugins/kind
    if source.exists():
        if kind == 'platforms':
            (dest/kind).mkdir(exist_ok=True)
            for name in ['qwindows.dll','qoffscreen.dll']:
                shutil.copy2(source/name,dest/kind/name)
        else: shutil.copytree(source,dest/kind,dirs_exist_ok=True)
archive_plugin=root/'build/imageformats/ixarchive.dll'
if archive_plugin.exists():shutil.copy2(archive_plugin,dest/'imageformats'/archive_plugin.name)
heif=sdk/'lib/libheif'
if heif.exists():shutil.copytree(heif,dest/'libheif',dirs_exist_ok=True)
(dest/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nTranslations=translations\n',encoding='utf-8')
translations=sdk/'share/qt6/translations'
for name in ['qtbase_de.qm','qtbase_ko.qm','qtbase_ja.qm','qtbase_fr.qm']:
    if (translations/name).exists():
        (dest/'translations').mkdir(exist_ok=True);shutil.copy2(translations/name,dest/'translations'/name)
def imports(file):
    data=file.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
    count=struct.unpack_from('<H',data,pe+6)[0];optlen=struct.unpack_from('<H',data,pe+20)[0];opt=pe+24
    is64=struct.unpack_from('<H',data,opt)[0]==0x20b;dirbase=opt+(112 if is64 else 96)
    sections=[]
    for n in range(count):
        s=opt+optlen+n*40
        vsize,rva,size,pos=struct.unpack_from('<IIII',data,s+8);sections.append((rva,max(vsize,size),pos))
    def offset(rva):
        for start,size,pos in sections:
            if start<=rva<start+size:return pos+rva-start
        raise ValueError(f'Bad RVA {rva} in {file}')
    rva=struct.unpack_from('<I',data,dirbase+8)[0]
    if not rva:return []
    pos=offset(rva);names=[]
    while any(data[pos:pos+20]):
        name=offset(struct.unpack_from('<I',data,pos+12)[0]);end=data.index(0,name);names.append(data[name:end].decode('ascii'));pos+=20
    return names
available={f.name.lower():f for f in (sdk/'bin').glob('*.dll')}
available['libheif.dll'] = custom_heif
available['libavif-16.dll'] = custom_avif
queue=[p for p in runtime_files(dest) if p.suffix.lower()=='.dll']+[dest/'uxzView.exe'];seen=set();copied=[];missing=[]
while queue:
    file=queue.pop();key=file.name.lower()
    if key in seen:continue
    seen.add(key)
    for name in imports(file):
        if name.lower() in seen:continue
        if name.lower() in available:
            target=dest/name
            shutil.copy2(available[name.lower()],target);queue.append(target);copied.append(name)
        elif not name.lower().startswith(('api-ms-','ext-ms-')) and not (pathlib.Path(os.environ['WINDIR'])/'System32'/name).exists():missing.append((file.name,name))
shutil.copytree(sdk/'share/licenses',dest/'licenses',dirs_exist_ok=True)
for source in documentation_files(root):
    target=dest/source.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
(dest/'runtime-manifest.json').write_text(json.dumps({'architecture':'x64','dlls':sorted(set(copied)),'unresolved':missing},indent=2),encoding='utf-8')
(dest/'custom-builds.json').write_text(json.dumps({
    'libheif.dll':json.loads((root/'build/heif-lite/build-origin.json').read_text()),
    'libavif-16.dll':json.loads((root/'build/avif-lite/build-origin.json').read_text())
},indent=2)+'\n',encoding='utf-8')
if missing:raise SystemExit(f'Missing runtime libraries: {missing}')
print(f'Portable app: {dest}; {len(seen)} PE modules, all imports resolved.')

for source in (root/'scripts/windows').glob('*'):
    if source.is_file():shutil.copy2(source,dest/source.name)
inventory = bundle_tools(root, dest)
print(f"Bundled tools: {len(inventory['packages'])} packages, {sum(item['bytes'] for item in inventory['files'].values())/1e6:.2f} MB; existing installed tools preserved.")
