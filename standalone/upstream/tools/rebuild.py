"""Build the D1 installed files from guarded inputs."""
import struct
from formats import bar, selected, programs, event_commands, bdx_triggers, directory_record, require
from battle import edit_table
from project import checked, digest, recipe


def worldpoint(data,r):
    end = r['rows'] * 4
    pos = r['row'] * 4 + 1
    require(r['field'] == 'AreaId' and 0 <= pos < end <= len(data) and data[pos] == r['from'], 'worldpoint row guard')
    output = bytearray(data[:end])
    output[pos] = r['to']
    return bytes(output)


def battle(data,patch):
    entries = bar(data)
    result = bytearray(data[:16 + 16 * len(entries)])
    applied = set()
    for e in entries:
        member = e.data(data)
        name = e.name.rstrip(b'\0').decode()
        if name in patch:
            require(e.kind == 2 and not e.link and name not in applied,'battle member identity')
            member = edit_table(name,member,patch[name])
            applied.add(name)
        directory_record(result,e.ordinal,len(result),len(member))
        result.extend(member)
    require(applied == set(patch),'unknown battle member')
    return bytes(result)


def room(load,r):
    data = load(r['room'])
    entries = bar(data)
    event = selected(entries,*r['member'])
    requester = selected(entries,*r['requester'])
    require([i for i,_ in programs(requester.data(data))] == r['program_ids'], 'ARD requester ProgramId mismatch')
    other = load(r['project_room'])
    source = selected(bar(other),*r['project_member'])
    op,project = next(event_commands(source.data(other)))
    require(op == 0,'missing event project')
    schema = {'setup':(15,''),'start':(16,'I'),'end_frame':(8,'HH'),'fade':(18,'HHH')}
    timeline = bytearray()
    for command,*args in r['commands']:
        if command == 'borrow_project':
            op,payload = 0,project
        else:
            op,fmt = schema[command]
            payload = struct.pack('<'+fmt,*args)
        timeline.extend(struct.pack('<HH',4+len(payload),op)+payload)
    timeline.extend(bytes(2))
    require(event == entries[-1] and all(e.offset+e.size <= event.offset for e in entries[:-1]),'expected final timeline')
    output = bytearray(event.offset)
    end = 16 + 16*len(entries)
    output[:end] = data[:end]
    for e in entries[:-1]:
        output[e.offset:e.offset+e.size] = e.data(data)
    directory_record(output,event.ordinal,event.offset,len(timeline))
    return bytes(output+timeline)


def model(data,cache,r):
    entries = bar(data)
    ai = selected(entries,r['ai_ordinal'],3,'m_ex')
    script = bytearray(ai.data(data))
    require(digest(script) == r['ai_input_sha256'],'model BDX baseline mismatch')
    bdx_triggers(script)
    for group in r['word_edits']:
        for pc in group['at_word_pc']:
            pos = 16 + 2*pc
            require(struct.unpack_from('<H',script,pos)[0] == group['expect'], f'BDX word PC {pc}')
            struct.pack_into('<H',script,pos,group['write'])
    require(digest(script) == r['ai_output_sha256'], 'edited BDX fingerprint')
    members = {ai.ordinal:script}
    for donor in r['donors']:
        e = selected(entries,donor['ordinal'],donor['kind'],donor['name'],donor['link'])
        members[e.ordinal] = checked(cache/donor['sha256'],donor['sha256'])
    output = bytearray(data[:16+16*len(entries)])
    for e in entries:
        output.extend(bytes(-len(output) % r['member_alignment']))
        payload = members[e.ordinal]
        directory_record(output,e.ordinal,len(output),len(payload))
        output.extend(payload)
    return bytes(output)
