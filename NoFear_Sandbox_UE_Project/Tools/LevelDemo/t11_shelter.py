# -*- coding: utf-8 -*-
# T11: укрытие и оборона (Docs/Interactables.md, зона Z7_Shelter). Шкаф: E прячет героя, монстр его не видит, E выводит,
# обыск монстра выдёргивает героя наружу; баррикада: доски закрывают проём и дверь, монстр бьёт доски и ломает их;
# капкан держит наступившего монстра, потом отпускает и взводится снова; растяжка взрывается под монстром.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
LIMIT[0] = 200.0


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def nearest(cls, x, y, r=2.0):
    best, bd = None, 1e9
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, cls):
        l = a.get_actor_location()
        d = math.hypot(l.x / 100.0 - x, l.y / 100.0 - y)
        if d < bd:
            best, bd = a, d
    return best if bd < r else None


def alive(a):
    try:
        return a is not None and unreal.SystemLibrary.is_valid(a) and not a.is_actor_being_destroyed()
    except Exception:
        return False


def press_e():
    f = pc.get_focused_interactable()
    if f is None:
        return None, ''
    prompt = str(f.get_interaction_prompt(pawn))
    f.interact(pawn)
    return f.get_owner(), prompt


def spawn_bot(kind, x, y):
    before = set(a.get_name() for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearCharacter))
    cmd('NoFearSpawnBot %s 1' % kind)
    new = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearCharacter) if a.get_name() not in before]
    if not new:
        return None
    b = new[0]
    p = proj(x, y) or V(x, y, 1.0)
    b.set_actor_location(unreal.Vector(p.x, p.y, p.z + 100.0), False, True)
    b.get_controller().set_home_location(b.get_actor_location())
    return b


def puppet(b):
    # Дерево поведения стоит: бот идёт только приказом move_to_location.
    try:
        b.get_controller().get_editor_property('brain_component').stop_logic('t11')
    except Exception as e:
        log('stop_logic: %s' % e)


def human_path(ax, ay, bx, by):
    a, b = proj(ax, ay), proj(bx, by)
    p = NAV.find_path_to_location_synchronously(w, a, b, None, unreal.NoFearHumanNavFilter)
    if p is None or not p.is_valid():
        return 'none'
    end = p.get_editor_property('path_points')[-1]
    return 'ok' if not p.is_partial() and math.hypot(end.x - b.x, end.y - b.y) < 100.0 else 'partial'


