# -*- coding: utf-8 -*-
# Генератор демо-уровня /Game/Levels/L_Demo из серых блоков. DefaultLevel только читается.
# Координаты в таблицах — метры (X на восток, Y на север, пол z=0). LevelBlock: пивот в мин. углу, размер = scale*100.
import unreal

TAG = 'NFB '


def log(m):
    unreal.log_warning(TAG + str(m))


eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary
LSU = unreal.EditorLoadingAndSavingUtils
MAP = '/Game/Levels/L_Demo'

LB = '/Game/LevelPrototyping/LevelBlock'
LBT = '/Game/LevelPrototyping/LevelBlock_Traversable'
LV = '/Game/LevelPrototyping/LevelVisuals'
TD = '/Game/LevelPrototyping/Teleporter_Destination'
TS = '/Game/LevelPrototyping/Teleporter_Sender'
BOTS = '/Game/Blueprints/Bots/CBP_Bot_'


def gp(obj, names):
    for n in names:
        try:
            return n, obj.get_editor_property(n)
        except Exception:
            pass
    return None, None


# ---------------------------------------------------------------- 1. образцы из DefaultLevel
LSU.load_map('/Game/Levels/DefaultLevel')
src = {'groups_block': {}, 'groups_trav': {}}
for a in eas.get_all_level_actors():
    cn = a.get_class().get_name()
    lab = a.get_actor_label()
    if cn in ('LevelBlock_C', 'LevelBlock_Traversable_C'):
        n, v = gp(a, ['Color Group', 'ColorGroup'])
        if lab == 'Floor':
            src['floor'] = {}
            for k in ['BaseMaterial', 'ColorGroup', 'AutoNameFromHeight', 'MaterialParams', 'UseLevelVisualsColor']:
                src['floor'][k] = gp(a, [k])[1]
            log('floor props %s' % src['floor'])
        elif n:
            d = src['groups_trav' if cn == 'LevelBlock_Traversable_C' else 'groups_block']
            d[str(v)] = v
            if 'cg_name' not in src:
                src['cg_name'] = n
                log('color group prop "%s" type %s value %s; auto name %s' % (n, type(v).__name__, v, gp(a, ['Auto Name from Height'])[1]))
    elif cn == 'Teleporter_Destination_C' and 'dest_prop' not in src:
        n, v = gp(a, ['Destination Name', 'DestinationName', 'Name'])
        src['dest_prop'] = n
        log('dest name prop "%s" type %s value %s' % (n, type(v).__name__, v))
    elif cn == 'Teleporter_Sender_C' and 'send_prop' not in src:
        n, v = gp(a, ['Destination'])
        src['send_prop'] = n
        log('sender prop "%s" -> %s' % (n, v.get_actor_label() if v else None))
    elif cn == 'LevelVisuals_C':
        for k in ['Style Index', 'StyleIndex', 'Floor']:
            n, v = gp(a, [k])
            if n:
                src['lv_' + k] = v
                log('levelvisuals %s = %s' % (k, v))
    elif cn == 'AmbientSound':
        try:
            src['ambient'] = a.get_editor_property('audio_component').get_editor_property('sound')
            log('ambient sound %s' % (src['ambient'].get_path_name() if src['ambient'] else None))
        except Exception as ex:
            log('ambient err %s' % ex)
    elif cn == 'NavMeshBoundsVolume':
        o, e = a.get_actor_bounds(False)
        log('src navvol scale %s ext %s' % (a.get_actor_scale3d(), e))
log('groups block %s trav %s' % (sorted(src['groups_block'].keys()), sorted(src['groups_trav'].keys())))

# ---------------------------------------------------------------- 2. новая пустая карта
if EAL.does_asset_exist(MAP):
    log('delete old %s -> %s' % (MAP, EAL.delete_asset(MAP)))
world = LSU.new_blank_map(False)
log('new world %s, actors %d' % (world.get_name(), len(eas.get_all_level_actors())))

_cls = {}


def bpc(path):
    if path not in _cls:
        _cls[path] = EAL.load_blueprint_class(path)
    return _cls[path]


def V(x, y, z=0.0):
    return unreal.Vector(x * 100.0, y * 100.0, z * 100.0)


def R(yaw=0.0):
    return unreal.Rotator(0.0, 0.0, yaw)


def place(a, label, folder):
    a.set_actor_label(label)
    a.set_folder_path('L_Demo/' + folder)
    return a


