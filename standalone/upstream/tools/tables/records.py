"""Field layouts for the clean PC tables. All selectors are zero based."""
import struct

SCHEMAS = {}


def schema(name, fmt, fields):
    codec = struct.Struct('<' + fmt)
    names = fields.split() if isinstance(fields, str) else fields
    assert len(codec.unpack(bytes(codec.size))) == len(names)
    SCHEMAS[name] = codec, names


schema('object', 'IBBBB32s32sHBBHHfBBBB4H',
       'ObjectId Type Subtype DrawPriority WeaponJoint Model Animation Flags Target Padding NeoStatus NeoMoveset Weight SpawnLimiter Page Shadow Form Spawn1 Spawn2 Spawn3 Spawn4')
schema('puzzle', 'BBHBBBBH', 'Picture Part Text World Room JigsawIdWorld Unknown07 Unknown08')
schema('route', 'HH', 'ProgramId Area')
schema('sound', '12h', 'Reverb Rate EnvironmentWAV EnvironmentSEB EnvironmentNumber EnvironmentSpot FootstepWAV Sora Donald Goofy WorldFriend Other')
schema('condition', 'HH4BHH', 'Id Reward Type MaterialType MaterialRank ItemCollect Count ShopUnlock')
schema('cmd', 'HHhbBiIfff4BHHBBHHHHBB',
       'Id Execute Argument SubMenu Icon Message Flags Range Direction DirectionRange Cost Camera Priority Receiver Time Require Mark Action ReactionCount DistRange Score DisableForm Group Reserve')
schema('wmst', '32s', 'Moveset')
schema('world-directory', 'HH', 'Count Offset')
schema('area', 'Iiii16HHHB11s', 'Flags Reverb SEBank1 SEBank2 '.split()
       + [mode+str(i) for i in range(8) for mode in ('FieldBgm','BattleBgm')]
       + 'Voice NavigationMapItem Command Reserved'.split())
schema('item', 'H4B7HhBB', 'Id Type Flag0 Flag1 Rank Stat Name Description ShopBuy ShopSell Command Slot Picture Icon1 Icon2')
schema('stat', 'HH12B', 'Id Ability Attack Magic Defense AP Unknown8 Fire Ice Lightning Dark Unknown13 General Unknown15')
schema('treasure', 'HH4Bhh', 'Id ItemId Type World Room Chest EventId OverallChest')
schema('party', '3H10B18H', 'World Flag FlagNeg Command'.split()+['Unknown'+str(i) for i in range(7,16)]
       +'Player Friend1 Friend2 WorldCharacter Valor Wisdom Limit Master Final Anti Mickey PlayerHi ValorHi WisdomHi LimitHi MasterHi FinalHi AntiHi'.split())
schema('skeleton', 'IHH', 'Character PrimaryBone SecondaryBone')
schema('spawn', 'BBH', 'World Episode Destination')
schema('world-flag', 'BBHHHII', 'Byte0 Byte1 Word2 StoryFlag Word6 Unknown8 Pointer')
schema('link', '28HI', ['TargetCategory','TargetArgument']+[field+str(i) for i in range(1,14) for field in ('Category','Argument')]+['Required'])
schema('count32', 'I', 'Count')
schema('count16', 'H', 'Count')
schema('fmab', '17f', 'HighJumpHeight AirDodgeHeight AirDodgeSpeed AirSlideTime AirSlideSpeed AirSlideBrake AirSlideStopBrake AirSlideInvulnerableFrames GlideSpeed GlideFallRatio GlideFallHeight GlideFallMax GlideAcceleration GlideStartHeight GlideEndHeight GlideTurnSpeed DodgeRollInvulnerableFrames')
schema('prty', '13f', 'WalkSpeed RunSpeed JumpHeight TurnSpeed HangHeight HangMargin StunTime MpChargeTime UpDownSpeed DashSpeed Acceleration Brake Subjective')
# PC Magi and Sstm differ from published layouts. Only edited slots are interpreted.
schema('magi', '31f', [f'FloatAt{i*4}' for i in range(31)])
schema('sstm', '95I', [f'WordAt{i*4}' for i in range(95)])
schema('party-selection', '4B', 'Player Friend1 Friend2 WorldFriend')
