# -*- coding: utf-8 -*-
# T14: колесо предметов на Q (Docs/Inventory_Design.md, этап 3). Зажатая Q замедляет время до 0.3 и прячет мышь
# от камеры, мышь выбирает один из восьми секторов, отпущенная Q применяет его: ствол в руки, граната на G,
# медицина (лечение — t15), ловушка на пол; мышь в центре — отмена. Колёсико листает стволы колеса,
# стабилизация браслета — на B, пока колесо открыто, экран кейса, стрельба и колёсико заперты.
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
K = unreal.NoFearItemKind


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def item(name):
    return unreal.load_asset('/Game/Items/DA_Item_' + name)


def inv():
    return pawn.get_inventory_component()


def press(name):
    cmd('NoFear.Inv.Key %s press' % name)


def release(name):
    cmd('NoFear.Inv.Key %s release' % name)


def key(name, hold=0.05):
    press(name)
    yield hold
    release(name)
    yield 0.05


def dilation():
    return unreal.GameplayStatics.get_global_time_dilation(w)


def sector(k, r=30.0):
    a = math.radians(k * 45.0)
    return unreal.Vector2D(math.sin(a) * r, math.cos(a) * r)


def slot_item(case, s):
    eid = case.get_wheel_slot(s)
    for e in case.get_entries():
        if e.get_editor_property('id') == eid:
            return e.get_editor_property('item')
    return None


def layout(case):
    out = []
    for s in range(8):
        it = slot_item(case, s)
        out.append(it.get_name().replace('DA_Item_', '') if it else '-')
    return ' '.join(out)


def weapon_index(name):
    for i, wpn in enumerate(weapon().get_weapons()):
        if wpn.get_weapon_data().get_name() == name:
            return i
    return -1