# Цветовые группы: стены, укрытия, реквизит — берём значения, которые реально есть в DefaultLevel.
gb = [src['groups_block'][k] for k in sorted(src['groups_block'].keys())] or [None]
gt = [src['groups_trav'][k] for k in sorted(src['groups_trav'].keys())] or gb
CG = src.get('cg_name')

blocks = []


def box(label, folder, x0, y0, x1, y1, h, z0=0.0, trav=False, group=0):
    a = eas.spawn_actor_from_class(bpc(LBT if trav else LB), V(x0, y0, z0), R())
    a.set_actor_scale3d(unreal.Vector(x1 - x0, y1 - y0, h))
    place(a, label, folder)
    # Пере-запуск construction script после масштаба: у Traversable из масштаба строятся уступы Ledge_1..4.
    if CG:
        pool = gt if trav else gb
        val = pool[group % len(pool)]
        if val is None:
            val = a.get_editor_property(CG)
        a.set_editor_property(CG, val)
    blocks.append((a, trav, (x1 - x0, y1 - y0, h)))
    return a


WALL, COVER, PROP = 0, 1, 2

# ---------------------------------------------------------------- 3. окружение
floor = box('Floor', 'Lighting', -1000, -1000, 1000, 1000, 1, z0=-1)
if 'floor' in src:
    for k, v in src['floor'].items():
        if v is not None:
            try:
                floor.set_editor_property(k, v)
            except Exception as ex:
                log('floor set %s err %s' % (k, ex))

lv = place(eas.spawn_actor_from_class(bpc(LV), V(0, 0, 0), R()), 'LevelVisuals', 'Lighting')
for k in ['Style Index', 'StyleIndex']:
    if ('lv_' + k) in src:
        lv.set_editor_property(k, src['lv_' + k])
if 'lv_Floor' in src:
    try:
        lv.set_editor_property('Floor', floor)
    except Exception as ex:
        log('lv floor err %s' % ex)

if src.get('ambient'):
    amb = place(eas.spawn_actor_from_class(unreal.AmbientSound, V(0, 0, 6), R()), 'AmbientSound', 'Lighting')
    amb.get_editor_property('audio_component').set_editor_property('sound', src['ambient'])

ps = place(eas.spawn_actor_from_class(unreal.PlayerStart, V(3, 0, 0.96), R(0)), 'PlayerStart', 'Z0_Start')

# ---------------------------------------------------------------- 4. навигация
navvols = []


def navvol(label, x0, y0, x1, y1, z0=-1.5, z1=1.5):
    a = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), R())
    place(a, 'Nav_' + label, 'Nav')
    o, e = a.get_actor_bounds(False)
    if e.x < 1:
        log('ERROR navvol %s has no brush (ext %s)' % (label, e))
        return a
    a.set_actor_scale3d(unreal.Vector((x1 - x0) * 50.0 / e.x, (y1 - y0) * 50.0 / e.y, (z1 - z0) * 50.0 / e.z))
    navvols.append((a, label, (x0, y0, x1, y1)))
    return a


navvol('S_South', -20, -45, 170, 71)        # старт, деревня, резерв, блокпост, рейдеры, сарай, загон, дорога
navvol('R_Ruins', 131, 91, 179, 134)        # остров стаи
navvol('N1_Yard', 141, 154, 169, 171)       # двор напарника
navvol('N2_RoadNorth', 149, 170, 162, 216)  # дорога к складу
navvol('N3_Warehouse', 131, 210, 172, 241)  # склад и обходной проулок
navvol('N4_RoadWest', 79, 221, 131, 234)    # дорога к арене
navvol('N5_Arena', 40, 210, 80, 245)        # арена финала
# Камеры 2x2 — свои острова. Навмеш коридора (связан с ареной) начинается в 17 м от камеры:
# монстр выбирает, куда бродить, в 9-15 м по курсу и в 7 м вокруг себя, так что спокойный наружу не выйдет.
navvol('C1_Cell', 10, 215, 12, 217)
navvol('C2_Cell', 10, 233.5, 12, 235.5)
navvol('C3_Cell', 59, 274.5, 61, 276.5)
navvol('C1_Corr', 28.4, 214, 40, 218)
navvol('C2_Corr', 28.4, 232.5, 40, 236.5)
navvol('C3_Corr', 58, 245, 62, 258)


def navlink(label, lx, ly, rx, ry):
    a = eas.spawn_actor_from_class(unreal.NavLinkProxy, V(lx, ly, 0), R())
    place(a, 'NavLink_' + label, 'Nav')
    link = unreal.NavigationLink()
    link.set_editor_property('left', unreal.Vector(0, 0, 20))
    link.set_editor_property('right', unreal.Vector((rx - lx) * 100.0, (ry - ly) * 100.0, 20))
    link.set_editor_property('direction', unreal.NavLinkDirection.LEFT_TO_RIGHT)
    a.set_editor_property('point_links', [link])
    return a