def hp(b):
    return b.get_health_component().get_health()


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()

    lockers = list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearHideSpot))
    bars = list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearBarricade))
    locker = nearest(unreal.NoFearHideSpot, 81.6, 32.37)
    bdoor = nearest(unreal.NoFearBarricade, 80.45, 30)
    bgap = nearest(unreal.NoFearBarricade, 83, 27.15)
    sdoor = nearest(unreal.NoFearDoor, 80.15, 30)
    bear = nearest(unreal.NoFearBearTrap, 76.5, 30)
    wire = nearest(unreal.NoFearTripwire, 77, 24.3)
    log('lockers %d barricades %d' % (len(lockers), len(bars)))
    check('placed', len(lockers) == 2 and len(bars) == 2 and locker and bdoor and bgap and sdoor and bear and wire)
    check('initial boards', bgap.get_boards() == 2 and bdoor.get_boards() == 0, 'gap %d door %d' % (bgap.get_boards(), bdoor.get_boards()))
    check('wire stretched', abs(wire.get_wire_length() - 200.0) < 15.0, 'length %.0f' % wire.get_wire_length())

    # --- шкаф: E прячет, монстр рядом не видит, E выводит
    put_player(81.6, 31.0, 90)
    yield 0.6
    look_at(81.6, 32.37, 1.0)
    yield 0.3
    got, prompt = press_e()
    yield 0.6
    check('hide in locker', got == locker and locker.get_occupant() == pawn and unreal.NoFearHideSpot.is_actor_hidden(pawn),
          'got %s prompt %s' % (got.get_name() if got else None, prompt))
    mon = spawn_bot('Monster', 84.5, 29.0)
    yield 0.2
    puppet(mon)
    mon.get_controller().set_focus(locker)
    yield 3.0
    ctrl = mon.get_controller()
    check('monster does not see hidden hero', ctrl.get_current_target() != pawn and ctrl.get_awareness_of(pawn) < 0.5,
          'target %s awareness %.2f' % (ctrl.get_current_target(), ctrl.get_awareness_of(pawn)))
    got, prompt = press_e()
    yield 0.6
    check('exit by E', not locker.is_occupied() and not unreal.NoFearHideSpot.is_actor_hidden(pawn)
          and pawn.get_actor_location().y / 100.0 < 32.0, 'prompt %s player %s' % (prompt, m2(pawn.get_actor_location())))

    # --- обыск: монстр у шкафа выдёргивает героя
    check('enter by API', locker.enter_by(pawn))
    yield 0.5
    sp = locker.get_search_location()
    mon.set_actor_location(unreal.Vector(sp.x, sp.y, sp.z + 100.0), False, True)
    yield 0.3
    check('heard recently', locker.was_heard_recently())
    searched = locker.search_by(mon)
    yield 1.5
    check('search drags hero out', searched and not locker.is_occupied() and locker.get_search_count() == 1
          and not unreal.NoFearHideSpot.is_actor_hidden(pawn),
          'searched %s player %s' % (searched, m2(pawn.get_actor_location())))
    mon.destroy_actor()
    yield 2.0

    # --- баррикада у двери: доски закрывают дверь и навмеш
    sdoor.close_door(None)
    yield 1.0
    for i in range(3):
        bdoor.add_board(None)
    yield 1.0
    check('boards up', bdoor.get_boards() == 3 and bdoor.is_barricaded() and not bdoor.add_board(None), 'boards %d' % bdoor.get_boards())
    check('door held shut', not sdoor.open_door(pawn) and sdoor.is_closed())
    put_player(78.5, 30, 0)
    yield 0.5
    look_at(80.15, 30, 1.0)
    yield 0.3
    got, prompt = press_e()
    check('door prompt barricaded', 'Заколочено' in prompt, 'got %s prompt %s' % (got.get_name() if got else None, prompt))
    human = human_path(78.0, 30.0, 83.5, 30.5)
    check('room sealed for humans', human != 'ok', human)
    beast = path(78.0, 30.0, 83.5, 30.5)
    check('monsters path through boards', beast.startswith('ok'), beast)

    # --- монстр бьёт доски, пока цель за проёмом
    # Дверь открыта, доски прибиты заново: монстр упирается прямо в доски.
    for i in range(3):
        bdoor.remove_board(None)
    sdoor.open_door(None)
    yield 1.0
    for i in range(3):
        bdoor.add_board(None)
    check('door open behind boards', sdoor.is_open() and bdoor.get_boards() == 3, 'state %s boards %d' % (sdoor.get_door_state(), bdoor.get_boards()))
    put_player(84, 30.5, 180)
    yield 0.5
    mon = spawn_bot('Monster', 78.2, 28.4)
    yield 0.3
    mon.get_controller().report_threat(pawn)
    t0 = now()
    for i in range(70):
        yield 0.5
        god()
        if i % 6 == 0 and alive(mon) and not dead(mon):
            mon.get_controller().report_threat(pawn)
            log('  mon %s door %d (%.2f) att %d gap %d att %d' % (m2(mon.get_actor_location()), bdoor.get_boards(), bdoor.get_top_board_health_fraction(),
                                                             bdoor.get_monster_attack_count(), bgap.get_boards(), bgap.get_monster_attack_count()))
        if bdoor.get_boards() == 0 or bgap.get_boards() == 0:
            break
    attacks = bdoor.get_monster_attack_count() + bgap.get_monster_attack_count()
    check('monster attacks barricade', attacks > 0, 'attacks door %d gap %d' % (bdoor.get_monster_attack_count(), bgap.get_monster_attack_count()))
    check('monster breaks boards', bdoor.get_boards() == 0 or bgap.get_boards() == 0,
          'door %d gap %d after %.0fs' % (bdoor.get_boards(), bgap.get_boards(), now() - t0))
    yield 1.5
    if bdoor.get_boards() == 0:
        reopened = path(78.0, 30.0, 83.5, 30.5)
        check('path open after break', reopened.startswith('ok'), reopened)
    if alive(mon):
        mon.destroy_actor()
    yield 0.5

    # --- капкан: держит наступившего монстра, отпускает, взводится заново
    put_player(70, 22, 0)
    yield 0.3
    mon = spawn_bot('Monster', 73.5, 30)
    yield 0.3
    puppet(mon)
    hp0 = hp(mon)
    mon.get_controller().move_to_location(V(79.0, 30, 0), 30.0)
    caught = None
    for i in range(30):
        yield 0.2
        if bear.get_held_pawn() == mon:
            caught = now()
            break
    p0 = mon.get_actor_location()
    yield 2.0
    held_still = dist_m(mon.get_actor_location(), p0) < 0.3
    check('bear trap catches', caught is not None and bear.get_sprung_count() == 1 and hp(mon) < hp0 and not bear.is_armed(),
          'sprung %d hp %.0f -> %.0f mon %s' % (bear.get_sprung_count(), hp0, hp(mon), m2(mon.get_actor_location())))
    check('held in place', held_still, 'moved %.2fm' % dist_m(mon.get_actor_location(), p0))
    yield 2.6
    check('released', bear.get_held_pawn() is None, 'held %s' % bear.get_held_pawn())
    mon.get_controller().move_to_location(V(78.0, 27.5, 0), 30.0)
    yield 2.0
    check('walks after release', dist_m(mon.get_actor_location(), p0) > 0.8, 'mon %s' % m2(mon.get_actor_location()))
    check('rearm', bear.rearm(pawn) and bear.is_armed())
    mon.destroy_actor()
    yield 0.5

    # --- растяжка: монстр задевает проволоку, взрыв
    mon = spawn_bot('Monster', 74.8, 24.3)
    yield 0.3
    puppet(mon)
    hp0 = hp(mon)
    log('wire path %s move %s' % (path(74.8, 24.3, 79.3, 24.3), mon.get_controller().move_to_location(V(79.3, 24.3, 0), 30.0)))
    for i in range(30):
        if i % 5 == 0:
            log('  mon %s %s' % (m2(mon.get_actor_location()), mon.get_controller().get_move_status()))
        yield 0.2
        if not alive(wire):
            break
    yield 0.5
    check('tripwire explodes', not alive(wire), 'mon %s' % (m2(mon.get_actor_location()) if alive(mon) else 'gone'))
    check('explosion hurts', not alive(mon) or dead(mon) or hp(mon) < hp0, 'hp %.0f -> %.0f' % (hp0, hp(mon) if alive(mon) else -1))

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
