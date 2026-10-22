import json,pathlib,sys,subprocess,os
root=pathlib.Path(sys.argv[1]); cat=json.loads((root/'catalog.json').read_text()); fbx=root/cat['fbx']
scene=json.loads(subprocess.check_output([sys.argv[2],str(fbx)],env={**os.environ,"SystemDrive":"C:"}))
nodes={n['name']:n for n in scene['nodes']}; assert len(nodes)==len(scene['nodes'])
textures={t['fileId']:t for t in cat['textures']}; choices={c['id'] for o in cat['options'] for c in o['choices']}; reqs={r['id']:r for r in cat['requirements']}; sets={b['id']:b for b in cat['boneSets']}; geo={(g['owner'],g['geoset']) for g in cat['geometry']}
def file(p):
 q=root/p; assert not pathlib.Path(p).is_absolute() and q.resolve().is_relative_to(root.resolve()) and q.is_file(),p; assert q.stat().st_size>0
for t in textures.values(): file(t['file']); assert (root/t['file']).read_bytes()[:8]==b'\x89PNG\r\n\x1a\n'
for g in cat['geometry']:
 n=nodes[g['node']]; assert n['materials'][g['materialIndex']]==g['material']; assert n['polygons']>0
for b in cat['bones']: assert nodes[b['node']]['skeleton']
for s in sets.values():
 for t in s['transforms']: assert nodes[t['node']]['skeleton'] and len(t['matrix'])==16
for o in cat['options']:
 assert not o['requirement'] or o['requirement'] in reqs
 for c in o['choices']:
  assert not c['requirement'] or c['requirement'] in reqs
  for e in c['effects']:
   assert not e['relatedChoice'] or e['relatedChoice'] in choices
   if 'texture' in e: assert e['texture']['fileId'] in textures
   if 'boneSet' in e: assert e['boneSet'] in sets
   for g in e.get('geosets',[]): assert g % 100 == 0 or ('base',g) in geo, (c['id'],g)
   if e.get('model') and e['variant']: assert ('customization:'+str(e['model']),e['group']*100+e['variant']) in geo
for d in cat['defaultSelection']: assert d['choice'] in choices
for t in scene['textures']: file(t['relative'])
for n in scene['nodes']:
 for b in n['clusters']: assert nodes[b]['skeleton']
mat=json.loads((root/cat['materialCatalog']).read_text());
for m in mat['materials']:
 for u in m['units']:
  if u['file']: file(u['file'])
  else: assert u['role']=='env'
assert len(scene['clips'])==len(cat['animationTakes'])
print(json.dumps(dict(package=root.name,options=len(cat['options']),choices=len(choices),textures=len(textures),geometry=len(cat['geometry']),bones=len(cat['bones']),boneSets=len(sets),clips=len(scene['clips']),equipment=len(cat['equipment']['items'])),indent=2))