# Односторонние выходы из камер в коридоры: путь есть только наружу.
# Detour связывает концы линка только в соседних тайлах (тайл 52 клетки x 19 см = 9.88 м от нуля),
# поэтому камеры 1-2 в тайле X 9.88-19.76, конец линка в X 19.76-29.64; камера 3 в Y 266.76-276.64, конец в 256.88-266.76.
navlink('Cell1', 11, 216, 29.2, 216)
navlink('Cell2', 11, 234.5, 29.2, 234.5)
navlink('Cell3', 60, 275.5, 60, 257.4)

# ---------------------------------------------------------------- 5. телепорты
_n, dest_prop = None, src.get('dest_prop') or 'Destination Name'
send_prop = src.get('send_prop') or 'Destination'
dests = {}


def dest(key, text, x, y, z, yaw, folder):
    a = eas.spawn_actor_from_class(bpc(TD), V(x, y, z), R(yaw))
    place(a, 'TP_Dest_' + key, folder)
    try:
        a.set_editor_property(dest_prop, text)
    except Exception:
        a.set_editor_property(dest_prop, unreal.Text(text))
    dests[key] = a
    return a


senders = []


def sender(label, x, y, z, yaw, to, folder):
    a = eas.spawn_actor_from_class(bpc(TS), V(x, y, z), R(yaw))
    place(a, 'TP_' + label, folder)
    a.set_editor_property(send_prop, dests[to])
    senders.append((a, to))
    return a


dest('Start', 'START', -3, 0, 0, 0, 'Z0_Start')
dest('Ruins', 'RUINS', 145.5, 66, 0, 90, 'Z2d_Pen')
dest('Warehouse', 'WAREHOUSE', 156, 188, 0, 90, 'Z4_Warehouse')
dest('Finale', 'FINALE', 98, 227, 0, 180, 'Z5_Finale')

# ---------------------------------------------------------------- 6. геометрия зон
F = 'Z0_Start'
box('Start_Pad', F, -14, -7, -6, 7, 1, trav=True, group=PROP)
sender('Start_ToRuins', -10, -4, 1, 0, 'Ruins', F)
sender('Start_ToWarehouse', -10, 0, 1, 0, 'Warehouse', F)
sender('Start_ToFinale', -10, 4, 1, 0, 'Finale', F)

F = 'Z1_Village'
box('Village_House1', F, 8, 6, 13, 11, 3.5, group=WALL)
box('Village_House2', F, 12, -13, 17, -8, 3.5, group=WALL)
box('Village_House3', F, 24, 7, 29, 12, 3.5, group=WALL)
box('Village_House4', F, 24, -12, 29, -7, 3.5, group=WALL)
box('Village_FenceN', F, 2, 14, 20, 14.3, 1, trav=True, group=COVER)
box('Village_FenceS', F, 20, -15.3, 34, -15, 1, trav=True, group=COVER)
box('Village_FenceE', F, 31, 5, 31.3, 12, 1, trav=True, group=COVER)
box('Village_Crate1', F, 19, 2.5, 20, 3.5, 1, trav=True, group=PROP)
box('Village_Crate2', F, 33, -3.5, 34, -2.5, 1, trav=True, group=PROP)

F = 'Z1b_Reserve'
box('Reserve_Tent', F, 55, -21, 59, -18.5, 2.5, group=WALL)
box('Reserve_SandbagsW', F, 45.5, -19, 46.5, -12, 1, trav=True, group=COVER)
box('Reserve_SandbagsN', F, 48, -8, 53, -7, 1, trav=True, group=COVER)
box('Reserve_Crate', F, 51, -15.5, 52.5, -14, 1, trav=True, group=PROP)

F = 'Z2_Outpost'
box('Outpost_WallN', F, 84, 9, 98, 10, 2.5, group=WALL)
box('Outpost_WallS', F, 84, -10, 98, -9, 2.5, group=WALL)
box('Outpost_LowWallW1', F, 84, -9, 84.5, -4, 1, trav=True, group=COVER)
box('Outpost_LowWallW2', F, 84, 4, 84.5, 9, 1, trav=True, group=COVER)
box('Outpost_SandbagsE1', F, 97.5, -9, 98.5, -2, 1, trav=True, group=COVER)
box('Outpost_SandbagsE2', F, 97.5, 2, 98.5, 9, 1, trav=True, group=COVER)
box('Outpost_Crate', F, 86, -7.5, 87.5, -6, 1, trav=True, group=PROP)

