# -*- coding: utf-8 -*-
# Общая обвязка прогонов L_Demo в -game. Сценарий задаёт scenario() и выполняет exec этого файла.
import unreal
import math
import traceback

TAG = 'NFT '
per_tick = []


def log(msg):
    unreal.log_warning(TAG + str(msg))


w = unreal.load_object(None, '/Game/Levels/L_Demo.L_Demo')
pc = unreal.GameplayStatics.get_player_controller(w, 0)
pawn = pc.get_controlled_pawn()
NAV = unreal.NavigationSystemV1
try:
    SQ = unreal.NoFearSquadSubsystem.get(w)
except Exception as e:
    SQ = None
    unreal.log_warning(TAG + 'no squad subsystem: %s' % e)


def now():
    return unreal.GameplayStatics.get_time_seconds(w)


def cmd(c):
    unreal.SystemLibrary.execute_console_command(w, c, pc)


def V(x, y, z=0.0):
    return unreal.Vector(x * 100.0, y * 100.0, z * 100.0)


def m2(v):
    return '(%.0f,%.0f)' % (v.x / 100.0, v.y / 100.0)


def god():
    h = pawn.get_health_component()
    h.set_editor_property('max_health', 100000.0)
    h.set_health(100000.0)


def put_player(x, y, yaw=None):
    pawn.set_actor_location(V(x, y, 0.95), False, True)
    if yaw is not None:
        pc.set_control_rotation(unreal.Rotator(0.0, 0.0, yaw))


def look_at(x, y, z=1.2):
    rot = unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location(), V(x, y, z))
    pc.set_control_rotation(rot)


def weapon():
    return pawn.get_weapon_component()


# ---------------------------------------------------------------- группы по месту появления
REGIONS = [
    ('village', 0, -20, 40, 20), ('reserve', 44, -25, 62, -5), ('outpost', 80, -12, 100, 12),
    ('raiders', 124, -12, 145, 12), ('shed', 112, 15, 120, 22), ('pen', 153, 50, 163, 62),
    ('ruins', 130, 90, 180, 135), ('yard', 140, 153, 170, 172), ('warehouse', 130, 210, 173, 241),
    ('arena', 40, 209, 81, 246), ('cells', 8, 212, 64, 278),
]


def region_of(v, regions=REGIONS):
    x, y = v.x / 100.0, v.y / 100.0
    for name, x0, y0, x1, y1 in regions:
        if x0 <= x <= x1 and y0 <= y <= y1:
            return name
    return '-'


bots = []  # (actor, group, kind, spawn)
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearCharacter):
    if a == pawn:
        continue
    kind = a.get_class().get_name().replace('CBP_Bot_', '').replace('_C', '')
    bots.append((a, region_of(a.get_actor_location()), kind, a.get_actor_location()))
groups = {}
for b in bots:
    groups.setdefault(b[1], []).append(b)


def dead(a):
    # Убитых ботов уничтожают: обращение к ним бросает исключение — считаем мёртвыми.
    try:
        h = a.get_health_component()
        return (h is None) or h.is_dead()
    except Exception:
        return True


def state(a):
    c = a.get_controller()
    if c is None:
        return 'noctl'
    s = []
    try:
        s.append(str(c.get_civilian_state()).split('.')[-1].split(':')[0])
    except Exception:
        pass
    try:
        s.append(str(c.get_alert_state()).split('.')[-1].split(':')[0])
    except Exception:
        pass
    try:
        t = c.get_current_target()
        if t:
            s.append('T=' + region_of(t.get_actor_location()) + ('P' if t == pawn else ''))
    except Exception:
        pass
    return '/'.join(s)


def group_line(g):
    out = []
    for a, grp, kind, sp in groups.get(g, []):
        if dead(a):
            out.append('%s:DEAD' % kind[:4])
            continue
        loc = a.get_actor_location()
        out.append('%s%s%s[%s]' % (kind[:4], m2(loc), '' if region_of(loc) == grp else '!' + region_of(loc), state(a)))
    return '%s: %s' % (g, ' '.join(out))


def report(gs, label=''):
    log('--- t=%.0f %s player%s' % (now() - T0, label, m2(pawn.get_actor_location())))
    for g in gs:
        log('  ' + group_line(g))


