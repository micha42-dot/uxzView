import pathlib,hashlib,json
root=pathlib.Path(__file__).resolve().parents[1]
packages=[]
for archive in sorted((root/'.deps/cache').glob('*.pkg.tar.zst')):
 packages.append({'file':archive.name,'url':'https://repo.msys2.org/mingw/ucrt64/'+archive.name,'sha256':hashlib.file_digest(archive.open('rb'),'sha256').hexdigest()})
(root/'scripts/sdk-lock.json').write_text(json.dumps({'platform':'Windows x64 UCRT','packages':packages},indent=2),encoding='utf-8')
print('Locked',len(packages),'packages')
