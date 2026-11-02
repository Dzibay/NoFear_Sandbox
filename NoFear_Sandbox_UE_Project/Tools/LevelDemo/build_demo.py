# -*- coding: utf-8 -*-
# Генератор демо-уровня /Game/Levels/L_Demo из серых блоков. DefaultLevel только читается.
# Координаты в таблицах — метры (X на восток, Y на север, пол z=0). LevelBlock: пивот в мин. углу, размер = scale*100.
import math
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
# Пол — четыре плиты вокруг подвала зоны Z6 (CELLAR): там пол тонкий, с люком.
CELLAR = (49.5, 38, 58, 44)


def floor_box(label, x0, y0, x1, y1):
    a = box(label, 'Lighting', x0, y0, x1, y1, 1, z0=-1)
    for k, v in src.get('floor', {}).items():
        if v is not None:
            try:
                a.set_editor_property(k, v)
            except Exception as ex:
                log('floor set %s err %s' % (k, ex))
    return a


cx0, cy0, cx1, cy1 = CELLAR
floor = floor_box('Floor', -1000, -1000, cx0, 1000)
floor_box('Floor_E', cx1, -1000, 1000, 1000)
floor_box('Floor_S', cx0, -1000, cx1, cy0)
floor_box('Floor_N', cx0, cy1, cx1, 1000)

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
navvol('Z6_Cellar', CELLAR[0], CELLAR[1], CELLAR[2], CELLAR[3], z0=-3.6, z1=1.5)  # подвал под люком
navvol('Z7_Tower', 93, 26, 101, 33, z0=1.5, z1=4.5)  # площадка вышки укрытия (3 м)


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

# ---------------------------------------------------------------- 6b. двери, люки, сундуки (Docs/Interactables.md)
doors = []


def door(label, folder, x, y, yaw, w=1.2, h=2.2, double=False, locked=False, hp=None, cls=None, start_open=False):
    # Точка двери — центр проёма; ось X двери — нормаль проёма (yaw 90 — проём в стене вдоль X).
    a = eas.spawn_actor_from_class(cls or unreal.NoFearDoor, V(x, y, h / 2), R(yaw))
    place(a, label, folder)
    a.set_editor_property('width', w * 100.0)
    a.set_editor_property('height', h * 100.0)
    a.set_editor_property('double_door', double)
    a.set_editor_property('locked', locked)
    a.set_editor_property('start_open', start_open)
    if cls is None:
        a.set_editor_property('open_angle', 90.0)  # в толстой стене створка дальше 90° уходит в откос
    if hp:
        a.set_editor_property('max_health', hp)
    doors.append(a)
    return a


def item(name):
    return EAL.load_asset('/Game/Items/DA_Item_%s.DA_Item_%s' % (name, name))


def chest(label, folder, x, y, yaw, contents, locked=False, z=0.0):
    a = eas.spawn_actor_from_class(unreal.NoFearChest, V(x, y, z), R(yaw))
    place(a, label, folder)
    items = []
    for name, count in contents:
        e = unreal.NoFearChestItem()
        e.set_editor_property('item', item(name))
        e.set_editor_property('count', count)
        items.append(e)
    a.set_editor_property('contents', items)
    a.set_editor_property('locked', locked)
    doors.append(a)
    return a


# Проёмы существующих зон: перемычка над дверью, чтобы над створками не было дыры.
# Открыта: герой в t4 начинает в доме и выходит с Максом.
door('Door_YardHouse', 'Z3b_Yard', 156, 160.25, 90, w=3, h=2.5, double=True, start_open=True)
box('House_Lintel', 'Z3b_Yard', 154.5, 160, 157.5, 160.5, 1, z0=2.5, group=WALL)
door('Door_WHPrisoners', 'Z4_Warehouse', 135, 232.25, 90, w=2, h=2.4, double=True)
box('WH_RoomLintel', 'Z4_Warehouse', 134, 232, 136, 232.5, 1.6, z0=2.4, group=WALL)
door('Door_WHEast', 'Z4_Warehouse', 164.5, 227, 0, w=2, h=2.4, double=True)
box('WH_EastLintel', 'Z4_Warehouse', 164, 226, 165, 228, 1.6, z0=2.4, group=WALL)

