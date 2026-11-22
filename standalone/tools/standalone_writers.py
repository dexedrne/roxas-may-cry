"""Assemble selected clean and retail-member archives using the decomposition writers."""
import subprocess
import importlib.util
import json
import struct
import sys
from collections import defaultdict
from functools import lru_cache
from pathlib import Path
from standalone_sources import UPSTREAM as D, rows


def writer(name, script):
    path=D/'tools'/script
    sys.path.insert(0,str(path.parent))
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build(tmp, stage, clean, work, donor):
    sys.path.insert(0,str(D/'tools'))
    from project import catalog, digest, inventory
    from formats import bar, selected, directory_record
    pins=inventory()
    owners=catalog('docs/inventory/baseline.tsv')
    area=writer('stock_models','models-ui/area.py')
    base=area.clean_reader(clean)
    installed=stage/'installed'
    def emit(path,data):
        if digest(data)!=pins[path]['sha256']:
            raise ValueError('rebuilt output mismatch: '+path)
        target=installed/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(data)
    subprocess.run([sys.executable,'-B',D/'tools/tables/build.py','--clean',clean,'--out',tmp/'tables'],check=True)
    for p in sorted(work):
        if owners[p]['writer']=='tables':
            emit(p,(tmp/'tables'/p).read_bytes())
    ai=writer('stock_common','ai/common.py')
    spec=json.loads(ai.PATCH.read_text())
    emit(ai.NAME,ai.rebuild(base(spec['source']),spec))

    rm=writer('stock_rooms','rooms/rooms.py')
    baseline=rm.Baseline(clean)
    @lru_cache(maxsize=260)
    def room(name):
        return rm.load_recipe(name)
    @lru_cache(maxsize=512)
    def member(name,i):
        r=room(name)[i]
        if r['op']=='reuse':
            source,index=r['from'].split(':')
            data=member(source,int(index))
        else:
            data=rm.payload(r,baseline,tmp/'empty-cache',{})
        if digest(data)!=r['sha256']:
            raise ValueError(f'room input mismatch: {name}#{i}')
        return data
    for p in sorted(work):
        if owners[p]['writer']!='rooms':
            continue
        name=Path(p).stem
        recipe=room(name)
        data=bytearray(struct.pack('<4sIII',b'BAR\x01',len(recipe),0,0)+bytes(16*len(recipe)))
        for i,r in enumerate(recipe):
            body=member(name,i)
            data.extend(bytes(r['gap']))
            struct.pack_into('<HH4sII',data,16+16*i,r['type'],r['link'],rm.label(r['name']),len(data),len(body))
            data.extend(body)
        emit(p,data)

    missions=writer('stock_missions','missions-messages/missions.py').Missions()
    missions.layout={p:v for p,v in missions.layout.items() if p in work}
    missions.build(clean,tmp/'empty-cache',installed)
    msg=writer('stock_messages','missions-messages/messages.py')
    texts={}
    arch={}
    records=list(rows(msg.PATCH/'archives.tsv'))
    for r in records:
        name=r['table']
        data=base(r['package']+'/original/msg/us/'+name+'.bar')
        entries=bar(data)
        text=selected(entries,0,2,name)
        texts[name]=msg.read_messages(text.data(data))
        arch[name]=(data,text,selected(entries,1,24,'md_m'))
    for r in records:
        name=r['table']
        p='msg/us/'+name+'.bar'
        if p not in work:
            continue
        words=dict(texts[name])
        for k,op,arg in msg.edits(name):
            if op=='clean-id':
                table,ident=arg.split('/')
                words[k]=texts[table][int(ident,16)]
            elif op=='rename':
                old,new=map(msg.word,arg.split(' -> '))
                if old not in words[k]:
                    raise ValueError(f'message guard: {p}#{k}')
                words[k]=words[k].replace(old,new)
            else:
                raise ValueError('unknown clean-only message operation: '+op)
        original,text,img=arch[name]
        packed=msg.pack_messages(words,r['packing']=='intern-equal')
        data=bytearray(original[:text.offset])+packed
        directory_record(data,0,text.offset,len(packed))
        off=(len(data)+15)&~15
        directory_record(data,1,off,img.size)
        if img.size:
            data.extend(bytes(off-len(data))+img.data(original))
        emit(p,data)

    scripts=writer('stock_members','ai/members.py')
    ai_rows={(r['path'],r['ordinal']):r for r in rows(D/'patches/ai/members.tsv')}
    changes=json.loads((D/'patches/ai/members-edits.json').read_text())
    changes['D1-RAW']=json.loads((D/'patches/M_EX760_RAW.yml').read_text())
    edits=defaultdict(list)
    for r in rows(area.PATCH/'collision.tsv'):
        edits[r['sha256']].append(r)
    slots=defaultdict(list)
    for r in rows(area.PATCH/'placements.tsv'):
        slots[r['path']].append(r)
    for f in rows(area.PATCH/'files.tsv'):
        p=f['path']
        if p not in work:
            continue
        size=int(pins[p]['bytes'])
        if f['operation']=='clean':
            emit(p,base(f['source'])[:size])
            continue
        if f['operation']!='bar':
            raise ValueError('cache required: '+p)
        data=bytearray(size)
        struct.pack_into('<4sIII',data,0,b'BAR\x01',*map(int,f['source'].split(',')))
        for r in slots[p]:
            off,n=int(r['offset']),int(r['size'])
            slot=r['slot']
            if slot!='gap':
                for i in range(int(slot),int(slot)+int(r['repeat'])):
                    area.ENTRY.pack_into(data,16+i*16,int(r['kind']),int(r['link']),bytes.fromhex(r['raw_name']),off,n)
            if not n:
                continue
            src=r['source']
            if src=='ai':
                spec=ai_rows[p,slot]
                body=scripts.extract(base(spec['package']+'/original/'+spec['source_path']),int(spec['source_ordinal']))[3]
                if digest(body)!=spec['source_sha256']:
                    raise ValueError(f'AI input mismatch {p}#{slot}')
                if spec['operation']=='edit':
                    body=scripts.edit(body,changes[spec['edit']])
            elif src=='donor':
                body=donor(r['sha256'])
            elif src=='collision':
                e=edits[r['sha256']]
                start=int(e[0]['offset'])
                body=area.edit_collision(base(e[0]['clean_archive'])[start:start+int(e[0]['size'])],e)
            elif src=='alias':
                body=data[off:off+n]
            elif src.startswith('fill '):
                body=bytes([int(src[5:])])*n
            else:
                key,start=src.rsplit('@',1)
                start=int(start)
                body=base(f['baseline'] if key=='base' else key)[start:start+n]
            if len(body)!=n or digest(body)!=r['sha256']:
                raise ValueError(f'BAR input mismatch {p}#{slot}')
            data[off:off+n]=body
        emit(p,data)