F = 'Z2a_NoMansLand'
box('NML_BlockerA', F, 108, -4, 110, 4, 2.5, group=WALL)
box('NML_BlockerB', F, 115, 6, 117, 12, 2.5, group=WALL)
box('NML_BlockerC', F, 115, -12, 117, -6, 2.5, group=WALL)
box('NML_Cover1', F, 103, 5, 105, 6, 1, trav=True, group=COVER)
box('NML_Cover2', F, 103, -6, 105, -5, 1, trav=True, group=COVER)
box('NML_Cover3', F, 119.5, 0.5, 120.5, 3.5, 1, trav=True, group=COVER)
box('NML_Cover4', F, 120.5, -5.5, 121.5, -2.5, 1, trav=True, group=COVER)

F = 'Z2b_Raiders'
box('Raiders_Sandbags1', F, 125, -9, 126, -3, 1, trav=True, group=COVER)
box('Raiders_Sandbags2', F, 125, 3, 126, 9, 1, trav=True, group=COVER)
box('Raiders_Bunker', F, 140, -3, 143, 3, 2.5, group=WALL)
box('Raiders_Crate1', F, 131, 1, 132.5, 2.5, 1, trav=True, group=PROP)
box('Raiders_Crate2', F, 135, -9, 136, -8, 1, trav=True, group=PROP)

F = 'Z2c_Shed'
box('Shed_WallS1', F, 112, 15, 115, 15.5, 2.5, group=WALL)
box('Shed_WallS2', F, 117, 15, 120, 15.5, 2.5, group=WALL)
box('Shed_WallN', F, 112, 21.5, 120, 22, 2.5, group=WALL)
box('Shed_WallW', F, 112, 15.5, 112.5, 21.5, 2.5, group=WALL)
box('Shed_WallE', F, 119.5, 15.5, 120, 21.5, 2.5, group=WALL)
# Порог: игрок перелезает, а навмеш внутри сарая отрезан — мирные не уходят.
box('Shed_DoorSill', F, 115, 15.1, 117, 15.3, 1, trav=True, group=COVER)

F = 'Z2d_Pen'
box('Road_Crate1', F, 148, 28, 149, 29, 1, trav=True, group=PROP)
box('Road_Crate2', F, 141, 44, 142, 45, 1, trav=True, group=PROP)
box('Pen_WallS', F, 153, 50, 163, 50.5, 1.3, group=COVER)
box('Pen_WallN', F, 153, 61.5, 163, 62, 1.3, group=COVER)
box('Pen_WallW', F, 153, 50.5, 153.5, 61.5, 1.3, group=COVER)
box('Pen_WallE', F, 162.5, 50.5, 163, 61.5, 1.3, group=COVER)
box('Ruins_ReturnPad', F, 140, 62, 143, 65, 1, trav=True, group=PROP)
sender('Ruins_ToStart', 141.5, 63.5, 1, 0, 'Start', F)

F = 'Z3_Ruins'
box('Ruins_StepOutside', F, 143, 88.5, 147, 90, 1.2, trav=True, group=PROP)
box('Ruins_WallS', F, 130, 90, 180, 91, 2.5, trav=True, group=WALL)
box('Ruins_WallW', F, 130, 91, 131, 134, 3.5, group=WALL)
box('Ruins_WallE', F, 179, 91, 180, 134, 3.5, group=WALL)
box('Ruins_WallN1', F, 130, 134, 150, 135, 3.5, group=WALL)
box('Ruins_WallN_Exit', F, 150, 134, 160, 135, 2.5, trav=True, group=WALL)
box('Ruins_WallN2', F, 160, 134, 180, 135, 3.5, group=WALL)
box('Ruins_InnerL1', F, 138, 96, 139, 110, 3.5, group=WALL)
box('Ruins_InnerL2', F, 138, 110, 146, 111, 3.5, group=WALL)
box('Ruins_InnerMid', F, 150, 112, 160, 113, 2, group=WALL)
box('Ruins_LowWall1', F, 162, 104, 168, 104.5, 1, trav=True, group=COVER)
box('Ruins_LowWall2', F, 143, 121, 148, 121.5, 1, trav=True, group=COVER)
box('Ruins_InnerE', F, 170, 115, 171, 128, 3.5, group=WALL)
box('Ruins_Pillar', F, 160, 124, 162, 126, 3.5, group=WALL)
box('Ruins_Rubble', F, 155, 102, 156, 103, 1, trav=True, group=PROP)