# Z6: испытательный дворик к востоку от деревни — запертая кладовая, подвал под люком.
F = 'Z6_Doors'
box('Vault_WallS1', F, 62, 36, 64, 36.3, 3, group=WALL)
box('Vault_WallS2', F, 65.2, 36, 67, 36.3, 3, group=WALL)
box('Vault_Lintel', F, 64, 36, 65.2, 36.3, 0.8, z0=2.2, group=WALL)
box('Vault_WallN', F, 62, 40.7, 67, 41, 3, group=WALL)
box('Vault_WallW', F, 62, 36.3, 62.3, 40.7, 3, group=WALL)
box('Vault_WallE', F, 66.7, 36.3, 67, 40.7, 3, group=WALL)
box('Vault_Roof', F, 62, 36, 67, 41, 0.2, z0=3, group=WALL)
vault_door = door('Door_VaultLocked', F, 64.6, 36.15, 90, locked=True, hp=200.0)
# Ключ от склада — на теле во дворе укрытия (Z7).
vault_door.set_editor_property('key_id', 'Vault')
chest('Chest_Vault', F, 65.5, 39.8, 270, [('RifleAmmo', 60), ('Grenade', 2), ('Medkit', 1)])
box('Shack_WallS', F, 68, 30, 72, 30.3, 3, group=WALL)
box('Shack_WallN1', F, 68, 33.7, 69.4, 34, 3, group=WALL)
box('Shack_WallN2', F, 70.6, 33.7, 72, 34, 3, group=WALL)
box('Shack_Lintel', F, 69.4, 33.7, 70.6, 34, 0.8, z0=2.2, group=WALL)
box('Shack_WallW', F, 68, 30.3, 68.3, 33.7, 3, group=WALL)
box('Shack_WallE', F, 71.7, 30.3, 72, 33.7, 3, group=WALL)
door('Door_Shack', F, 70, 33.85, 90)
chest('Chest_ShackLocked', F, 71, 31, 180, [('Shells', 16), ('Bandage', 2)], locked=True)
# Подвал: тонкий потолок с проёмом 3.4 x 1.4 м, пандус 38° вниз на -3 м (проём длинный, чтобы над пандусом было 2+ м).
HX0, HY0, HX1, HY1 = 50, 40, 53.4, 41.4
box('Cellar_CeilW', F, cx0, cy0, HX0, cy1, 0.2, z0=-0.2, group=WALL)
box('Cellar_CeilE', F, HX1, cy0, cx1, cy1, 0.2, z0=-0.2, group=WALL)
box('Cellar_CeilS', F, HX0, cy0, HX1, HY0, 0.2, z0=-0.2, group=WALL)
box('Cellar_CeilN', F, HX0, HY1, HX1, cy1, 0.2, z0=-0.2, group=WALL)
box('Cellar_Floor', F, cx0, cy0, cx1, cy1, 0.2, z0=-3.2, group=WALL)
box('Cellar_WallW', F, cx0, cy0, cx0 + 0.5, cy1, 2.8, z0=-3, group=WALL)
box('Cellar_WallE', F, cx1 - 0.5, cy0, cx1, cy1, 2.8, z0=-3, group=WALL)
box('Cellar_WallS', F, cx0 + 0.5, cy0, cx1 - 0.5, cy0 + 0.5, 2.8, z0=-3, group=WALL)
box('Cellar_WallN', F, cx0 + 0.5, cy1 - 0.5, cx1 - 0.5, cy1, 2.8, z0=-3, group=WALL)
RAMP_A, RAMP_T, RAMP_DROP = 38.0, 0.3, 3.0
ra = math.radians(RAMP_A)
ramp = eas.spawn_actor_from_class(bpc(LB), V(HX0 - RAMP_T * math.sin(ra), HY0, -RAMP_T * math.cos(ra)), unreal.Rotator(0.0, -RAMP_A, 0.0))
ramp.set_actor_scale3d(unreal.Vector(RAMP_DROP / math.sin(ra), HY1 - HY0, RAMP_T))
place(ramp, 'Cellar_Ramp', F)
if CG:
    ramp.set_editor_property(CG, gb[WALL % len(gb)] or ramp.get_editor_property(CG))
