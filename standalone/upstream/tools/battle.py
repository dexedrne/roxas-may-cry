"""D1 battle row layouts. Addresses identify physical rows including repeated keys."""
import struct
from formats import require


def schema(*groups):
    fields = {}
    offset = 0
    for fmt, names in groups:
        for name in names.split():
            field, _, count = name.partition('*')
            for i in range(int(count or 1)):
                label = f'{field}[{i}]' if count else field
                fields[label] = (offset, '<' + fmt)
                offset += struct.calcsize('<' + fmt)
    return offset, fields


TABLES = {
    'atkp': schema(('H','SubId Id'),('B','Type CriticalAdjust'),('H','Power'),
        ('B','Team Element EnemyReaction EffectOnHit'),('h','KnockbackStrength1 KnockbackStrength2 Unknown'),
        ('B','Flags RefactSelf RefactOther ReflectedMotion'),('h','ReflectHitBack'),('i','ReflectAction ReflectHitSound'),
        ('H','ReflectRC'),('B','ReflectRange'),('b','ReflectAngle'),('B','DamageEffect Switch'),('H','Interval'),
        ('B','FloorCheck DriveDrain RevengeDamage AttackTrReaction ComboGroup RandomEffect Kind HpDrain')),
    'przt': schema(('H','Id'),('B','SmallHpOrbs BigHpOrbs BigMoneyOrbs MediumMoneyOrbs SmallMoneyOrbs SmallMpOrbs BigMpOrbs SmallDriveOrbs BigDriveOrbs Padding'),
        ('H','Item1'),('h','Item1Percentage'),('H','Item2'),('h','Item2Percentage'),('H','Item3'),('h','Item3Percentage')),
    'vtbl': schema(('B','CharacterId Id Priority Reserved'),('b',' '.join(f'Voice[{i}].{f}' for i in range(5) for f in ('VsbIndex','Weight')))),
    'bons': schema(('B','RewardId CharacterId HpIncrease MpIncrease DriveGaugeUpgrade ItemSlotUpgrade AccessorySlotUpgrade ArmorSlotUpgrade'),
        ('H','BonusItem1 BonusItem2'),('I','Padding')),
    'btlv': schema(('i','Id ProgressFlag'),('B','zz es tt di hb bb he al mu po lk lm dc wi nm wm ca tr eh Padding*5')),
    'enmp': schema(('H','Id Level'),('h','Health*32'),('H','MaxDamage MinDamage PhysicalWeakness FireWeakness IceWeakness ThunderWeakness DarkWeakness LightWeakness GeneralWeakness Experience Prize BonusLevel')),
    'plrp': schema(('H','Id'),('B','Character Hp Mp Ap Strength Magic Defense ArmorSlotMax AccessorySlotMax ItemSlotMax'),('H','Items*32'),('B','Padding*52')),
    'limt': schema(('B','Id Character Summon Group FileName*32'),('I','SpawnId'),('H','Command Limit World'),('B','Padding*18')),
    'magc': schema(('B','Id Level World Padding FileName*32'),('H','Item Command'),('h','GroundMotion GroundBlend'),('H','FinishMotion'),('h','FinishBlend AirMotion AirBlend'),('b','Voice VoiceFinisher VoiceSelf'),('B','Padding2')),
    'fmlv': schema(('B','Unk0 Unk1'),('H','Ability'),('i','Exp')),
    'lvup': schema(('i','Exp'),('B','Strength Magic Defense Ap'),('H','SwordAbility ShieldAbility StaffAbility Padding')),
    'ptya': schema(('B','Id Type'),('b','Sub ComboOffset'),('I','Flags'),('H','Motion NextMotion'),
        ('f','Jump JumpMax JumpMin SpeedMin SpeedMax Near Far Low High InnerMin InnerMax Blend DistanceAdjust'),('H','Ability Score')),
}


def row_offsets(name, data):
    stride, _ = TABLES[name]
    require(len(data) >= 8, 'short table')
    count, = struct.unpack_from('<I',data,4)
    if name == 'lvup':
        starts = struct.unpack_from('<' + 'I' * count,data,8)
        groups = [(i, p*4) for i,p in enumerate(starts) if p]
    elif name == 'ptya':
        starts = struct.unpack_from('<' + 'I' * count,data,8)
        groups = [(starts.index(p),p) for p in sorted(set(starts)-{0})]
    else:
        require(8 + count*stride <= len(data), f'truncated {name}')
        return {str(i):8+i*stride for i in range(count)}
    rows = {}
    for key,p in groups:
        n, = struct.unpack_from('<I',data,p)
        require(p+4+n*stride <= len(data), f'truncated {name} group {key}')
        for i in range(n):
            rows[f'{key}/{i}'] = p+4+i*stride
    return rows


def edit_table(name, data, recipe):
    require(set(recipe) <= {'set','clone_last_from','move_rows'}, f'unknown {name} recipe operation')
    result = bytearray(data)
    stride, fields = TABLES[name]
    if 'clone_last_from' in recipe:
        offsets = row_offsets(name,data)
        p = offsets[str(recipe['clone_last_from'])]
        require(8+len(offsets)*stride == len(data), 'cannot append behind tail')
        result.extend(data[p:p+stride])
        struct.pack_into('<I', result,4,len(offsets)+1)
    if 'move_rows' in recipe:
        require(name == 'ptya', 'move requires ptya')
        src,dst,n = recipe['move_rows']
        # Move the set-count boundary while retaining the physical sequence of attack records.
        count, = struct.unpack_from('<I',data,4)
        pointers = struct.unpack_from('<'+'I'*count,data,8)
        p,q = pointers[src],pointers[dst]
        a,b = struct.unpack_from('<I',data,p)[0],struct.unpack_from('<I',data,q)[0]
        require(q == p+4+a*stride and 0 < n < b, 'invalid adjacent sets')
        result[q:q+n*stride+4] = data[q+4:q+4+n*stride]+struct.pack('<I',b-n)
        struct.pack_into('<I',result,p,a+n)
        for i,v in enumerate(pointers):
            if v == q: struct.pack_into('<I',result,8+4*i,q+n*stride)
    offsets = row_offsets(name,result)
    for field, value, keys in recipe.get('set',[]):
        offset,fmt = fields[field]
        for key in keys.split(','):
            struct.pack_into(fmt,result,offsets[key]+offset,value)
    return bytes(result)