F = 'Z3b_Yard'
box('Yard_WallS1', F, 140, 153, 153, 154, 3, group=WALL)
box('Yard_WallS2', F, 157, 153, 170, 154, 3, group=WALL)
box('Yard_WallN1', F, 140, 171, 153, 172, 3, group=WALL)
box('Yard_WallN2', F, 158, 171, 170, 172, 3, group=WALL)
box('Yard_WallW', F, 140, 154, 141, 171, 3, group=WALL)
box('Yard_WallE', F, 169, 154, 170, 171, 3, group=WALL)
box('House_WallS1', F, 150, 160, 154.5, 160.5, 3.5, group=WALL)
box('House_WallS2', F, 157.5, 160, 162, 160.5, 3.5, group=WALL)
box('House_WallN', F, 150, 167.5, 162, 168, 3.5, group=WALL)
box('House_WallW', F, 150, 160.5, 150.5, 167.5, 3.5, group=WALL)
box('House_WallE', F, 161.5, 160.5, 162, 167.5, 3.5, group=WALL)
box('House_Roof', F, 150, 160, 162, 168, 0.3, z0=3.5, group=WALL)
box('House_Crates', F, 151, 165.5, 152.5, 167, 1, trav=True, group=PROP)
box('Yard_Crate1', F, 144, 157, 146, 158, 1, trav=True, group=PROP)
box('Yard_Crate2', F, 164, 163, 166, 164, 1, trav=True, group=PROP)
box('RoadN_Cover1', F, 152, 185, 154, 186, 1, trav=True, group=COVER)
box('RoadN_Cover2', F, 157, 200, 159, 201, 1, trav=True, group=COVER)

F = 'Z4_Warehouse'
box('WH_ReturnPad', F, 162, 186, 165, 189, 1, trav=True, group=PROP)
sender('Warehouse_ToStart', 163.5, 187.5, 1, 0, 'Start', F)
box('WH_WallS1', F, 130, 215, 151, 216, 4, group=WALL)
box('WH_WallS2', F, 157, 215, 165, 216, 4, group=WALL)
box('WH_WallN', F, 130, 239, 165, 240, 4, group=WALL)
box('WH_WallW1', F, 130, 216, 131, 225, 4, group=WALL)
box('WH_WallW2', F, 130, 230, 131, 239, 4, group=WALL)
box('WH_WallE1', F, 164, 216, 165, 226, 4, group=WALL)
box('WH_WallE2', F, 164, 228, 165, 239, 4, group=WALL)
box('WH_AlleyWall', F, 172, 210, 173, 241, 4, group=WALL)
box('WH_AlleyEnd', F, 165, 240, 172, 241, 4, group=WALL)
box('WH_RoomE', F, 137.5, 232.5, 138, 239, 4, group=WALL)
box('WH_RoomS1', F, 131, 232, 134, 232.5, 4, group=WALL)
box('WH_RoomS2', F, 136, 232, 138, 232.5, 4, group=WALL)
box('WH_Shelf1', F, 136, 220, 144, 221, 2, group=WALL)
box('WH_Shelf2', F, 149, 220, 157, 221, 2, group=WALL)
box('WH_Shelf3', F, 140, 227, 148, 228, 2, group=WALL)
box('WH_Shelf4', F, 153, 227, 161, 228, 2, group=WALL)
box('WH_Crate1', F, 158, 223, 159.5, 224.5, 1, trav=True, group=PROP)
box('WH_Crate2', F, 145, 231, 146.5, 232.5, 1, trav=True, group=PROP)
box('WH_Crate3', F, 160, 232, 161.5, 233.5, 1, trav=True, group=PROP)
box('WH_Fallback1', F, 141, 233.5, 145, 234.5, 1, trav=True, group=COVER)
box('WH_Fallback2', F, 148, 233.5, 152, 234.5, 1, trav=True, group=COVER)
box('WH_Fallback3', F, 155, 233.5, 159, 234.5, 1, trav=True, group=COVER)

