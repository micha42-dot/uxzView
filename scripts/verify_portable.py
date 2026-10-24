"""Run the same tests against deployed DLLs, with no SDK directory in PATH."""
from pathlib import Path
import os,shutil,subprocess,re,json,tempfile,time
root=Path(__file__).resolve().parents[1]
dest=root/'dist/uxzView'
tests=dest/'ixview_runtime_tests.exe';qt_test=dest/'Qt6Test.dll'
quality=dest/'ixview_runtime_quality_tests.exe'
storage=dest/'ixview_runtime_storage_tests.exe'
extensions=dest/'ixview_runtime_extension_tests.exe'
workflows=dest/'ixview_runtime_workflow_tests.exe'
if any(path.exists() for path in [tests,qt_test,quality,storage,extensions,workflows]):raise SystemExit('Temporary runtime-test filenames already exist; inspect them first.')
try:
 shutil.copy2(root/'build/ixview_tests.exe',tests)
 shutil.copy2(root/'build/ixview_quality_tests.exe',quality)
 shutil.copy2(root/'build/ixview_storage_tests.exe',storage)
 shutil.copy2(root/'build/ixview_extension_tests.exe',extensions)
 shutil.copy2(root/'build/ixview_workflow_tests.exe',workflows)
 shutil.copy2(root/'.deps/sdk/ucrt64/bin/Qt6Test.dll',qt_test)
 env=os.environ.copy();env['PATH']=str(Path(env['WINDIR'])/'System32')+';'+env['WINDIR']
 for key in ['QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML2_IMPORT_PATH','CMAKE_PREFIX_PATH']:env.pop(key,None)
 version=subprocess.run([str(dest/'uxzView.exe'),'-platform','offscreen','--version'],cwd=dest,env=env,capture_output=True,text=True,encoding='utf-8',check=True,timeout=30)
 expected=re.search(r'project\(IxView VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text(encoding='utf-8-sig'))[1]
 if version.stdout.strip()!='üxzView '+expected:raise SystemExit('Unexpected application version: '+version.stdout)
 print('Installed app smoke check:',version.stdout.strip())
 probe=subprocess.run([str(dest/'uxzView.exe'),'-platform','offscreen','--probe-image',str(root/'tests/fixtures/smoke.png')],cwd=dest,env=env,capture_output=True,text=True,encoding='utf-8',check=True,timeout=30)
 decoded=json.loads(probe.stdout)
 if (decoded['width'],decoded['height'],decoded['pages'],decoded['format'])!=(4,3,1,'png'):raise SystemExit('Installed application image probe failed: '+probe.stdout)
 (root/'build/portable-image-probe.json').write_text(probe.stdout,encoding='utf-8')
 print('Installed application decoded the 4x3 PNG fixture.')
 args=[str(tests),'-platform','offscreen','-o',str(root/'build/portable-test-results.txt')+',txt','-o',str(root/'build/portable-test-results.xml')+',junitxml']
 result=subprocess.run(args,cwd=dest,env=env,timeout=180)
 print((root/'build/portable-test-results.txt').read_text(encoding='utf-8',errors='replace'))
 if result.returncode:raise SystemExit(result.returncode)
 args=[str(quality),'-platform','offscreen','-o',str(root/'build/portable-quality-test-results.txt')+',txt','-o',str(root/'build/portable-quality-test-results.xml')+',junitxml']
 result=subprocess.run(args,cwd=dest,env=env,timeout=300)
 print((root/'build/portable-quality-test-results.txt').read_text(encoding='utf-8',errors='replace'))
 if result.returncode:raise SystemExit(result.returncode)
 args=[str(storage),'-o',str(root/'build/portable-storage-test-results.txt')+',txt','-o',str(root/'build/portable-storage-test-results.xml')+',junitxml']
 result=subprocess.run(args,cwd=dest,env=env,timeout=120)
 print((root/'build/portable-storage-test-results.txt').read_text(encoding='utf-8',errors='replace'))
 if result.returncode:raise SystemExit(result.returncode)
 args=[str(extensions),'-platform','offscreen','-o',str(root/'build/portable-extension-test-results.txt')+',txt','-o',str(root/'build/portable-extension-test-results.xml')+',junitxml']
 result=subprocess.run(args,cwd=dest,env=env,timeout=180)
 print((root/'build/portable-extension-test-results.txt').read_text(encoding='utf-8',errors='replace'))
 if result.returncode:raise SystemExit(result.returncode)
 args=[str(workflows),'-platform','offscreen','-o',str(root/'build/portable-workflow-test-results.txt')+',txt','-o',str(root/'build/portable-workflow-test-results.xml')+',junitxml']
 result=subprocess.run(args,cwd=dest,env=env,timeout=180)
 print((root/'build/portable-workflow-test-results.txt').read_text(encoding='utf-8',errors='replace'))
 if result.returncode:raise SystemExit(result.returncode)
 with tempfile.TemporaryDirectory(prefix='run-portable-startup-',dir=root/'tests/output') as temporary:
  before=Path(temporary)/'before';after=Path(temporary)/'after'
  working=Path(temporary)/'unrelated-working-directory';working.mkdir()
  shutil.copytree(dest,before,ignore=shutil.ignore_patterns('IxView-data','portable.mode','ixview_runtime*','Qt6Test.dll'))
  def storage_info(folder,activate=False):
   command=[str(folder/'uxzView.exe'),'-platform','offscreen','--storage-info']
   if activate:command.append('--portable')
   return json.loads(subprocess.run(command,cwd=working,env=env,capture_output=True,text=True,encoding='utf-8',check=True,timeout=30).stdout)
  first=storage_info(before,True)
  assert first['portable'] and Path(first['data'])==before/'IxView-data',first
  assert Path(first['tools'])==before/'tools',first
  tool_state=json.loads((before/'tools/tools.json').read_text(encoding='utf-8'))
  assert tool_state['layout']=='flat' and len(tool_state['packages'])==7,tool_state
  blank=Path(temporary)/'blank.ppm'
  blank.write_bytes(b'P6\n100 100\n255\n'+b'\xff\xff\xff'*10000)
  def tool_folder(identifier):
   item=tool_state['packages'][identifier]
   return before/'tools'/identifier/item['version']
  qr=tool_state['packages']['zxing-cpp']
  result=subprocess.run([str(tool_folder('zxing-cpp')/qr['entryPoint']),'-json',str(blank)],cwd=working,env=env,capture_output=True,text=True,check=True,timeout=30)
  assert json.loads(result.stdout)==[],result
  ocr=tool_state['packages']['tesseract-ocr']
  for language in ['deu','eng','fra','jpn','kor']:
   model=tool_folder('tessdata-'+language)/'data'
   subprocess.run([str(tool_folder('tesseract-ocr')/ocr['entryPoint']),str(blank),'stdout','--tessdata-dir',str(model),'-l',language],cwd=working,env=env,capture_output=True,text=True,check=True,timeout=30)
  assert not (working/'portable.mode').exists() and not (working/'IxView-data').exists()
  assert Path(first['settings']).is_relative_to(before/'IxView-data'),first
  ini=Path(first['settings']);ini.parent.mkdir(parents=True,exist_ok=True)
  ini.write_text('[files]\ndirectory='+before.as_posix()+'/photos\n',encoding='utf-8')
  catalog=before/'IxView-data/catalog.json'
  catalog.write_text(json.dumps({before.as_posix()+'/photos/image.png':{'rating':4}}))
  for attempt in range(40):
   try:
    before.rename(after)
    break
   except PermissionError:
    if attempt==39:raise
    time.sleep(.05)
  moved=storage_info(after)
  assert moved['portable'] and Path(moved['data'])==after/'IxView-data',moved
  assert Path(moved['tools'])==after/'tools',moved
  assert not (after/'IxView-data/tools').exists()
  assert after.as_posix()+'/photos/image.png' in json.loads((after/'IxView-data/catalog.json').read_text())
  assert after.as_posix()+'/photos' in Path(moved['settings']).read_text(encoding='utf-8')
  extension=after/'IxView-data/extensions/state.json';extension.parent.mkdir(parents=True,exist_ok=True)
  extension.write_text(json.dumps({'disabledFormats':['png']}))
  disabled=subprocess.run([str(after/'uxzView.exe'),'-platform','offscreen','--probe-image',str(root/'tests/fixtures/smoke.png')],cwd=after,env=env,capture_output=True,text=True,timeout=30)
  assert disabled.returncode==2 and 'error' in json.loads(disabled.stdout),disabled
  (root/'build/portable-storage-startup.json').write_text(json.dumps({'first':first,'moved':moved,'private_plugin_preferences_used':True},indent=2)+'\n')
 print('Bundled QR and OCR engines, all five models and relocated tools folder passed without the SDK in PATH.')
 print('Portable runtime tests and relocated-profile startup passed without the SDK in PATH.')
finally:
 if tests.exists():tests.unlink()
 if quality.exists():quality.unlink()
 if storage.exists():storage.unlink()
 if extensions.exists():extensions.unlink()
 if workflows.exists():workflows.unlink()
 if qt_test.exists():qt_test.unlink()
