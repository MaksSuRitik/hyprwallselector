#!/usr/bin/env python3
"""Exercise the shipped QML ownership/rollback bridge with an isolated provider."""
from pathlib import Path
import json,os,subprocess,sys,tempfile,time,uuid
component=Path(__file__).resolve().parents[1]/'integrations/caelestia/WallpaperPreview.qml'
with tempfile.TemporaryDirectory(prefix='hws-preview-') as temporary:
 root=Path(temporary);(root/'mocks').mkdir();(root/'runtime').mkdir(mode=0o700)
 (root/'WallpaperPreview.qml').write_text(component.read_text().replace('import qs.services','import "mocks"'))
 (root/'mocks/qmldir').write_text('singleton Wallpapers 1.0 Wallpapers.qml\n')
 (root/'mocks/Wallpapers.qml').write_text('''pragma Singleton
import QtQuick
QtObject {
 property string actualCurrent: "/original.mp4"
 property string previewPath: ""
 property bool showPreview: false
 property bool isTrackingRollback: false
 property string rollbackPath: ""
 readonly property string current: showPreview ? previewPath : actualCurrent
 function isVideo(path) {return /\\.(mp4|mkv|webm)$/i.test(path);}
 function preview(path) {if(!isTrackingRollback){rollbackPath=actualCurrent;isTrackingRollback=true;}previewPath=path;showPreview=true;}
 function stopPreview() {showPreview=false;if(isTrackingRollback){actualCurrent=rollbackPath;isTrackingRollback=false;}}
 function setWallpaper(path) {actualCurrent=path;isTrackingRollback=false;stopPreview();}
}
''')
 (root/'shell.qml').write_text('''import QtQuick
import Quickshell
import Quickshell.Io
import "mocks"
ShellRoot {
 WallpaperPreview {}
 IpcHandler {target: "test-native"
  function set(path: string): void {Wallpapers.setWallpaper(path);}
  function preview(path: string): void {Wallpapers.preview(path);}
  function stop(): void {Wallpapers.stopPreview();}
 }
}
''')
 env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='',XDG_RUNTIME_DIR=str(root/'runtime'))
 prefix=['qs','--no-color','-p',str(root/'shell.qml')]
 log=(root/'log').open('w')
 proc=subprocess.Popen(prefix,env=env,stdout=log,stderr=subprocess.STDOUT)
 def call(method,*args,target='hws-preview'):
  r=subprocess.run(prefix+['ipc','call',target,method,*map(str,args)],env=env,capture_output=True,text=True,timeout=5)
  if r.returncode:raise RuntimeError(r.stderr+r.stdout)
  return json.loads(r.stdout) if target=='hws-preview' else None
 def token():return str(uuid.uuid4())
 try:
  for _ in range(50):
   try:call('status');break
   except Exception:time.sleep(.05)
  else:raise RuntimeError('Bridge failed to start')
  a=token();assert call('begin',a,os.getpid())['ok']
  assert not call('begin',token(),os.getpid())['ok']
  p=call('preview',a,1,'/candidate.mp4');assert p['actual']=='/original.mp4' and p['path']=='/candidate.mp4'
  assert not call('preview',a,1,'/stale.png')['ok']
  assert call('cancel',a,2)['path']=='/original.mp4'
  assert not call('preview',a,3,'/late.png')['ok']
  b=token();call('cancel',b,1000);assert not call('begin',b,os.getpid())['ok']
  c=token();call('begin',c,os.getpid());call('preview',c,1,'/candidate.png')
  assert call('commit',c,2,'/confirmed.png')['actual']=='/confirmed.png'
  assert call('cancel',c,3)['actual']=='/confirmed.png'
  d=token();call('begin',d,os.getpid());call('preview',d,1,'/temporary.png')
  call('set','/external.png',target='test-native')
  assert call('cancel',d,2)['actual']=='/external.png'
  e=token();call('begin',e,os.getpid());call('preview',e,1,'/temporary.png')
  call('preview','/native-picker.png',target='test-native')
  assert call('cancel',e,2)['path']=='/native-picker.png'
  call('stop',target='test-native')
  child=subprocess.Popen(['sleep','1'])
  f=token();call('begin',f,child.pid);call('preview',f,1,'/crashed-owner.png');child.wait()
  for _ in range(40):
   s=call('status')
   if not s['preview']:break
   time.sleep(.05)
  assert not s['preview'] and s['actual']=='/external.png'
  print('Preview ownership, stale fencing, commit/cancel, external choices and owner-death rollback passed')
 except Exception:
  log.flush();print((root/'log').read_text(),file=sys.stderr);raise
 finally:
  proc.terminate();proc.wait(timeout=3);log.close()