F = 'Z5_Finale'
box('RoadW_Cover1', F, 110, 224, 112, 225, 1, trav=True, group=COVER)
box('RoadW_Cover2', F, 118, 229, 120, 232, 2.5, group=WALL)
box('Finale_ReturnPad', F, 100, 230, 103, 233, 1, trav=True, group=PROP)
sender('Finale_ToStart', 101.5, 231.5, 1, 0, 'Start', F)
box('Arena_WallW1', F, 39, 209, 40, 215.25, 4, group=WALL)
box('Arena_WallW2', F, 39, 216.75, 40, 233.75, 4, group=WALL)
box('Arena_WallW3', F, 39, 235.25, 40, 246, 4, group=WALL)
box('Arena_WallE1', F, 80, 209, 81, 225, 4, group=WALL)
box('Arena_WallE2', F, 80, 230, 81, 246, 4, group=WALL)
box('Arena_WallS', F, 40, 209, 80, 210, 4, group=WALL)
box('Arena_WallN1', F, 40, 245, 59.25, 246, 4, group=WALL)
box('Arena_WallN2', F, 60.75, 245, 80, 246, 4, group=WALL)
box('Arena_Pillar1', F, 50, 218, 52, 220, 2.5, group=WALL)
box('Arena_Pillar2', F, 62, 234, 64, 236, 2.5, group=WALL)
box('Arena_Pillar3', F, 66, 214, 68, 216, 2.5, group=WALL)
box('Arena_Low1', F, 58, 224, 62, 224.5, 1, trav=True, group=COVER)
box('Arena_Low2', F, 45, 236, 49, 236.5, 1, trav=True, group=COVER)
box('Arena_Low3', F, 70, 226, 74, 226.5, 1, trav=True, group=COVER)
box('Arena_Low4', F, 54, 240, 55, 243, 1, trav=True, group=COVER)


def cell_w(n, cy):
    # Камера 2x2 к западу от арены, коридор 1.5 м на восток до стены арены (X 39).
    f = 'Z5_Finale/Cell%d' % n
    box('Cell%d_W' % n, f, 9, cy - 2, 10, cy + 2, 3, group=WALL)
    box('Cell%d_S' % n, f, 10, cy - 2, 12, cy - 1, 3, group=WALL)
    box('Cell%d_N' % n, f, 10, cy + 1, 12, cy + 2, 3, group=WALL)
    box('Cell%d_E1' % n, f, 12, cy - 2, 13, cy - 0.75, 3, group=WALL)
    box('Cell%d_E2' % n, f, 12, cy + 0.75, 13, cy + 2, 3, group=WALL)
    box('Cell%d_CorrS' % n, f, 13, cy - 1.75, 39, cy - 0.75, 3, group=WALL)
    box('Cell%d_CorrN' % n, f, 13, cy + 0.75, 39, cy + 1.75, 3, group=WALL)


cell_w(1, 216)
cell_w(2, 234.5)  # не 237: там коридор лёг бы вдоль границы тайлов навмеша и выпал
f = 'Z5_Finale/Cell3'
box('Cell3_N', f, 58, 276.5, 62, 277.5, 3, group=WALL)
box('Cell3_W', f, 58, 274.5, 59, 276.5, 3, group=WALL)
box('Cell3_E', f, 61, 274.5, 62, 276.5, 3, group=WALL)
box('Cell3_S1', f, 58, 273.5, 59.25, 274.5, 3, group=WALL)
box('Cell3_S2', f, 60.75, 273.5, 62, 274.5, 3, group=WALL)
box('Cell3_CorrW', f, 58.25, 246, 59.25, 273.5, 3, group=WALL)
box('Cell3_CorrE', f, 60.75, 246, 61.75, 273.5, 3, group=WALL)

# ---------------------------------------------------------------- 7. боты
ROLE = unreal.NoFearSquadRole
TEAM = unreal.NoFearTeam
bots = []


def bot(kind, label, folder, x, y, yaw, squad=None, role=None, pack=None, name=None, team=None, color=None, scale=None):
    a = eas.spawn_actor_from_class(bpc(BOTS + kind), V(x, y, 1.2), R(yaw))
    place(a, label, folder)
    if scale:
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    if squad:
        a.set_editor_property('squad', squad)
    if role is not None:
        a.set_editor_property('squad_role', role)
    if pack:
        a.set_editor_property('pack_name', pack)
    if name:
        try:
            a.set_editor_property('character_name', name)
        except Exception:
            a.set_editor_property('character_name', unreal.Text(name))
    if team is not None:
        a.set_editor_property('team', team)
    if color is not None:
        a.set_editor_property('apply_body_color', True)
        a.set_editor_property('body_color', color)
    cap = a.get_editor_property('capsule_component')
    hh = cap.get_scaled_capsule_half_height()
    a.set_actor_location(unreal.Vector(x * 100.0, y * 100.0, hh + 2.0), False, False)
    bots.append(a)
    return a


