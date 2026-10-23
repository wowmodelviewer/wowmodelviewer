"""Verify every exported Orc face/posture against the client's DB, then reimport each FBX."""
import argparse,json,pathlib,sqlite3,subprocess,os,sys
p=argparse.ArgumentParser();p.add_argument('package',type=pathlib.Path);p.add_argument('--database',type=pathlib.Path,required=True);p.add_argument('--inspector',type=pathlib.Path,required=True);a=p.parse_args()
root=a.package.resolve();main=json.loads((root/'catalog.json').read_text())
assert main['version']==2 and main['requiredFeatures']==['conditional-body-models-v1']
assert main['raceId']==2 and main['sexId']==0
assert {b['choice'] for b in main['bodyVariants']}=={438,439}
c=sqlite3.connect(a.database.resolve().as_uri()+'?mode=ro',uri=True)
checks=0
for body in main['bodyVariants']:
 catalog_path=(root/body['catalog']).resolve();assert catalog_path.is_relative_to(root)
 cat=json.loads(catalog_path.read_text());assert cat['modelFileId']==body['modelFileId']
 assert cat['version']==2 and cat['bodyModelFileId']==body['modelFileId']
 model=c.execute('SELECT M.FileDataID FROM ChrCustomizationElement E JOIN ChrCustomizationCondModel C ON C.ID=E.ChrCustomizationCondModelID JOIN CreatureModelData M ON M.ID=C.CreatureModelDataID WHERE E.ChrCustomizationChoiceID=?',(body['choice'],)).fetchall()
 if not model:model=c.execute('SELECT M.FileDataID FROM ChrModel C JOIN CreatureDisplayInfo D ON D.ID=C.DisplayID JOIN CreatureModelData M ON M.ID=D.ModelID WHERE C.ID=?',(cat['chrModelId'],)).fetchall()
 assert model==[(body['modelFileId'],)]
 sets={s['id']:s for s in cat['boneSets']};bones={b['index']:b for b in cat['bones']}
 faces=next(o for o in cat['options'] if o['id']==20)['choices']
 for face in faces:
  active={e['boneSet'] for e in face['effects'] if 'boneSet' in e and e['relatedChoice'] in (0,body['choice'])}
  authored=c.execute('SELECT E.ChrCustomizationBoneSetID,B.BoneFileDataID,B.ModelFileDataID FROM ChrCustomizationElement E JOIN ChrCustomizationBoneSet B ON B.ID=E.ChrCustomizationBoneSetID WHERE E.ChrCustomizationChoiceID=? AND E.RelatedChrCustomizationChoiceID IN (0,?)',(face['id'],body['choice'])).fetchall()
  assert len(authored)==1 and active=={authored[0][0]},(face['id'],body,active,authored)
  bone_set=sets[authored[0][0]]
  assert bone_set['fileId']==authored[0][1] and bone_set['modelFileId']==authored[0][2]==cat['modelFileId']
  for t in bone_set['transforms']:assert bones[t['bone']]['node']==t['node'] and len(t['matrix'])==16
  checks+=1
 subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('check-package.py')),str(catalog_path.parent),str(a.inspector)],env={**os.environ,'SystemDrive':'C:'},check=True)
 print(body['name'],len(faces),'faces',len(sets),'bone sets',len(cat['animationTakes']),'clips',flush=True)
print('PASS:',checks,'authored face/posture combinations with their own model and skeleton')
