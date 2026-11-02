# -*- coding: utf-8 -*-
# T8: экран кейса на I (Docs/Inventory_Design.md, этап 2). Открытие с замедлением, ходьба без стрельбы, перенос щелчком
# и перетаскиванием, поворот, красная цель, разделение и доливка стопок, сектора колеса (1–8, перетаскивание, из сектора),
# меню ПКМ (взять в руки, выбросить), выброс за край, E заблокирована, предмет на курсоре при нехватке места.
# Клавиши идут через NoFear.Inv.Key, как с клавиатуры; курсор ставится напрямую (в -nullrhi мыши нет).
import os
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


def entries():
    return list(inv().get_entries())


def eid(e):
    return e.get_editor_property('id')


def ecell(e):
    c = e.get_editor_property('cell')
    return (c.x, c.y)


def describe():
    out = []
    for e in entries():
        it = e.get_editor_property('item')
        c = e.get_editor_property('cell')
        wpn = e.get_editor_property('weapon')
        out.append('#%d %s x%d (%d,%d)%s%s' % (
            eid(e), it.get_name().replace('DA_Item_', '') if it else '?', e.get_editor_property('count'),
            c.x, c.y, ' rot' if e.get_editor_property('rotated') else '', ' mag%d' % wpn.get_magazine_ammo() if wpn else ''))
    return ' '.join(out)


def all_of(name):
    return [e for e in entries() if e.get_editor_property('item') == item(name)]


def entry_of(name):
    found = all_of(name)
    return found[0] if found else None


def by_id(i):
    for e in entries():
        if eid(e) == i:
            return e
    return None


def pickups():
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearPickup))


def spawn(name, count, dist=150):
    before = set(p.get_name() for p in pickups())
    cmd('NoFear.Inv.Spawn %s %d %d' % (name, count, dist))
    new = [p for p in pickups() if p.get_name() not in before]
    return new[0] if new else None


def aim(p):
    loc = p.get_actor_location()
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location() + unreal.Vector(0, 0, 60), loc))


def press_e():
    f = pc.get_focused_interactable()
    if f is None:
        return None
    owner = f.get_owner()
    f.interact(pawn)
    return owner


def key(name, hold=0.05):
    """Нажать и отпустить клавишу; между ними и после — по кадру с запасом (время замедлено)."""
    cmd('NoFear.Inv.Key %s press' % name)
    yield hold
    cmd('NoFear.Inv.Key %s release' % name)
    yield 0.05


def cursor(p):
    inv().set_screen_cursor(p)


def cell_center(x, y):
    return inv().get_cell_center(unreal.IntPoint(x, y))


def grid_geometry():
    c00 = cell_center(0, 0)
    c10 = cell_center(1, 0)
    cell = c10.x - c00.x
    return c00.x - cell / 2.0, c00.y - cell / 2.0, cell


def held_cursor(x, y):
    """Точка курсора, при которой предмет на курсоре ляжет левым верхним углом в клетку (x, y)."""
    gx, gy, cell = grid_geometry()
    grab = inv().get_held_grab()
    return unreal.Vector2D(gx + (x + grab.x) * cell, gy + (y + grab.y) * cell)


def free_spot(it, rotated, ignore=0, avoid=()):
    size = inv().get_grid_size()
    for y in range(size.y):
        for x in range(size.x):
            if (x, y) not in avoid and inv().can_place_at(it, unreal.IntPoint(x, y), rotated, ignore):
                return (x, y)
    return None


def on(e):
    """Центр левой верхней клетки записи."""
    x, y = ecell(e)
    return cell_center(x, y)