# Масштаб тел (прыгун 0.85, танк 1.3) — как у экземпляров в DefaultLevel, если в BP он не задан.
def kind_scale(kind):
    a = eas.spawn_actor_from_class(bpc(BOTS + kind), V(0, 0, -50), R())
    s = a.get_actor_scale3d().x
    col = None
    try:
        col = a.get_editor_property('body_color')
    except Exception:
        pass
    a.destroy_actor()
    return s, col


tank_s, _ = kind_scale('Tank')
jump_s, _ = kind_scale('Jumper')
_, aggro_col = kind_scale('AggroMonster')
log('bp scales tank %.2f jumper %.2f, aggro color %s' % (tank_s, jump_s, aggro_col))
TANK_S = None if abs(tank_s - 1.0) > 0.01 else 1.3
JUMP_S = None if abs(jump_s - 1.0) > 0.01 else 0.85

F = 'Z1_Village'
for i, (x, y, nm) in enumerate([(10, 3, 'Марта'), (15, -5, 'Олег'), (21, 9, 'Вера'), (26, -3, 'Степан'), (31, 9, 'Нина'), (6, -9, 'Гриша')]):
    bot('Civilian', 'Village_Civilian%d' % (i + 1), F, x, y, (i * 67) % 360, name=nm)

F = 'Z1b_Reserve'
bot('Ally', 'Reserve_Commander', F, 54, -16, 0, 'Reserve', ROLE.COMMANDER, name='Капитан Орлов')
bot('Ally', 'Reserve_Assault', F, 50, -12, 0, 'Reserve', ROLE.ASSAULT)
bot('Ally', 'Reserve_Rifleman1', F, 55, -11, 0, 'Reserve', ROLE.RIFLEMAN)
bot('Ally', 'Reserve_Rifleman2', F, 49, -17, 0, 'Reserve', ROLE.RIFLEMAN)

F = 'Z2_Outpost'
bot('Ally', 'Outpost_Commander', F, 88, 3, 0, 'Outpost', ROLE.COMMANDER, name='Лейтенант Громов')
bot('Ally', 'Outpost_Support', F, 92, -5, 0, 'Outpost', ROLE.SUPPORT)
bot('Ally', 'Outpost_Assault', F, 95, 4, 0, 'Outpost', ROLE.ASSAULT)
bot('Ally', 'Outpost_Rifleman', F, 95, -3, 0, 'Outpost', ROLE.RIFLEMAN)

F = 'Z2b_Raiders'
bot('Enemy', 'Raiders_Commander', F, 138, 0, 180, 'Raiders', ROLE.COMMANDER)
bot('Enemy', 'Raiders_Assault', F, 130, 5, 180, 'Raiders', ROLE.ASSAULT)
bot('Enemy', 'Raiders_Support', F, 132, -6, 180, 'Raiders', ROLE.SUPPORT)
bot('Enemy', 'Raiders_Rifleman1', F, 129, -2, 180, 'Raiders', ROLE.RIFLEMAN)
bot('Enemy', 'Raiders_Rifleman2', F, 134, 7, 180, 'Raiders', ROLE.RIFLEMAN)

F = 'Z2c_Shed'
bot('Civilian', 'Shed_Civilian1', F, 114, 19, 0, name='Пётр')
bot('Civilian', 'Shed_Civilian2', F, 118, 20, 200, name='Анна')

F = 'Z2d_Pen'
bot('AggroMonster', 'Pen_AggroMonster', F, 158, 56, 180)

F = 'Z3_Ruins'
bot('Monster', 'Ruins_Monster1', F, 155, 117, 270, pack='RuinsPack')
bot('Monster', 'Ruins_Monster2', F, 163, 120, 250, pack='RuinsPack')
bot('Jumper', 'Ruins_Jumper1', F, 150, 102, 270, pack='RuinsPack', scale=JUMP_S)
bot('Jumper', 'Ruins_Jumper2', F, 166, 110, 230, pack='RuinsPack', scale=JUMP_S)
bot('Spitter', 'Ruins_Spitter', F, 158, 127, 270, pack='RuinsPack')

F = 'Z3b_Yard'
bot('Companion', 'Companion_Max', F, 156, 164, 270, name='Макс')

