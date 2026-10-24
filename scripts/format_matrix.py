"""Generate a conservative matrix from actual runtime capability and test reports."""
import json,pathlib,re
root=pathlib.Path(__file__).resolve().parents[1]
formats=json.loads((root/'tests/output/format-capabilities.json').read_text(encoding='utf-8'))
log=(root/'build/test-results.txt').read_text(encoding='utf-8',errors='replace')
roundtrips=set(re.findall(r'PASS\s+: IxTests::roundTripFormats\(([^)]+)\)',log))
quality_path=root/'build/quality-test-results.txt'
quality=quality_path.read_text(encoding='utf-8',errors='replace') if quality_path.exists() else ''
camera_formats={pathlib.Path(name).suffix[1:].upper() for name in re.findall(r'PASS\s+: QualityTests::cameraRaw\(([^)]+)\)',quality)}
quality_note = f'Zusätzlich wurden {len(camera_formats)} reale Kamera-RAW-Formate geprüft.' if camera_formats else 'Keine Kamera-RAW-Fixture in diesem Lauf geprüft.'
rows=['# Formatkompatibilität','','Stand: 23.10.2026, Windows-x64-Paket. Linux/macOS besitzen je nach installierten Qt-/KImageFormats-Plugins andere Fähigkeiten. Die Erkennung basiert auf den tatsächlich geladenen Plugins des getesteten Builds. „Rundlauf“ bedeutet: ein synthetisches RGB-Bild speichern, wieder öffnen und die Größe prüfen. Das ist keine vollständige Konformitätsprüfung aller Formatvarianten. '+quality_note+' Weitere Qualitätsnachweise und Grenzen stehen in [QUALITY.md](QUALITY.md). Die folgenden Rundlaufmarkierungen beziehen sich auf die synthetische Funktionssuite.','','| Dateiformat | Lesen | Schreiben | Mehrseitigkeit | Animation | Metadaten | Verwendete Bibliothek | Implementierungsstatus |','| --- | --- | --- | --- | --- | --- | --- | --- |']
for item in formats:
 name=item['format'];exts=item['extensions'];key=exts[0]
 tested=key in roundtrips
 special={'APNG':'apngComposition','GIF':'animationFrames','PSD':'psd','SVG':'svg','TIFF':'tiffPages'}
 specific=name in special and re.search(r'PASS\s+: IxTests::'+special[name]+r'\(',log)
 read='Ja (getestet)' if tested or specific else 'Ja (Decoder geladen)' if item['read'] else 'Nein'
 write='Ja (Rundlauf)' if tested else 'Ja (Encoder geladen)' if item['write'] else 'Nein'
 multi='Ja, Lesen getestet; Export einzelne Seite' if name=='TIFF' and specific else 'Frames lesen getestet' if name in ('APNG','GIF') else 'Nicht separat geprüft'
 animation='Ja, Wiedergabedaten getestet' if name in ('APNG','GIF') and specific else 'Nicht separat geprüft'
 metadata='ICC/16-Bit und EXIF-Ausrichtungsrunde getestet' if name=='PNG' else 'EXIF lesen / Orientierungsanwendung getestet' if name=='JPEG' else 'QImage / ggf. Exiv2; Kombination nicht separat geprüft'
 status='Synthetischer Test bestanden' if tested or specific else 'Decoder/Encoder vorhanden; keine Formatfixture'
 if name in camera_formats:
  read='Ja (Kamera-Fixture getestet)'
  metadata='16-Bit/ICC und PNG-Pixeltransport getestet; MakerNotes nicht vollständig'
  status='Reale Kamera-Fixture bestanden; weitere Varianten ungeprüft'
 if name=='EXR' and 'PASS   : QualityTests::hdrExportPreservesExtendedValues()' in quality:
  status+='; erweiterte HDR-/Alpha-Werte geprüft'
 if name=='HEIC' and 'PASS   : QualityTests::heicColorAndAlphaRoundTrip()' in quality:
  status+='; Farb-/Alpha-Rundlauf geprüft'
 if name=='PSD' and specific:status='RGB-Zusammenfassungsbild: 8/16 Bit, Raw/RLE und unvollständige Daten geprüft; echte Ebenendokumente/PSB/CMYK ungeprüft'
 if name=='ICNS':status='Schreiben gesperrt: Encoder ändert Bildgröße'
 rows.append('| '+' | '.join([name+' ('+', '.join(exts)+')',read,write,multi,animation,metadata,item['backend'],status])+' |')
rows+=['','Besondere Grenzen: GIF/APNG werden ausschließlich als Einzelbild exportiert. Ein animierter oder mehrseitiger Encoder ist noch nicht implementiert. SVG wird rasterisiert. RAW wird durch das KImageFormats/LibRaw-Modul entwickelt; Kamera-/Sensorvarianten, Demosaicing-Details, HDR und RAW-Metadaten sind noch nicht umfassend validiert. PCX/PSD/Krita/XCF usw. sind flache Ansichten, kein Ebeneneditor. ICO/CUR-Mehrbildvarianten und ICNS-Größenwahl benötigen zusätzliche Tests. Bekannte Exportverluste werden angezeigt; Metadaten über 128 MiB Quelldateigröße werden derzeit nicht vollständig eingelesen.','']
(root/'docs/FORMAT_COMPATIBILITY.md').write_text('\n'.join(rows),encoding='utf-8',newline='\n')
print(f'Matrix: {len(formats)} Laufzeitformate, {len(roundtrips)} Encoder-Rundläufe')
