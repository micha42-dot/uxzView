import pathlib,re,json
root=pathlib.Path(__file__).resolve().parents[1]
# IDs and shortcut labels are observations from the installed German 4.76 x64 UI.
bindings={1126:'open',1138:'reopen',1178:'thumbnails',1155:'slideshowDialog',1147:'slideshow',1046:'batch',36:'find',1137:'rename',1116:'move',1053:'fileCopy',1060:'delete',1148:'save',1149:'saveAs',72:'compare',153:'saveSelection',1132:'print',1080:'exit',1070:'undo',98:'redo',5:'selection',126:'grid',1061:'text',1065:'cut',1066:'cutOutside',1064:'crop',66:'autoCrop',169:'autoSelect',83:'captureViewport',1063:'copy',1069:'paste',1067:'clear',1062:'clearClipboard',1104:'info',1105:'new',1145:'left',1146:'right',1076:'rotate',1191:'vertical',1102:'horizontal',1139:'resize',10:'canvas',1106:'increaseDepth',1059:'reduceDepth',1051:'grayscale',1050:'colors',54:'histogram',42:'autoCorrect',1153:'sharpen',1133:'redEye',1129:'settings',1166:'animation',1082:'extract',1047:'capture',144:'hotfolder',1115:'minimize',1042:'onTop',1195:'statusbar',1198:'toolbar',1193:'menubar',1192:'titlebar',50:'settings',1096:'fullscreen',44:'settings',1125:'next',1131:'previous',1086:'first',1112:'last',1194:'random',1134:'refresh',1203:'slideshow',1204:'zoomIn',1205:'zoomOut',1114:'lockZoom',55:'lockScroll',1206:'original',1154:'hex',1095:'fitOriginal',1091:'fitWindow',1087:'fitLarge',99:'fitWidth',100:'fitHeight',123:'fitShort',1090:'fitScreen',1088:'fitScreenLarge',1094:'fitScreenWidth',1093:'fitScreenHeight',124:'fitScreenShort',1089:'noFit',28:'smoothFit',79:'smooth',121:'sharpenDisplay',43:'center'}
# Separator positions, read from reference menu screenshots (before the ID).
separators={'datei':{1178,1155,36,1137,1148,1132,1040,1080},'bearbeiten':{1070,60,5,1061,1065,1064,1063},'bild':{1105,1145,1139,1106,1051,42},'optionen':{1165,1166,0,1151,1109,1047,144,80,1115},'ansicht':{50,0,1096,1125,1204,1154},'anzeige-optionen':{28,43}}
shortcut_re=re.compile(r' (Strg\+\S+|Alt\+\S+|Shift\+\S+|F\d+|O|S|T|W|B|I|L|R|V|H|P|G|C|M|D|Esc|Entf|Eingabetaste|Leertaste|Rücktaste|\+|-)\Z')
bindings.update({190:'restoreSelection',69:'maximizeSelection',185:'invertSelection',192:'cropViewport',1109:'jpegTransform',52:'jpegCrop',95:"channel0",96:"channel1",97:"channel2",111:"channel3",1052:"negativeAll",132:"negative0",133:"negative1",134:"negative2",1144:"swap12",1140:"swap02",1141:"cycleBRG",1143:"swap01",1142:"cycleGBR"})
menus=[]
for filename,title,key in [('datei','&Datei','Datei'),('bearbeiten','&Bearbeiten','Bearbeiten'),('bild','B&ild','Bild'),('optionen','&Optionen','Optionen'),('ansicht','&Ansicht','Ansicht'),('anzeige-optionen','Anzeige-Optionen (Fenstermodus)','Anzeige-Optionen (Fenstermodus)'),('kanal','Farbkanal anzeigen','Farbkanal anzeigen'),('negativ','Negativ erstellen (Farben invertieren)','Negativ erstellen (Farben invertieren)'),('tauschen','Farben tauschen','Farben tauschen')]:
 text=(root/f'reference/{filename}.uia.txt').read_text(encoding='utf-8');lines=text.splitlines();start=next(i for i,l in enumerate(lines) if re.search(r'\bMenü '+re.escape(key)+r'$',l));items=[]
 for line in lines[start+1:]:
  if not line.startswith('\t\t\t'):break
  m=re.match(r'\t\t\t\d+ Menüelement (?:\(disabled\) )?(.*?)(?: ID: (\d+))?$',line)
  if not m:continue
  label=m[1];id=int(m[2]) if m[2] else 0;shortcut='';sm=shortcut_re.search(label)
  if sm:shortcut=sm[1];label=label[:sm.start()]
  item={'label':label,'originalId':id,'shortcut':shortcut,'command':bindings.get(id),'verification':'observed-ui-2026-10-17','accessKey':None}
  if id in separators.get(filename,set()) and id!=0:items.append({'separator':True})
  if label=='Letzte Dateien öffnen':item['dynamic']='recent'
  if label=='Letzte gespeicherte Dateien öffnen':item['dynamic']='saved'
  if label=='Anzeige-Optionen (Fenstermodus)':item['submenu']='anzeige-optionen'
  for child,labelMatch in [('kanal','Farbkanal anzeigen'),('negativ','Negativ erstellen (Farben invertieren)'),('tauschen','Farben tauschen')]:
   if label==labelMatch:item['submenu']=child
  items.append(item)
 menus.append({'id':filename,'label':title,'items':items})
reference={'reference':{'product':'IrfanView','version':'4.76.0.0','architecture':'x64','language':'Deutsch.dll 4.76','installation':'C:/Program Files/IrfanView','method':'Windows UI Automation plus visual inspection; no proprietary resources copied'},'policy':'Unimplemented commands stay in reference data and are omitted from the working UI. Access keys are null unless verified. Menu order remains a verified subsequence.','menus':menus}
(root/'reference/menus.de.json').write_text(json.dumps(reference,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'resources/menus.de.json').write_text(json.dumps(reference,ensure_ascii=False,indent=2),encoding='utf-8')
print('Reference menus:',len(menus),'items:',sum(len(m['items']) for m in menus))