door('Hatch_Cellar', F, (HX0 + HX1) / 2, (HY0 + HY1) / 2, 0, w=HY1 - HY0, h=HX1 - HX0, double=True, cls=unreal.NoFearHatch)
doors[-1].set_actor_location(V((HX0 + HX1) / 2, (HY0 + HY1) / 2, 0), False, False)
chest('Chest_Cellar', F, 56.5, 42.3, 180, [('Rifle', 1), ('RifleAmmo', 30), ('CaseExpansion', 1)], z=-3.0)
lamp = eas.spawn_actor_from_class(unreal.PointLight, V(55, 41, -0.6), R())
place(lamp, 'Cellar_Lamp', F)
lc = lamp.get_component_by_class(unreal.PointLightComponent)
lc.set_mobility(unreal.ComponentMobility.MOVABLE)
lc.set_editor_property('intensity', 3000.0)
lc.set_editor_property('attenuation_radius', 700.0)

# Z7: укрытие — комната с дверью под баррикаду, проём с досками, шкафы-укрытия; коридор с растяжкой и капкан на подходе.
F = 'Z7_Shelter'
box('Shelter_WallS1', F, 80, 27, 82.4, 27.3, 3, group=WALL)
box('Shelter_WallS2', F, 83.6, 27, 86, 27.3, 3, group=WALL)
box('Shelter_GapLintel', F, 82.4, 27, 83.6, 27.3, 0.8, z0=2.2, group=WALL)
box('Shelter_WallN', F, 80, 32.7, 86, 33, 3, group=WALL)
# Восточная стена с окном: подоконник 0,9 м, окно до 2 м.
box('Shelter_WallE1', F, 85.7, 27.3, 86, 29.4, 3, group=WALL)
box('Shelter_WallE2', F, 85.7, 30.6, 86, 32.7, 3, group=WALL)
box('Shelter_WinSill', F, 85.7, 29.4, 86, 30.6, 0.9, group=WALL)
box('Shelter_WinTop', F, 85.7, 29.4, 86, 30.6, 1.0, z0=2.0, group=WALL)
box('Shelter_WallW1', F, 80, 27.3, 80.3, 29.4, 3, group=WALL)
box('Shelter_WallW2', F, 80, 30.6, 80.3, 32.7, 3, group=WALL)
box('Shelter_Lintel', F, 80, 29.4, 80.3, 30.6, 0.8, z0=2.2, group=WALL)
shelter_door = door('Door_Shelter', F, 80.15, 30, 0)
interact = []


def barricade(label, x, y, yaw, w=1.2, h=2.2, boards=0, linked=None):
    # Точка — середина проёма на полу, ось X — нормаль проёма.
    a = eas.spawn_actor_from_class(unreal.NoFearBarricade, V(x, y, 0), R(yaw))
    place(a, label, F)
    a.set_editor_property('width', w * 100.0)
    a.set_editor_property('height', h * 100.0)
    a.set_editor_property('initial_boards', boards)
    if linked:
        a.set_editor_property('linked_door', linked)
    interact.append(a)
    return a


# Доски у двери — изнутри, чтобы не задевать створку.
barricade('Barricade_ShelterDoor', 80.45, 30, 0, linked=shelter_door)
barricade('Barricade_ShelterGap', 83, 27.15, 90, boards=2)
for i, x in enumerate([81.6, 82.8]):
    # Шкаф: точка — середина основания, дверца смотрит по +X (yaw 270 — в комнату от северной стены).
    a = eas.spawn_actor_from_class(unreal.NoFearHideSpot, V(x, 32.7 - 0.33, 0), R(270))
    place(a, 'Locker_%d' % (i + 1), F)
    interact.append(a)
