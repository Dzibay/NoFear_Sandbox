# -*- coding: utf-8 -*-
# T15: лечение и гибель (Docs/Inventory_Design.md, этап 4). Бинт лечит 3 с на +40, аптечка 6 с до полного; пока лечение идёт,
# герой ходит шагом и не стреляет; урон, спринт, ЛКМ, ПКМ, R, G, X, колесо предметов и экран кейса прерывают лечение,
# предмет остаётся; при полном здоровье лечиться нельзя. Лечение запускают колесо и пункт «Использовать» меню ПКМ.
# Регенерации нет. Гибель не возрождает: Enter через 1.5 с начинает L_Demo заново.
# Клавиши идут через NoFear.Inv.Key, как с клавиатуры; курсор ставится напрямую (в -nullrhi мыши нет).
import os
import time
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
A = unreal.NoFearCaseAction


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def item(name):
    return unreal.load_asset('/Game/Items/DA_Item_' + name)


def inv():
    return pawn.get_inventory_component()


def hp():
    return pawn.get_health_component().get_health()


def set_hp(v):
    pawn.get_health_component().set_health(v)


def press(name):
    cmd('NoFear.Inv.Key %s press' % name)


def release(name):
    cmd('NoFear.Inv.Key %s release' % name)


def key(name, hold=0.05):
    press(name)
    yield hold
    release(name)
    yield 0.05


def speed():
    v = pawn.get_velocity()
    return math.hypot(v.x, v.y)


push = [None]


def push_forward():
    if push[0] is not None:
        pawn.add_movement_input(push[0], 1.0, False)


per_tick.append(push_forward)


def sector(k, r=30.0):
    a = math.radians(k * 45.0)
    return unreal.Vector2D(math.sin(a) * r, math.cos(a) * r)


def slot_of(name):
    case = inv()
    for s in range(8):
        eid = case.get_wheel_slot(s)
        for e in case.get_entries():
            if e.get_editor_property('id') == eid and e.get_editor_property('item') == item(name):
                return s
    return -1


def entry_of(name):
    for e in inv().get_entries():
        if e.get_editor_property('item') == item(name):
            return e
    return None


def cell_center(x, y):
    return inv().get_cell_center(unreal.IntPoint(x, y))


def cursor(p):
    inv().set_screen_cursor(p)


def wounded(v=50.0):
    """Герой с v здоровья, без лечения и экранов."""
    case = inv()
    case.cancel_healing(False)
    if case.is_screen_open():
        case.close_screen()
    set_hp(v)


