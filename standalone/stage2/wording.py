"""Apply retail-backed system message edits, the battle-item hints, and the opening objective."""
import struct
import sys
from pathlib import Path
UP=Path(__file__).resolve().parents[1]/'upstream/tools'
sys.path[:0]=[str(UP/'missions-messages'),str(UP)]
from formats import bar, directory_record, selected, require
from project import checked
import messages as msg

def text(s):
    punctuation={'.':b'\x4f','\n':b'\x02'}
    return b''.join(punctuation[c] if c in punctuation else msg.word(c) for c in s)

def build(clean):
    recipes={r['table']:r for r in msg.rows(msg.PATCH/'archives.tsv')}
    tables={}; archives={}
    for key,r in recipes.items():
        blob=checked(clean/r['package']/'original/msg/us'/f'{key}.bar',r['clean_sha256'])
        entries=bar(blob)
        t=selected(entries,0,2,key)
        tables[key]=msg.read_messages(t.data(blob))
        archives[key]=(blob,t,selected(entries,1,24,'md_m'))
    for key in ('sys','eh'):
        words=tables[key].copy()
        for k,op,arg in msg.edits(key):
            if op=='clean-id':
                source,i=arg.split('/')
                words[k]=tables[source][int(i,16)]
            elif op=='rename':
                old,new=map(msg.word,arg.split(' -> '))
                require(old in words[k],'rename source differs')
                words[k]=words[k].replace(old,new)
            elif op=='append-icon':
                words[k]=words[k][:-1]+bytes((1,9,int(arg,16),0))
        if key=='sys':
            for k,old,new in [(482,'Items','Item Slots'),(14093,'or use items in stock','or use items in stock.\nEquip items to use them in battle')]:
                require(words[k].count(text(old))==1,'stock item hint differs')
                words[k]=words[k].replace(text(old),text(new))
            # Keep the retail secret-ending control sequence and text after the difficulty description.
            before,control,after=words[20020].partition(b'\x02\x07')
            require(control and before.startswith(text('A true test of skill')), 'stock difficulty description differs')
            words[20020]=text('A merciless challenge. Enemies are more aggressive.\nRecovery items drain HP or MP.')+control+after
        blob,t,img=archives[key]
        packed=msg.pack_messages(words,recipes[key]['packing']=='intern-equal')
        data=bytearray(blob[:t.offset])+packed
        directory_record(data,0,t.offset,len(packed))
        end=(len(data)+15)&~15
        directory_record(data,1,end,img.size)
        if img.size:data+=bytes(end-len(data))+img.data(blob)
        yield 'msg/us/'+key+'.bar',bytes(data)
        if key=='sys':yield 'msg/us/sys.text',bytes(data)
