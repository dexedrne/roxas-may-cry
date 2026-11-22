"""Reconstruct the Marluxia and Larxene room timelines from retail event commands."""
import csv
import json
import struct
import sys
from collections import defaultdict
from functools import lru_cache
from pathlib import Path
from assets import apply_delta,digest
HERE=Path(__file__).resolve().parent
UP=HERE.parent/'upstream/tools'
sys.path[:0]=[str(UP/'rooms'),str(UP)]
import rooms
from formats import bar,require
from records import tokens,encode,label

class RetailRooms(rooms.Baseline):
    def __init__(self,game):
        self.game=game
        self.ledger=rooms.owned()

    @lru_cache(None)
    def archive(self,room):
        path=rooms.pathof(room)
        row=self.ledger[path]
        require(row['decision']=='base',f'missing retail room {room}')
        data=self.game.read(path,row['base_sha256'])
        return [(e.kind,rooms.name(e.name),e.link,e.data(data)) for e in bar(data)]

def build(game,cache):
    cache.mkdir()
    base=RetailRooms(game)
    pins=json.loads((HERE/'event-sources.json').read_text())
    @lru_cache(None)
    def commands(path,slot):
        data=game.read(path,pins[path])
        entry=bar(data)[slot]
        require(entry.kind==22,'retail event type mismatch')
        return tokens(22,entry.data(data))
    perf=defaultdict(list)
    for r in csv.DictReader((HERE/'events.tsv').open(),delimiter='\t'):
        seq=perf[r['room'],int(r['slot'])]
        require(len(seq)==int(r['command']),'retail event order mismatch')
        seq.append(commands(r['donor'],int(r['donor_slot']))[int(r['donor_command'])])
    for (name,slot),seq in perf.items():
        h=rooms.load_recipe(name)[slot]['performance_sha256']
        data=encode(22,seq)
        require(digest(data)==h,'retail timeline mismatch')
        (cache/h).write_bytes(data)

    @lru_cache(None)
    def member(name,i):
        row=rooms.load_recipe(name)[i]
        if row['op']=='reuse':
            area,ordinal=row['from'].split(':')
            data=member(area,int(ordinal))
        else:
            data=rooms.payload(row,base,cache,{})
        require(digest(data)==row['sha256'],f'room member differs {name}#{i}')
        return data
    specs=json.loads((HERE/'t100-rooms.json').read_text())
    for path,(_,_,base_pin,pin,patch) in specs.items():
        name=Path(path).stem
        rows=rooms.load_recipe(name)
        archive=bytearray(struct.pack('<4sIII',b'BAR\x01',len(rows),0,0)+bytes(16*len(rows)))
        for i,r in enumerate(rows):
            data=member(name,i)
            archive.extend(bytes(r['gap']))
            struct.pack_into('<HH4sII',archive,16+16*i,r['type'],r['link'],label(r['name']),len(archive),len(data))
            archive.extend(data)
        require(digest(archive)==base_pin,'room archive differs '+path)
        yield path,apply_delta(archive,HERE/patch),pin