def interrupts(name, start, stop, hold=0.1, expect=50.0):
    """Генератор: лечение с 50 здоровья прерывается действием; предмет остаётся, здоровья не прибавилось (expect)."""
    case = inv()
    band = item('Bandage')
    n = case.count_item(band)
    wounded(50.0)
    started = case.start_healing(band)
    yield 0.5
    check('%s: healing runs' % name, started and case.is_healing(), 'progress %.2f' % case.get_healing_progress())
    start()
    yield hold
    stop()
    yield 0.3
    check('%s: interrupts' % name, not case.is_healing() and case.get_healing_progress() == 0.0, 'healing %s' % case.is_healing())
    check('%s: item stays' % name, case.count_item(band) == n and abs(hp() - expect) < 0.5, 'count %d/%d, hp %.1f' % (case.count_item(band), n, hp()))
    if case.is_screen_open():
        case.close_screen()
    yield 0.3


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    put_player(2, 0, 0)
    yield 0.5
    case = inv()
    wc = weapon()
    gc = pawn.get_grenade_component()
    band, med = item('Bandage'), item('Medkit')
    cmd('NoFear.Inv.Give Bandage 5')
    cmd('NoFear.Inv.Give Medkit 2')
    yield 0.2
    check('items given', case.count_item(band) == 6 and case.count_item(med) == 2, '%d %d' % (case.count_item(band), case.count_item(med)))
    check('asset values', abs(band.get_editor_property('use_time') - 3.0) < 1e-3 and abs(band.get_editor_property('heal_amount') - 40.0) < 1e-3
          and not band.get_editor_property('heal_to_full') and abs(med.get_editor_property('use_time') - 6.0) < 1e-3
          and med.get_editor_property('heal_to_full'), 'band %.1f/%.1f med %.1f' % (
              band.get_editor_property('use_time'), band.get_editor_property('heal_amount'), med.get_editor_property('use_time')))
    maxhp = pawn.get_health_component().get_max_health()

    # --- полное здоровье: лечиться нельзя, предмет цел
    set_hp(maxhp)
    check('full health refuses', not case.start_healing(band) and not case.is_healing() and case.count_item(band) == 6)

    # --- скорость бега без лечения
    put_player(2, 0, 0)
    push[0] = unreal.Vector(1.0, 0.0, 0.0)
    yield 1.5
    run_speed = speed()
    push[0] = None
    yield 0.8

    # --- бинт: идёт 3 с, герой идёт шагом, потом +40
    put_player(2, 0, 0)
    set_hp(30.0)
    wc.stop_fire()
    check('start bandage', case.start_healing(band) and case.is_healing() and case.get_healing_item() == band, '')
    check('second start refuses', not case.start_healing(med) and case.get_healing_item() == band)
    push[0] = unreal.Vector(1.0, 0.0, 0.0)
    yield 1.5
    walk_speed = speed()
    p = case.get_healing_progress()
    check('progress mid', 0.35 < p < 0.65 and hp() == 30.0 and case.count_item(band) == 6, 'progress %.2f hp %.1f' % (p, hp()))
    check('walks slowly', walk_speed > 20.0 and walk_speed < 0.7 * run_speed, 'run %.0f walk %.0f' % (run_speed, walk_speed))
    check('time remaining', 1.0 < case.get_healing_time_remaining() < 2.0, '%.2f' % case.get_healing_time_remaining())
    yield 1.8
    push[0] = None
    check('bandage heals 40', not case.is_healing() and abs(hp() - 70.0) < 0.5 and case.count_item(band) == 5, 'hp %.1f count %d' % (hp(), case.count_item(band)))
    yield 0.5

    # --- после лечения бег как прежде
    put_player(2, 0, 0)
    push[0] = unreal.Vector(1.0, 0.0, 0.0)
    yield 1.5
    after = speed()
    push[0] = None
    check('run restored', after > 0.8 * run_speed, 'run %.0f after %.0f' % (run_speed, after))
    yield 0.5

    # --- аптечка: 6 с до полного
    put_player(2, 0, 0)
    set_hp(10.0)
    check('start medkit', case.start_healing(med), '')
    yield 3.0
    check('medkit still healing', case.is_healing() and 0.4 < case.get_healing_progress() < 0.6 and abs(hp() - 10.0) < 0.5, 'progress %.2f hp %.1f' % (case.get_healing_progress(), hp()))
    yield 3.5
    check('medkit heals to full', not case.is_healing() and abs(hp() - maxhp) < 0.5 and case.count_item(med) == 1, 'hp %.1f/%.1f count %d' % (hp(), maxhp, case.count_item(med)))

    # --- здоровье стало полным во время лечения: прервать без траты
    set_hp(60.0)
    case.start_healing(band)
    yield 0.5
    set_hp(maxhp)
    yield 0.3
    check('healed elsewhere: cancelled', not case.is_healing() and case.count_item(band) == 5, 'count %d' % case.count_item(band))

    # --- прерывания
    yield from interrupts('damage', lambda: cmd('NoFearHurt 10'), lambda: None, expect=40.0)
    yield from interrupts('LMB', lambda: press('LeftMouseButton'), lambda: release('LeftMouseButton'))
    yield from interrupts('RMB', lambda: press('RightMouseButton'), lambda: release('RightMouseButton'))
    yield from interrupts('R', lambda: press('R'), lambda: release('R'))
    yield from interrupts('G', lambda: press('G'), lambda: release('G'))
    yield from interrupts('X', lambda: press('X'), lambda: release('X'))
    yield from interrupts('sprint', lambda: press('LeftShift'), lambda: release('LeftShift'), hold=0.3)
    yield from interrupts('wheel', lambda: press('Q'), lambda: release('Q'), hold=0.3)
    check('wheel closed', not case.is_item_wheel_open())
    yield from interrupts('screen', lambda: press('Tab'), lambda: release('Tab'), hold=0.1)

    # --- на бегу не начать
    wounded(50.0)
    press('LeftShift')
    yield 0.3
    check('sprint refuses start', not case.start_healing(band) and not case.is_healing() and case.count_item(band) == 5)
    release('LeftShift')
    yield 0.3

    # --- ЛКМ при лечении не стреляет: патроны в магазине те же
    wounded(50.0)
    mag = wc.get_current_weapon().get_magazine_ammo()
    case.start_healing(band)
    yield 0.3
    yield from key('LeftMouseButton', 0.3)
    yield 0.3
    check('LMB does not shoot', not case.is_healing() and wc.get_current_weapon().get_magazine_ammo() == mag,
          'magazine %d -> %d' % (mag, wc.get_current_weapon().get_magazine_ammo()))
    case.cancel_healing(False)

    # --- колесо: сектор с бинтом лечит
    wounded(50.0)
    s = slot_of('Bandage')
    check('bandage in wheel', s >= 0, 'slot %d' % s)
    press('Q')
    yield 0.1
    case.set_item_wheel_cursor(sector(s))
    yield 0.05
    release('Q')
    yield 0.3
    check('wheel starts healing', case.is_healing() and case.get_healing_item() == band, '')
    yield 3.2
    check('wheel heals', not case.is_healing() and abs(hp() - 90.0) < 0.5 and case.count_item(band) == 4, 'hp %.1f count %d' % (hp(), case.count_item(band)))

    # --- меню ПКМ: «Использовать»
    wounded(40.0)
    press('Tab')
    yield 0.1
    release('Tab')
    yield 0.3
    check('screen opens', case.is_screen_open())
    e = entry_of('Bandage')
    c = e.get_editor_property('cell')
    cursor(cell_center(c.x, c.y))
    yield from key('RightMouseButton')
    acts = list(case.get_menu_actions())
    check('menu has Use', case.is_menu_open() and A.USE in acts, str(acts))
    cursor(case.get_menu_item_center(acts.index(A.USE)))
    yield from key('LeftMouseButton')
    yield 0.3
    check('Use starts healing', case.is_healing() and not case.is_screen_open() and not case.is_menu_open(),
          'healing %s screen %s' % (case.is_healing(), case.is_screen_open()))
    yield 3.2
    check('Use heals', abs(hp() - 80.0) < 0.5 and case.count_item(band) == 3, 'hp %.1f count %d' % (hp(), case.count_item(band)))

    # --- оружие в руках для стрелковых пунктов меню остаётся без «Использовать»
    wounded(maxhp)
    press('Tab')
    yield 0.1
    release('Tab')
    yield 0.3
    pist = None
    for ent in case.get_entries():
        if ent.get_editor_property('weapon'):
            pist = ent
    if pist:
        c = pist.get_editor_property('cell')
        cursor(cell_center(c.x, c.y))
        yield from key('RightMouseButton')
        check('weapon menu has no Use', A.USE not in list(case.get_menu_actions()), str(list(case.get_menu_actions())))
    case.close_screen()
    yield 0.3

    # --- регенерации нет
    wounded(maxhp)
    cmd('NoFearHurt 30')
    yield 0.2
    low = hp()
    yield 12.0
    check('no regen', abs(hp() - low) < 0.01 and low < maxhp, 'hp %.1f -> %.1f' % (low, hp()))

    # --- гибель: без возрождения, Enter через 1.5 с начинает уровень заново
    old_time = now()
    check('can not restart alive', not pc.can_restart_after_death())
    pawn.get_health_component().kill()
    yield 0.5
    check('dead', pawn.get_health_component().is_dead())
    check('restart not yet', not pc.can_restart_after_death() and not pc.restart_after_death())
    yield from key('Enter')
    check('early Enter ignored', pawn.get_health_component().is_dead(), '')
    yield 4.0
    body = pc.get_controlled_pawn()
    check('no respawn', body is None or body == pawn, 'body %s' % (body.get_name() if body else None))
    check('restart prompt after delay', pc.can_restart_after_death() and pc.get_respawn_time_remaining() == 0.0,
          'remaining %.1f' % pc.get_respawn_time_remaining())

    # Enter перезагружает мир: w, pc и pawn сценария устаревают, дальше свой тик.
    unreal.unregister_slate_post_tick_callback(handle[0])
    finished[0] = True
    cmd('NoFear.Inv.Key Enter press')
    started = time.time()
    state = {'released': False}

    def after_restart(dt):
        try:
            if time.time() - started > 60.0:
                log('FAIL restart timeout')
                log('RESULT FAILED ' + ', '.join(fails + ['restart']))
                unreal.unregister_slate_post_tick_callback(state['h'])
                unreal.SystemLibrary.quit_game(w, pc, unreal.QuitPreference.QUIT, False)
                return
            w2 = unreal.load_object(None, '/Game/Levels/L_Demo.L_Demo')
            t = unreal.GameplayStatics.get_time_seconds(w2)
            pc2 = unreal.GameplayStatics.get_player_controller(w2, 0)
            pawn2 = pc2.get_controlled_pawn() if pc2 else None
            if t > 3.0 or pawn2 is None:
                return
            unreal.unregister_slate_post_tick_callback(state['h'])
            case2 = pawn2.get_inventory_component()
            h2 = pawn2.get_health_component()
            unreal.SystemLibrary.execute_console_command(w2, 'NoFear.Inv.Key Enter release', pc2)
            ok = (not h2.is_dead()) and abs(h2.get_health() - h2.get_max_health()) < 0.5 and case2.is_case_active() \
                and not pc2.can_restart_after_death()
            log('%s level restarted %s' % ('PASS' if ok else 'FAIL', 'world t=%.2f hp %.1f/%.1f entries %d' % (
                t, h2.get_health(), h2.get_max_health(), len(case2.get_entries()))))
            if not ok:
                fails.append('level restarted')
            log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))
            unreal.SystemLibrary.quit_game(w2, pc2, unreal.QuitPreference.QUIT, False)
        except Exception as ex:
            log('ERR %s %s' % (ex, traceback.format_exc().replace('\n', ' | ')))

    state['h'] = unreal.register_slate_post_tick_callback(after_restart)
    yield 100000.0


run(scenario)