dark = eas.spawn_actor_from_class(unreal.NoFearDarkVolume, V(83, 30, 1.5), R())
place(dark, 'Shelter_Dark', F)
dark.set_editor_property('extent', unreal.Vector(300.0, 300.0, 200.0))
# Коридор с растяжкой: проволока натягивается до стен сама.
box('TrapCorr_WallS', F, 74, 23, 80, 23.3, 2.5, group=WALL)
box('TrapCorr_WallN', F, 74, 25.3, 80, 25.6, 2.5, group=WALL)
a = eas.spawn_actor_from_class(unreal.NoFearTripwire, V(77, 24.3, 0), R(0))
place(a, 'Trap_Tripwire', F)
interact.append(a)
a = eas.spawn_actor_from_class(unreal.NoFearBearTrap, V(76.5, 30, 0), R(0))
place(a, 'Trap_Bear', F)
interact.append(a)
# Окно в восточной стене, бочки и ящики во дворе за ним.
a = eas.spawn_actor_from_class(unreal.NoFearWindow, V(85.85, 30, 0), R(0))
place(a, 'Window_Shelter', F)
interact.append(a)
for i, (x, y) in enumerate([(88, 24), (88.8, 24.4), (88.3, 25.2), (93, 19)]):
    a = eas.spawn_actor_from_class(unreal.NoFearExplosiveBarrel, V(x, y, 0), R(i * 40))
    place(a, 'Barrel_%d' % (i + 1), F)
    interact.append(a)


def crate(label, x, y, yaw, contents):
    a = eas.spawn_actor_from_class(unreal.NoFearBreakableCrate, V(x, y, 0), R(yaw))
    place(a, label, F)
    items = []
    for name, count in contents:
        e = unreal.NoFearChestItem()
        e.set_editor_property('item', item(name))
        e.set_editor_property('count', count)
        items.append(e)
    a.set_editor_property('contents', items)
    interact.append(a)
    return a


crate('Crate_Planks', 90, 30, 10, [('Planks', 2)])
crate('Crate_Supplies', 91.2, 31.2, 35, [('PistolAmmo', 12), ('Bandage', 1)])
crate('Crate_Traps', 90.2, 32.4, 0, [('Tripwire', 1)])

# Генератор к северу от комнаты питает линию Shelter: две лампы в комнате, выключатель на западной стене южнее двери.
# Лампа над капканом без линии — горит всегда и мигает.
gen = eas.spawn_actor_from_class(unreal.NoFearGenerator, V(83, 34.6, 0), R(0))
place(gen, 'Generator_Shelter', F)
gen.set_editor_property('circuit', 'Shelter')
gen.set_editor_property('start_fuel_fraction', 0.5)
interact.append(gen)
shelter_lamps = []
for i, (x, y) in enumerate([(81.8, 30), (84.2, 30)]):
    a = eas.spawn_actor_from_class(unreal.NoFearLamp, V(x, y, 2.6), R())
    place(a, 'Lamp_Shelter_%d' % (i + 1), F)
    a.set_editor_property('circuit', 'Shelter')
    shelter_lamps.append(a)
a = eas.spawn_actor_from_class(unreal.NoFearLightSwitch, V(80.32, 28.6, 1.3), R(0))
place(a, 'Switch_Shelter', F)
a.set_editor_property('lamps', shelter_lamps)
interact.append(a)
a = eas.spawn_actor_from_class(unreal.NoFearLamp, V(76.5, 31.2, 2.4), R())
place(a, 'Lamp_Porch', F)
a.set_editor_property('flicker', True)
a.set_editor_property('radius', 500.0)

# Тело во дворе к северо-востоку от комнаты: при нём ключ от склада Z6 (Door_VaultLocked), карта лаборатории, патроны и бинт.
a = eas.spawn_actor_from_class(unreal.NoFearBody, V(87.6, 35.6, 0), R(200))
place(a, 'Body_Courier', F)
items = []
for name, count in [('Key_Vault', 1), ('KeyCard_Lab', 1), ('PistolAmmo', 8), ('Bandage', 1)]:
    e = unreal.NoFearChestItem()
    e.set_editor_property('item', item(name))
    e.set_editor_property('count', count)
    items.append(e)