F = 'Z4_Warehouse'
bot('Enemy', 'WH_Commander', F, 150, 236.5, 270, 'Warehouse', ROLE.COMMANDER)
bot('Enemy', 'WH_Support', F, 140, 225, 270, 'Warehouse', ROLE.SUPPORT)
bot('Enemy', 'WH_Assault', F, 158, 226, 270, 'Warehouse', ROLE.ASSAULT)
bot('Enemy', 'WH_Rifleman1', F, 135, 222, 270, 'Warehouse', ROLE.RIFLEMAN)
bot('Enemy', 'WH_Rifleman2', F, 152, 222.5, 270, 'Warehouse', ROLE.RIFLEMAN)
bot('Civilian', 'WH_Prisoner1', F, 133, 236, 0, name='Иван')
bot('Civilian', 'WH_Prisoner2', F, 135, 237, 180, name='Ольга')

F = 'Z5_Finale'
bot('Enemy', 'Finale_Commander', F, 48, 228, 0, 'Finale', ROLE.COMMANDER)
bot('Enemy', 'Finale_Support', F, 52, 222, 0, 'Finale', ROLE.SUPPORT)
bot('Enemy', 'Finale_Assault', F, 56, 231, 0, 'Finale', ROLE.ASSAULT)
bot('Enemy', 'Finale_Rifleman1', F, 46, 241, 0, 'Finale', ROLE.RIFLEMAN)
bot('Enemy', 'Finale_Rifleman2', F, 62, 219, 0, 'Finale', ROLE.RIFLEMAN)
bot('AggroMonster', 'Cell1_AggroMonster', 'Z5_Finale/Cell1', 11, 216, 0)
bot('AggroMonster', 'Cell2_AggroMonster', 'Z5_Finale/Cell2', 11, 234.5, 0)
bot('Tank', 'Cell3_AggroTank', 'Z5_Finale/Cell3', 60, 275.5, 270, team=TEAM.AGGRO_MONSTER,
    color=aggro_col if aggro_col is not None else unreal.LinearColor(0.45, 0.05, 0.75, 1.0), scale=TANK_S)

# ---------------------------------------------------------------- 8. проверки и сохранение
# Таблица длины сплайнов уступов сама не пересчитывается после масштаба из Python (в DefaultLevel она в мировых см).
for a, trav, _ in blocks:
    if trav:
        for s in a.get_components_by_class(unreal.SplineComponent):
            s.update_spline()
bad = 0
for a, trav, (sx, sy, h) in blocks:
    o, e = a.get_actor_bounds(True)
    want = unreal.Vector(sx * 50.0, sy * 50.0, h * 50.0)
    if abs(e.x - want.x) > 2 or abs(e.y - want.y) > 2 or abs(e.z - want.z) > 2:
        bad += 1
        if bad < 10:
            log('BOUNDS MISMATCH %s ext %s want %s' % (a.get_actor_label(), e, want))
    if trav:
        lens = sorted(round(s.get_spline_length()) for s in a.get_components_by_class(unreal.SplineComponent))
        exp = sorted([round(sx * 100), round(sx * 100), round(sy * 100), round(sy * 100)])
        if len(lens) != 4 or any(abs(p - q) > 5 for p, q in zip(lens, exp)):
            bad += 1
            log('LEDGE MISMATCH %s lens %s want %s' % (a.get_actor_label(), lens, exp))
log('blocks %d, problems %d' % (len(blocks), bad))
for a, lab, r in navvols:
    o, e = a.get_actor_bounds(False)
    log('nav %s origin (%.0f,%.0f,%.0f) ext (%.0f,%.0f,%.0f) want %s' % (lab, o.x, o.y, o.z, e.x, e.y, e.z, r))
for a, to in senders:
    d = a.get_editor_property(send_prop)
    log('sender %s -> %s' % (a.get_actor_label(), d.get_actor_label() if d else None))
for a in bots:
    log('bot %s %s team=%s squad=%s role=%s pack=%s name=%s scale=%.2f z=%.0f' % (
        a.get_actor_label(), a.get_class().get_name(), a.get_editor_property('team'), a.get_editor_property('squad'),
        a.get_editor_property('squad_role'), a.get_editor_property('pack_name'), a.get_editor_property('character_name'),
        a.get_actor_scale3d().x, a.get_actor_location().z))
for p in eas.get_all_level_actors():
    if isinstance(p, unreal.NavLinkProxy):
        for l in p.get_editor_property('point_links'):
            log('link %s at %s L %s R %s dir %s' % (p.get_actor_label(), p.get_actor_location(), l.get_editor_property('left'), l.get_editor_property('right'), l.get_editor_property('direction')))

ok = LSU.save_map(world, MAP)
log('SAVED %s -> %s, actors %d' % (MAP, ok, len(eas.get_all_level_actors())))
log('DONE')