def menu_click(action):
    acts = list(inv().get_menu_actions())
    if action not in acts:
        return False
    cursor(inv().get_menu_item_center(acts.index(action)))
    return True


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
    pistol = wc.get_current_weapon()
    log('start: %s' % describe())

    # --- открыть на I: замедление, курсор, камера стоит
    yield from key('I')
    dil = unreal.GameplayStatics.get_global_time_dilation(w)
    check('screen opens on I', case.is_screen_open())
    check('time slowed', abs(dil - 0.3) < 0.01, 'dilation %.2f' % dil)
    check('cursor shown, look ignored', pc.get_editor_property('show_mouse_cursor') and pc.is_look_input_ignored())

    # --- ЛКМ по пустой клетке не стреляет; ходить можно
    mag0 = pistol.get_magazine_ammo()
    fx, fy = free_spot(item('Bandage'), False)
    cursor(cell_center(fx, fy))
    yield from key('LeftMouseButton', 0.4)
    check('no fire while open', pistol.get_magazine_ammo() == mag0 and not case.is_holding(),
          'mag %d -> %d' % (mag0, pistol.get_magazine_ammo()))
    # Без окна (-nullrhi) при замедлении герой не двигается и с закрытым экраном, поэтому ходьбу
    # проверяем на обычной скорости: экран не должен глушить движение. В окне ходит и при 0.3.
    check('move not ignored', not pc.is_move_input_ignored())
    unreal.GameplayStatics.set_global_time_dilation(w, 1.0)
    p0 = pawn.get_actor_location()
    cmd('NoFear.Inv.Key W press')
    yield 0.8
    cmd('NoFear.Inv.Key W release')
    yield 0.3
    moved = dist_m(p0, pawn.get_actor_location())
    check('walk while open', moved > 0.3 and case.is_screen_open(), '%.2f m' % moved)
    unreal.GameplayStatics.set_global_time_dilation(w, 0.3)
    put_player(2, 0, 0)
    yield 0.1

    # --- перенос щелчком: взять бинт, щёлкнуть в свободную клетку
    band = entry_of('Bandage')
    bid = eid(band)
    cursor(on(band))
    yield from key('LeftMouseButton')
    check('click picks up', case.is_holding() and case.get_held_entry_id() == bid and case.get_held_item() == item('Bandage'))
    to = free_spot(item('Bandage'), False, bid, avoid=(ecell(band),))
    cursor(held_cursor(*to))
    yield from key('LeftMouseButton')
    check('click places', not case.is_holding() and ecell(by_id(bid)) == to, describe())

    # --- перетаскивание: зажать на патронах, увести, отпустить
    ammo = entry_of('PistolAmmo')
    aid = eid(ammo)
    cursor(on(ammo))
    cmd('NoFear.Inv.Key LeftMouseButton press')
    yield 0.05
    to = free_spot(item('PistolAmmo'), False, aid, avoid=(ecell(ammo),))
    cursor(held_cursor(*to))
    yield 0.05
    cmd('NoFear.Inv.Key LeftMouseButton release')
    yield 0.05
    check('drag places', not case.is_holding() and ecell(by_id(aid)) == to, describe())

    # --- красная цель: пистолет поверх патронов не кладётся; R поворачивает на курсоре
    pist = entry_of('Pistol')
    pid = eid(pist)
    cell0 = ecell(pist)
    cursor(on(pist))
    yield from key('LeftMouseButton')
    ax, ay = ecell(by_id(aid))
    cursor(held_cursor(max(ax - 1, 0), ay))
    yield from key('LeftMouseButton')
    check('blocked place keeps holding', case.is_holding() and ecell(by_id(pid)) == cell0)
    size0 = case.get_held_size()
    yield from key('R')
    size1 = case.get_held_size()
    check('R rotates held', case.is_held_rotated() and size1.x == size0.y and size1.y == size0.x,
          '%dx%d -> %dx%d' % (size0.x, size0.y, size1.x, size1.y))
    to = free_spot(item('Pistol'), True, pid)
    cursor(held_cursor(*to))
    yield from key('LeftMouseButton')
    pist = by_id(pid)
    check('rotated place', not case.is_holding() and pist.get_editor_property('rotated') and ecell(pist) == to, describe())

    # --- R под курсором поворачивает на месте, если есть место
    can = case.can_place_at(item('Pistol'), unreal.IntPoint(*ecell(pist)), False, pid)
    cursor(on(pist))
    yield from key('R')
    rot = by_id(pid).get_editor_property('rotated')
    check('R rotates in place', rot == (not can), 'room %s rotated %s' % (can, rot))

    # --- меню ПКМ: разделить патроны, положить половину отдельно, долить обратно
    cursor(on(by_id(aid)))
    yield from key('RightMouseButton')
    acts = list(case.get_menu_actions())
    check('menu opens', case.is_menu_open() and A.SPLIT in acts and A.DROP in acts and A.ASSIGN_WHEEL not in acts, str(acts))
    menu_click(A.SPLIT)
    yield from key('LeftMouseButton')
    check('split', case.is_holding_split() and case.get_held_count() == 12 and by_id(aid).get_editor_property('count') == 12,
          'held %d, left %d' % (case.get_held_count(), by_id(aid).get_editor_property('count')))
    to = free_spot(item('PistolAmmo'), False)
    cursor(held_cursor(*to))
    yield from key('LeftMouseButton')
    stacks = all_of('PistolAmmo')
    check('split placed', not case.is_holding() and len(stacks) == 2 and case.count_item(item('PistolAmmo')) == 24, describe())
    other = [e for e in stacks if eid(e) != aid][0]
    cursor(on(other))
    yield from key('LeftMouseButton')
    cursor(on(by_id(aid)))
    yield from key('LeftMouseButton')
    stacks = all_of('PistolAmmo')
    check('merge', not case.is_holding() and len(stacks) == 1 and stacks[0].get_editor_property('count') == 24, describe())

    # Разделить и вернуть: в свою же стопку щелчком и по Esc.
    cursor(on(by_id(aid)))
    yield from key('RightMouseButton')
    menu_click(A.SPLIT)
    yield from key('LeftMouseButton')
    cursor(on(by_id(aid)))
    yield from key('LeftMouseButton')
    check('split back by click', not case.is_holding() and by_id(aid).get_editor_property('count') == 24 and len(all_of('PistolAmmo')) == 1)
    yield from key('RightMouseButton')
    menu_click(A.SPLIT)
    yield from key('LeftMouseButton')
    yield from key('Escape')
    check('split back by Esc', case.is_screen_open() and not case.is_holding() and by_id(aid).get_editor_property('count') == 24)

    # --- колесо: 1–8 над предметом, перетаскивание в сектор, из сектора в сектор и за край
    # Бинт из стартового набора уже в колесе: сперва убираем, чтобы проверить и назначение, и снятие.
    if case.find_wheel_slot_of(bid) >= 0:
        case.clear_wheel_slot(case.find_wheel_slot_of(bid))
    cursor(on(by_id(bid)))
    yield from key('Two')
    check('key 2 assigns', case.get_wheel_slot(1) == bid)
    yield from key('Two')
    check('key 2 again clears', case.get_wheel_slot(1) == 0)
    cursor(on(by_id(aid)))
    yield from key('Three')
    check('ammo not in wheel', case.get_wheel_slot(2) == 0)
    band_cell = ecell(by_id(bid))
    cursor(on(by_id(bid)))
    yield from key('LeftMouseButton')
    cursor(case.get_wheel_slot_center(3))
    yield from key('LeftMouseButton')
    check('drag to sector', not case.is_holding() and case.get_wheel_slot(3) == bid and ecell(by_id(bid)) == band_cell)
    yield from key('LeftMouseButton')
    check('take from sector', case.is_holding() and case.get_held_wheel_slot() == 3)
    cursor(case.get_wheel_slot_center(5))
    yield from key('LeftMouseButton')
    check('sector to sector', case.get_wheel_slot(5) == bid and case.get_wheel_slot(3) == 0)
    n = len(pickups())
    yield from key('LeftMouseButton')
    cursor(case.get_outside_point())
    yield from key('LeftMouseButton')
    check('sector out clears only', case.get_wheel_slot(5) == 0 and case.count_item(item('Bandage')) == 1 and len(pickups()) == n)
    cursor(on(by_id(bid)))
    yield from key('Two')
    cursor(case.get_wheel_slot_center(1))
    yield from key('RightMouseButton')
    check('RMB on sector clears', case.get_wheel_slot(1) == 0 and not case.is_menu_open())

    # --- меню: взять в руки
    cmd('NoFear.Inv.Give Rifle')
    rifle_e = entry_of('Rifle')
    rifle = rifle_e.get_editor_property('weapon')
    cursor(on(rifle_e))
    yield from key('RightMouseButton')
    acts = list(case.get_menu_actions())
    check('weapon menu', acts[:1] == [A.EQUIP] and (A.ASSIGN_WHEEL in acts or A.CLEAR_WHEEL in acts) and A.SPLIT not in acts, str(acts))
    menu_click(A.EQUIP)
    yield from key('LeftMouseButton')
    yield 2.0
    check('equip from menu', wc.get_current_weapon() == rifle)

    # --- выброс перетаскиванием за край: ствол с магазином падает на землю
    n = len(pickups())
    cursor(on(entry_of('Rifle')))
    cmd('NoFear.Inv.Key LeftMouseButton press')
    yield 0.05
    cursor(case.get_outside_point())
    yield 0.05
    cmd('NoFear.Inv.Key LeftMouseButton release')
    yield 0.1
    new = [p for p in pickups() if p.get_editor_property('item') == item('Rifle')]
    check('drag out drops', entry_of('Rifle') is None and len(pickups()) == n + 1 and len(new) == 1 and rifle not in list(wc.get_weapons()),
          describe())

    # --- меню: выбросить
    cursor(on(by_id(bid)))
    yield from key('RightMouseButton')
    menu_click(A.DROP)
    yield from key('LeftMouseButton')
    check('menu drop', case.count_item(item('Bandage')) == 0 and len(pickups()) == n + 2, describe())

    # --- Esc закрывает: время, курсор, камера
    yield from key('Escape')
    dil = unreal.GameplayStatics.get_global_time_dilation(w)
    check('Esc closes', not case.is_screen_open() and abs(dil - 1.0) < 0.01 and not pc.get_editor_property('show_mouse_cursor')
          and not pc.is_look_input_ignored(), 'dilation %.2f' % dil)
    for q in pickups():
        q.destroy_actor()

    # --- E заблокирована, пока экран открыт
    p = spawn('PistolAmmo', 10)
    yield 1.5
    aim(p)
    yield 0.3
    yield from key('I')
    yield from key('E')
    check('E blocked while open', p in pickups() and case.count_item(item('PistolAmmo')) == 24)
    yield from key('I')
    check('I closes', not case.is_screen_open())
    yield from key('E')
    yield 0.2
    check('E works after close', p not in pickups() and case.count_item(item('PistolAmmo')) == 34)

    # --- нет места: экран с предметом на курсоре
    case.clear_case()
    cmd('NoFear.Inv.Give Shells %d' % (24 * 40))
    p = spawn('Medkit', 1)
    yield 1.5
    aim(p)
    yield 0.3
    got = press_e()
    yield 0.1
    check('no room opens screen', got == p and case.is_screen_open() and case.is_holding_pickup() and case.get_held_item() == item('Medkit')
          and p in pickups())
    cursor(held_cursor(3, 2))
    yield from key('LeftMouseButton')
    check('no room red', case.is_holding_pickup() and p in pickups())
    yield from key('One')
    check('pickup not to wheel', case.get_wheel_slot(0) == 0)
    yield from key('Escape')
    check('Esc leaves on ground', case.is_screen_open() and not case.is_holding() and p in pickups())
    yield from key('Escape')
    press_e()
    yield 0.1
    put_player(9, 0)
    yield 0.2
    check('walk away releases', case.is_screen_open() and not case.is_holding() and p in pickups())
    yield from key('Escape')
    put_player(2, 0)
    yield 0.3
    aim(p)
    yield 0.3
    press_e()
    yield 0.1
    # Освободить 2×2 в углу и положить туда аптечку.
    for c in [(0, 0), (1, 0), (0, 1), (1, 1)]:
        case.drop_entry(case.get_entry_at(unreal.IntPoint(*c)), 0)
    cursor(held_cursor(0, 0))
    yield from key('LeftMouseButton')
    med = entry_of('Medkit')
    check('pickup placed from cursor', not case.is_holding() and med is not None and ecell(med) == (0, 0) and p not in pickups(), describe())
    yield from key('Escape')
    check('closed at end', not case.is_screen_open() and abs(unreal.GameplayStatics.get_global_time_dilation(w) - 1.0) < 0.01)
    for q in pickups():
        q.destroy_actor()

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