def squad_line(g):
    for a, grp, kind, sp in groups.get(g, []):
        if kind != 'Civilian' and not dead(a):
            try:
                return '%s squad: %s assist=%s helper=%s' % (g, SQ.get_squad_debug_text(a), SQ.get_assisting_squad(a), SQ.get_helper_squad(a))
            except Exception as e:
                return '%s squad err %s' % (g, e)
    return '%s squad: all dead' % g


def escaped(g, box):
    # Сколько живых из группы вне прямоугольника (метры).
    n = 0
    x0, y0, x1, y1 = box
    for a, grp, kind, sp in groups.get(g, []):
        if not dead(a):
            l = a.get_actor_location()
            if not (x0 <= l.x / 100.0 <= x1 and y0 <= l.y / 100.0 <= y1):
                n += 1
    return n


def dist_m(a, b):
    return math.hypot(a.x - b.x, a.y - b.y) / 100.0


def nearest_alive(gs, skip=('Civilian', 'Companion')):
    best, bd = None, 1e9
    pl = pawn.get_actor_location()
    for g in gs:
        for a, grp, kind, sp in groups.get(g, []):
            if kind in skip or dead(a):
                continue
            try:
                d = dist_m(a.get_actor_location(), pl)
            except Exception:
                continue
            if d < bd:
                best, bd = a, d
    return best, bd


def shoot(gs, secs=1.0):
    # Генератор: навестись на ближайшего живого из групп и дать очередь.
    a, d = nearest_alive(gs)
    if a is None:
        return
    l = a.get_actor_location()
    look_at(l.x / 100.0, l.y / 100.0, l.z / 100.0)
    weapon().start_fire()
    yield secs
    weapon().stop_fire()


# Ходьба игрока по точкам через ввод движения (как с клавиатуры).
route = []


def drive():
    if not route:
        return
    tx, ty = route[0]
    l = pawn.get_actor_location()
    dx, dy = tx * 100.0 - l.x, ty * 100.0 - l.y
    d = math.hypot(dx, dy)
    if d < 80.0:
        route.pop(0)
        return
    pawn.add_movement_input(unreal.Vector(dx / d, dy / d, 0.0), 1.0, False)


per_tick.append(drive)


def find_bot(kind):
    for a, grp, k, sp in bots:
        if k == kind:
            return a
    return None


def proj(x, y, ext=150.0):
    try:
        r = NAV.project_point_to_navigation(w, V(x, y, 0.5), None, None, unreal.Vector(ext, ext, 200.0))
    except Exception as e:
        return None
    if r is None:
        return None
    if isinstance(r, tuple):
        r = r[0]
    if abs(r.x) < 1 and abs(r.y) < 1 and abs(r.z) < 1:
        return None
    return r


def path(ax, ay, bx, by):
    a, b = proj(ax, ay), proj(bx, by)
    if a is None or b is None:
        return 'noproj(%s,%s)' % (a is not None, b is not None)
    p = NAV.find_path_to_location_synchronously(w, a, b)
    if p is None:
        return 'nopath'
    pts = p.get_editor_property('path_points')
    if not p.is_valid() or len(pts) == 0:
        return 'invalid'
    end = pts[-1]
    d = math.hypot(end.x - b.x, end.y - b.y) / 100.0
    return '%s len=%.0fm end%s miss=%.1fm' % ('PARTIAL' if p.is_partial() or d > 1.0 else 'ok', p.get_path_length() / 100.0, m2(end), d)


# ---------------------------------------------------------------- запуск генератора
T0 = now()
gen = None
wake = [0.0]
finished = [False]
handle = [None]
LIMIT = [200.0]


def finish():
    finished[0] = True
    log('QUIT')
    try:
        unreal.unregister_slate_post_tick_callback(handle[0])
    except Exception:
        pass
    unreal.SystemLibrary.quit_game(w, pc, unreal.QuitPreference.QUIT, False)



def tick(dt):
    if finished[0]:
        return
    try:
        for f in per_tick:
            f()
        if now() - T0 >= wake[0]:
            try:
                delay = next(gen)
                wake[0] = (now() - T0) + delay
            except StopIteration:
                finish()
        if now() - T0 > LIMIT[0]:
            log('TIMEOUT')
            finish()
    except Exception as e:
        log('ERR %s %s' % (e, traceback.format_exc().replace('\n', ' | ')))
        finish()


def run(scn):
    global gen
    log('bots %d groups %s' % (len(bots), {k: len(v) for k, v in groups.items()}))
    gen = scn()
    handle[0] = unreal.register_slate_post_tick_callback(tick)
