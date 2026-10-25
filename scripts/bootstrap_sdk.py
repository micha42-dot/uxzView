"""Fetch the exact SDK packages in sdk-lock.json; installs only inside this project."""
import concurrent.futures,hashlib,json,pathlib,urllib.request,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
cache=root/'.deps/cache';cache.mkdir(parents=True,exist_ok=True)
manifest=json.loads((root/'scripts/sdk-lock.json').read_text(encoding='utf-8'))
def fetch(package):
    target=cache/package['file']
    def valid(path):return path.exists() and hashlib.file_digest(path.open('rb'),'sha256').hexdigest()==package['sha256']
    if valid(target):return
    part=target.with_suffix(target.suffix+'.part')
    print('Download',package['file'],flush=True)
    with urllib.request.urlopen(package['url'],timeout=120) as response,part.open('wb') as output:
        while block:=response.read(1024*1024):output.write(block)
    if not valid(part):raise RuntimeError('SHA-256 mismatch: '+package['file'])
    part.replace(target)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(fetch,manifest['packages']))
subprocess.run([sys.executable,str(root/'scripts/extract_sdk.py')],check=True)