def choose(case, k):
    """Генератор: зажать Q, навести мышь на сектор k (None — центр) и отпустить."""
    press('Q')
    yield 0.1
    case.set_item_wheel_cursor(sector(k) if k is not None else unreal.Vector2D(0.0, 0.0))
    yield 0.05
    release('Q')
    yield 0.1


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()
    put_player(2, 0, 0)
    yield 0.5
    case = inv()
    wc = weapon()
    gc = pawn.get_grenade_component()
    bracelet = pc.get_bracelet()
    rifle, pistol, shotgun = weapon_index('DA_Rifle'), weapon_index('DA_Pistol'), weapon_index('DA_Shotgun')

    # --- раскладка после набора: стволы по порядку, граната и бинт следом
    lay = layout(case)
    check('kit layout', lay.split()[:5] == ['Rifle', 'Pistol', 'Shotgun', 'Grenade', 'Bandage'], lay)
    check('rifle in hands', wc.get_target_weapon_index() == rifle, 'target %d' % wc.get_target_weapon_index())

    # --- Q открывает колесо: замедление, мышь не крутит камеру
    check('closed at start', not case.is_item_wheel_open() and abs(dilation() - 1.0) < 1e-3)
    press('Q')
    yield 0.1
    check('Q opens', case.is_item_wheel_open(), 'dilation %.2f' % dilation())
    check('slow time', abs(dilation() - 0.3) < 1e-3, '%.2f' % dilation())
    check('look ignored', pc.is_look_input_ignored())

    # --- выбор по направлению мыши: 0 — вверх, по часовой
    picks = []
    for k in range(8):
        case.set_item_wheel_cursor(sector(k))
        picks.append(case.get_item_wheel_selection())
    check('sectors by angle', picks == list(range(8)), str(picks))
    case.set_item_wheel_cursor(unreal.Vector2D(5.0, 5.0))
    check('dead zone', case.get_item_wheel_selection() == -1 and case.get_item_wheel_cursor().length() < 1e-3)
    case.set_item_wheel_cursor(unreal.Vector2D(1000.0, 0.0))
    cur = case.get_item_wheel_cursor()
    check('clamped cursor', case.get_item_wheel_selection() == 2 and abs(cur.x - 1.0) < 1e-3 and abs(cur.y) < 1e-3,
          '(%.2f, %.2f)' % (cur.x, cur.y))
    yield 0.1
    check('cursor kept between ticks', case.get_item_wheel_selection() == 2)

    # --- мышь в центре — отмена: ничего не применено, время идёт как раньше
    case.set_item_wheel_cursor(unreal.Vector2D(0.0, 0.0))
    yield 0.05
    release('Q')
    yield 0.1
    check('release closes', not case.is_item_wheel_open() and abs(dilation() - 1.0) < 1e-3 and not pc.is_look_input_ignored(),
          'dilation %.2f' % dilation())
    check('center cancels', wc.get_target_weapon_index() == rifle)
    check('Q does not stabilize', not bracelet.is_stabilizing())

    # --- стрельба: без колеса ЛКМ стреляет, при открытом колесе — нет
    yield 1.5
    mag0 = wc.get_current_weapon().get_magazine_ammo()
    yield from key('LeftMouseButton', 0.3)
    yield 0.2
    mag1 = wc.get_current_weapon().get_magazine_ammo()
    check('LMB fires without wheel', mag1 < mag0, '%d -> %d' % (mag0, mag1))
    press('Q')
    yield 0.1
    yield from key('LeftMouseButton', 0.3)
    yield 0.2
    mag2 = wc.get_current_weapon().get_magazine_ammo()
    check('LMB blocked on wheel', mag2 == mag1, '%d -> %d' % (mag1, mag2))

    # --- экран кейса и колесо не открываются вместе
    check('screen refuses on wheel', not case.open_screen() and not case.is_screen_open())
    yield from key('I')
    check('I blocked on wheel', not case.is_screen_open())
    yield from key('Tab')
    check('Tab blocked on wheel', not pc.is_order_wheel_open())
    yield from key('B')
    check('B blocked on wheel', not bracelet.is_stabilizing())
    release('Q')
    yield 0.1
    yield from key('I')
    check('screen opens', case.is_screen_open())
    press('Q')
    yield 0.1
    check('Q refuses on screen', not case.is_item_wheel_open())
    release('Q')
    yield from key('I')
    check('screen closed', not case.is_screen_open())

    # --- ствол из колеса в руки
    yield from choose(case, 1)
    check('wheel equips pistol', wc.get_target_weapon_index() == pistol, 'target %d' % wc.get_target_weapon_index())
    yield 1.5
    cw = wc.get_current_weapon()
    check('pistol in hands', cw is not None and cw.get_weapon_data().get_name() == 'DA_Pistol')
    check('same weapon is ok', case.use_wheel_slot(1) and wc.get_target_weapon_index() == pistol)

    # --- колёсико мыши: стволы колеса по порядку секторов
    check('cycle next', case.cycle_wheel_weapon(1) and wc.get_target_weapon_index() == shotgun)
    check('cycle wraps', case.cycle_wheel_weapon(1) and wc.get_target_weapon_index() == rifle)
    check('cycle prev', case.cycle_wheel_weapon(-1) and wc.get_target_weapon_index() == shotgun)
    yield 1.5
    t0 = wc.get_target_weapon_index()
    press('MouseWheelAxis')
    yield 0.0
    release('MouseWheelAxis')
    yield 0.2
    t1 = wc.get_target_weapon_index()
    check('mouse wheel switches', t1 != t0, '%d -> %d' % (t0, t1))
    yield 1.5
    press('Q')
    yield 0.1
    press('MouseWheelAxis')
    yield 0.0
    release('MouseWheelAxis')
    yield 0.2
    check('mouse wheel blocked on wheel', wc.get_target_weapon_index() == t1, '%d -> %d' % (t1, wc.get_target_weapon_index()))
    case.set_item_wheel_cursor(unreal.Vector2D(0.0, 0.0))
    release('Q')
    yield 0.1

    # --- гранаты: колесо выбирает, какую бросает G; кончились — берётся другая
    nade = item('Grenade')
    g2 = unreal.new_object(unreal.NoFearItemData)
    g2.set_editor_property('kind', K.GRENADE)
    g2.set_editor_property('display_name', unreal.Text('Тестовая граната'))
    g2.set_editor_property('max_stack', 3)
    g2.set_editor_property('grenade_data', nade.get_editor_property('grenade_data'))
    check('g2 added', case.add_item(g2, 2, -1) == 2)
    g2slot = -1
    for s in range(8):
        if slot_item(case, s) == g2:
            g2slot = s
    check('g2 in wheel', g2slot >= 0, layout(case))
    check('default grenade', gc.get_grenade_item() == nade and gc.get_grenade_count() == 3)
    yield from choose(case, g2slot)
    check('wheel picks g2', gc.get_grenade_item() == g2 and gc.get_grenade_count() == 2)
    put_player(2, 0, 0)
    yield 0.2
    check('throws g2', gc.throw_grenade_at(V(12, 0, 0)), '')
    yield 1.5
    check('g2 spent', case.count_item(g2) == 1 and case.count_item(nade) == 3,
          'g2 %d nade %d' % (case.count_item(g2), case.count_item(nade)))
    case.remove_item(g2, 1)
    yield 0.1
    check('falls back to grenade', gc.get_grenade_item() == nade and slot_item(case, g2slot) is None,
          '%s, slot %s' % (gc.get_grenade_item().get_name() if gc.get_grenade_item() else None, slot_item(case, g2slot)))
    yield 3.0

    # --- медицина: сектор запускает лечение, бинт тратится только по его завершении (подробно в t15)
    bslot = layout(case).split().index('Bandage')
    band = item('Bandage')
    n = case.count_item(band)
    h = pawn.get_health_component()
    h.set_health(h.get_health() - 50.0)
    yield from choose(case, bslot)
    check('bandage starts healing', case.is_healing() and case.get_healing_item() == band and case.count_item(band) == n,
          'healing %s, count %d' % (case.is_healing(), case.count_item(band)))
    case.cancel_healing(False)
    check('cancelled bandage stays', not case.is_healing() and case.count_item(band) == n)

    # --- ловушка из колеса на пол перед героем
    wire = item('Tripwire')
    cmd('NoFear.Inv.Give Tripwire 2')
    wslot = layout(case).split().index('Tripwire') if 'Tripwire' in layout(case).split() else -1
    check('wire in wheel', wslot >= 0, layout(case))
    put_player(2, 0, 0)
    yield 0.2
    before = len(list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearTripwire)))
    yield from choose(case, wslot)
    after = list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearTripwire))
    check('wheel places wire', len(after) == before + 1 and case.count_item(wire) == 1, 'wires %d, left %d' % (len(after), case.count_item(wire)))
    cmd('NoFear.Inv.Wheel %d' % (wslot + 1))
    yield 0.1
    check('console wheel places', case.count_item(wire) == 0 and slot_item(case, wslot) is None)
    for x in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearTripwire):
        x.destroy_actor()

    # --- пустой сектор: ничего
    empty = layout(case).split().index('-')
    t = wc.get_target_weapon_index()
    yield from choose(case, empty)
    check('empty sector does nothing', not case.use_wheel_slot(empty) and wc.get_target_weapon_index() == t)

    # --- B — стабилизация
    yield from key('B')
    check('B stabilizes', bracelet.is_stabilizing() or bracelet.get_stabilize_cooldown_remaining() > 0.0,
          'cooldown %.1f' % bracelet.get_stabilize_cooldown_remaining())

    check('closed at end', not case.is_item_wheel_open() and abs(dilation() - 1.0) < 1e-3)
    log('layout %s' % layout(case))
    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