a.set_editor_property('contents', items)
a.set_editor_property('display_name', 'Тело курьера')
interact.append(a)
# Радио на столе у крыльца — приманка: пока играет, монстры идут к нему.
box('Radio_Table', F, 77.1, 32.2, 77.9, 32.7, 0.75, group=PROP)
a = eas.spawn_actor_from_class(unreal.NoFearRadio, V(77.5, 32.45, 0.75), R(180))
place(a, 'Radio_Porch', F)
a.set_editor_property('broadcast', '«...всем, кто слышит: эвакуация с крыши больницы отменена. Повторяю, отменена...»')
interact.append(a)
# Сирена во дворе на линии Shelter: воет, только пока работает генератор.
a = eas.spawn_actor_from_class(unreal.NoFearSiren, V(92.5, 27.5, 0), R(180))
place(a, 'Siren_Yard', F)
a.set_editor_property('circuit', 'Shelter')
interact.append(a)

# Вышка к востоку от двора: площадка 3x3 м на высоте 3 м, на неё — лестница с запада и лифт с востока (линия Shelter).
box('Tower_Deck', F, 95, 28, 98, 31, 0.2, z0=2.8, group=WALL)
for i, (x, y) in enumerate([(95, 28), (97.8, 28), (95, 30.8), (97.8, 30.8)]):
    box('Tower_Post%d' % (i + 1), F, x, y, x + 0.2, y + 0.2, 2.8, group=WALL)
box('Tower_RailS', F, 95, 28, 98, 28.15, 1.0, z0=3.0, group=PROP)
box('Tower_RailN', F, 95, 30.85, 98, 31, 1.0, z0=3.0, group=PROP)
box('Tower_RailW1', F, 95, 28.15, 95.15, 29.1, 1.0, z0=3.0, group=PROP)
box('Tower_RailW2', F, 95, 29.9, 95.15, 30.85, 1.0, z0=3.0, group=PROP)
a = eas.spawn_actor_from_class(unreal.NoFearLadder, V(95, 29.5, 0), R(180))
place(a, 'Ladder_Tower', F)
a.set_editor_property('height', 300.0)
interact.append(a)
lift = eas.spawn_actor_from_class(unreal.NoFearLift, V(99.15, 29.5, 0), R())
place(lift, 'Lift_Tower', F)
lift.set_editor_property('circuit', 'Shelter')
interact.append(lift)
for label, (x, y, z), floor in [('LiftButton_Ground', (99.15, 27.7, 0), 0), ('LiftButton_Deck', (97.7, 30.6, 3.0), 1)]:
    a = eas.spawn_actor_from_class(unreal.NoFearLiftButton, V(x, y, z), R())
    place(a, label, F)
    a.set_editor_property('lift', lift)
    a.set_editor_property('floor', floor)
    interact.append(a)
# Забор по пояс вдоль x = 102 (y 22-38): в середине перелаз, его можно перелезть по E, монстры лезут следом.
box('Fence_S', F, 101.9, 22, 102.1, 28.5, 1.1, group=PROP)
box('Fence_N', F, 101.9, 31.5, 102.1, 38, 1.1, group=PROP)
a = eas.spawn_actor_from_class(unreal.NoFearVault, V(102, 30, 0), R())
place(a, 'Vault_Fence', F)
interact.append(a)

# Лаборатория к северу от вышки: дверь без ключа, её отпирает терминал у входа (линия Shelter). Войти — картой с тела
# курьера, паролем из записки на площадке вышки или взломом (держать E, пищит). Внутри сундук с припасами.
box('Lab_WallS1', F, 94, 34, 96.4, 34.3, 3, group=WALL)
box('Lab_WallS2', F, 97.6, 34, 100, 34.3, 3, group=WALL)
box('Lab_Lintel', F, 96.4, 34, 97.6, 34.3, 0.8, z0=2.2, group=WALL)
box('Lab_WallN', F, 94, 37.7, 100, 38, 3, group=WALL)
box('Lab_WallW', F, 94, 34.3, 94.3, 37.7, 3, group=WALL)
box('Lab_WallE', F, 99.7, 34.3, 100, 37.7, 3, group=WALL)
box('Lab_Roof', F, 94, 34, 100, 38, 0.2, z0=3, group=WALL)
lab_door = door('Door_Lab', F, 97, 34.15, 90, locked=True, hp=300.0)
chest('Chest_Lab', F, 98.8, 37.2, 270, [('Medkit', 1), ('RifleAmmo', 30), ('Grenade', 1)])
a = eas.spawn_actor_from_class(unreal.NoFearTerminal, V(95.3, 33.6, 0), R(-90))
place(a, 'Terminal_Lab', F)
a.set_editor_property('circuit', 'Shelter')
a.set_editor_property('key_id', 'Lab')
a.set_editor_property('code_id', 'LabCode')
a.set_editor_property('doors', [lab_door])
a.set_editor_property('title', 'Терминал лаборатории')
a.set_editor_property('text', 'Журнал доступа.\n\n03:12 — партия образцов перенесена в подвал склада.\n'
                      '03:40 — связь с больницей потеряна. Генератор во дворе барахлит, без него терминал не работает.\n'
                      '04:05 — замок двери лаборатории переведён на терминал. Ключей больше нет.')
