"""Hidden integration checks for named, independently movable equipment folders."""
import argparse, base64, hashlib, json, os, pathlib, shutil, subprocess
p=argparse.ArgumentParser()
p.add_argument('--runtime',type=pathlib.Path,required=True)
p.add_argument('--character',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--inspector',type=pathlib.Path,required=True)
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
logs=a.output/'diagnostics';logs.mkdir()
env={**os.environ,'SystemDrive':'C:', 'PATH':str(a.runtime)+os.pathsep+os.environ.get('PATH','')}
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
counter=0
def run(destination,slots,name='Battlelord test',as_set=True,expect='OK'):
    global counter
    counter+=1
    req=dict(version=1,slots=slots,bodies=[dict(race=4,sex=0),dict(race=4,sex=1)],asSet=as_set,setName=name,destination=str(destination))
    target=logs/str(counter)
    with target.with_suffix('.log').open('w') as log:
        result=subprocess.run([str(a.runtime/'wowmodelviewer.exe'),str(a.character),'-fbxexport',str(target),'-fbxequipmentpackage',base64.b64encode(json.dumps(req).encode()).decode(),'-fbxcomponent','-fbxanim','0'],cwd=a.runtime,env=env,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW,stdout=log,stderr=subprocess.STDOUT,timeout=600)
    status=target.with_suffix('.status').read_text()
    assert status.startswith(expect),status
    print(counter,name,status.strip(),flush=True)
    return status

def verify(folder):
    result = subprocess.run(['python',str(pathlib.Path(__file__).with_name('verify-package.py')),str(folder),'--inspector',str(a.inspector)],env=env,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW,capture_output=True,text=True)
    print(result.stdout, result.stderr, flush=True)
    result.check_returncode()

def hashes(folder):
    return {str(p.relative_to(folder)):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*') if p.is_file()}

out=a.output/'Druid';out.mkdir()
run(out,[0,1,2,3,5,6,7,8,10],'Battlelord_Plate')
children=list(out.iterdir());assert len(children)==2 and all(x.is_dir() for x in children)
assert (out/'Battlelord_Plate').is_dir()
weapon=next(x for x in children if x.name!='Battlelord_Plate')
assert weapon.name=='Kaldorei Moon Bow',weapon
for folder in children:verify(folder)
before=hashes(out)
run(out,[0,10],'Different set',expect='ERROR')
assert hashes(out)==before and not (out/'Different set').exists()
run(out,[0],'Battlelord_Plate',expect='ERROR');assert hashes(out)==before
for invalid in ['Bad:name','NUL','End.','End ','']:
    status=run(out,[0],invalid,expect='ERROR')
    assert hashes(out)==before
moved=a.output/'Moved';moved.mkdir()
for folder in children:
    target=moved/(folder.name+' relocated')
    shutil.move(str(folder),str(target));verify(target)
assert not list(out.iterdir())
run(out,[0,6],"Armadura del campeón")
assert (out/'Armadura del campeón').is_dir();verify(out/'Armadura del campeón')
individual=a.output/'Individual';individual.mkdir()
run(individual,[0,6,10],as_set=False)
assert len(list(individual.iterdir()))==3
for folder in individual.iterdir():verify(folder)
weapons=a.output/'Weapons';weapons.mkdir()
run(weapons,[10],name='Bad:name')
assert len(list(weapons.iterdir()))==1
verify(next(weapons.iterdir()))
for set_name in ['catalog.json', 'README.txt']:
    container=a.output/('Container-'+set_name);container.mkdir()
    run(container,[0,10],name=set_name)
    assert len(list(container.iterdir()))==2
    for folder in container.iterdir():verify(folder)
print('PASS: direct named folders, separate resources, relocation, collisions, validation, individual items, weapons-only',flush=True)