interact.append(a)
a = eas.spawn_actor_from_class(unreal.NoFearNote, V(97.3, 28.6, 3.0), R(20))
place(a, 'Note_LabCode', F)
a.set_editor_property('title', 'Записка лаборанта')
a.set_editor_property('text', 'Если меня не будет — пароль от терминала лаборатории: 4-1-7-7.\n'
                      'Сначала заведи генератор. И не шуми у двери: они слышат писк.')
a.set_editor_property('code_id', 'LabCode')
interact.append(a)

# ---------------------------------------------------------------- 6c. предметы на земле (Docs/Inventory_Design.md)
# Герой стартует с пистолетом, 24 патронами и бинтом; остальное лежит по уровню. Пары «ствол и патроны к нему» лежат рядом,
# у ящиков и укрытий: места открытые, вне Z6/Z7 и дворика к востоку от деревни.
F = 'Z8_Items'
loose = []


def loose_item(name, x, y, count=0, yaw=0.0):
    # Падает на пол при старте (physics): точка на 30 см выше, чтобы не врасти в плиту.
    a = eas.spawn_actor_from_class(unreal.NoFearPickup, V(x, y, 0.3), R(yaw))
    place(a, 'Item_%s_%d' % (name, len(loose) + 1), F)
    a.set_item(EAL.load_asset('/Game/Items/DA_Item_%s.DA_Item_%s' % (name, name)), count, -1)
    loose.append(a)
    return a


# Деревня: первые патроны и бинты на пути от старта, дробовик у восточного ящика.
loose_item('PistolAmmo', 16.5, -2, 24)
loose_item('Bandage', 18, 3.2, 2, 30)
loose_item('Shotgun', 32.2, -3.4, 0, 100)
loose_item('Shells', 32.2, -4.6, 8, 20)
# Резерв: автомат с патронами у ящика, аптечка в палатке.
loose_item('Rifle', 50, -14.8, 0, 90)
loose_item('RifleAmmo', 49.9, -13.6, 30, 10)
loose_item('Medkit', 56.6, -17, 0, 0)
# Застава и рейдеры.
loose_item('Grenade', 88.8, -6.8, 2)
loose_item('Bandage', 133.5, 1.9, 2, 45)
loose_item('RifleAmmo', 133.5, 0.7, 30, 80)
# Руины и двор.
loose_item('Medkit', 154, 102.5)
loose_item('Grenade', 157.3, 102.5, 2)
loose_item('CaseExpansion', 155, 110.5)
loose_item('Shells', 147.4, 157.5, 12, 15)
loose_item('Bandage', 143, 157.5, 2, 70)
# Склад и арена.
loose_item('PistolAmmo', 160.6, 223.7, 24)
loose_item('Medkit', 156.8, 223.7)
loose_item('Grenade', 60, 222.8, 2)
loose_item('Bandage', 60, 226.4, 3, 12)
for a in loose:
    log('item %s %s at %s' % (a.get_actor_label(), a.get_editor_property('item').get_name(), a.get_actor_location()))

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
for a in doors:
    log('door %s %s at %s' % (a.get_actor_label(), a.get_class().get_name(), a.get_actor_location()))
for a in interact:
    log('interact %s %s at %s' % (a.get_actor_label(), a.get_class().get_name(), a.get_actor_location()))
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
