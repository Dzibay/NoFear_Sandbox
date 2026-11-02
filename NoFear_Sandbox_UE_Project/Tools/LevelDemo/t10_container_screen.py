# -*- coding: utf-8 -*-
# T10: экран кейса с хранилищем (Docs/Inventory_Design.md). Ящик NoFear.Inv.Box рядом с кейсом, без колеса;
# ствол переезжает между сетками с тем же магазином, доливка в стопку кейса, поворот и красная цель в ящике,
# меню ПКМ «Забрать в кейс» и «Переложить», разделение и Esc, выброс за край, расширение из ящика,
# 1–8 на предмете ящика, закрытие на E с OnContainerScreenClosed, занятый экран, отход и уничтожение ящика.
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


def eid(e):
    return e.get_editor_property('id')


def ecell(e):
    c = e.get_editor_property('cell')
    return (c.x, c.y)


def ecount(e):
    return e.get_editor_property('count')


def describe(storage):
    out = []
    for e in storage.get_entries():
        it = e.get_editor_property('item')
        c = e.get_editor_property('cell')
        wpn = e.get_editor_property('weapon')
        mag = wpn.get_magazine_ammo() if wpn else e.get_editor_property('magazine_ammo')
        out.append('#%d %s x%d (%d,%d)%s%s' % (
            eid(e), it.get_name().replace('DA_Item_', '') if it else '?', ecount(e),
            c.x, c.y, ' rot' if e.get_editor_property('rotated') else '', ' mag%d' % mag if mag >= 0 else ''))
    return ' '.join(out)


def entry_of(storage, name):
    for e in storage.get_entries():
        if e.get_editor_property('item') == item(name):
            return e
    return None


def by_id(storage, i):
    for e in storage.get_entries():
        if eid(e) == i:
            return e
    return None


def pickups():
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearPickup))


def key(name, hold=0.05):
    """Нажать и отпустить клавишу; между ними и после — по кадру с запасом (время замедлено)."""
    cmd('NoFear.Inv.Key %s press' % name)
    yield hold
    cmd('NoFear.Inv.Key %s release' % name)
    yield 0.05


def cursor(p):
    inv().set_screen_cursor(p)


def center(x, y, box):
    p = unreal.IntPoint(x, y)
    return inv().get_container_cell_center(p) if box else inv().get_cell_center(p)


def held_cursor(x, y, box):
    """Точка курсора, при которой предмет на курсоре ляжет левым верхним углом в клетку (x, y) нужной сетки."""
    c00 = center(0, 0, box)
    cell = center(1, 0, box).x - c00.x
    grab = inv().get_held_grab()
    return unreal.Vector2D(c00.x - cell / 2.0 + (x + grab.x) * cell, c00.y - cell / 2.0 + (y + grab.y) * cell)


def on(e, box):
    """Центр левой верхней клетки записи."""
    return center(*ecell(e), box=box)


def free_spot(storage, it, rotated, ignore=0, avoid=()):
    size = storage.get_grid_size()
    for y in range(size.y):
        for x in range(size.x):
            if (x, y) not in avoid and storage.can_place_at(it, unreal.IntPoint(x, y), rotated, ignore):
                return (x, y)
    return None


def menu_click(action):
    acts = list(inv().get_menu_actions())
    if action not in acts:
        return False
    cursor(inv().get_menu_item_center(acts.index(action)))
    return True


def find_box():
    found = [a for a in unreal.GameplayStatics.get_all_actors_with_tag(w, 'NoFearTestBox')]
    return found[-1] if found else None


closed = []


def on_closed(container):
    closed.append(container)


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
    case.on_container_screen_closed.add_callable(on_closed)
    log('case: %s' % describe(case))

    # --- ящик открывается рядом с кейсом: замедление, без колеса
    cmd('NoFear.Inv.Box')
    yield 0.1
    box = find_box()
    store = box.get_component_by_class(unreal.NoFearItemContainerComponent) if box else None
    # Гранаты в стопки не складываются: две записи.
    check('box spawned', store is not None and len(store.get_entries()) == 7, describe(store) if store else '')
    log('box: %s' % describe(store))
    dil = unreal.GameplayStatics.get_global_time_dilation(w)
    check('screen opens with box', case.is_screen_open() and case.get_open_container() == store)
    check('time slowed', abs(dil - 0.3) < 0.01, 'dilation %.2f' % dil)
    cursor(case.get_wheel_slot_center(0))
    check('no wheel', case.get_hovered_wheel_slot() == -1)
    shot = entry_of(store, 'Shotgun')
    check('box shotgun magazine', shot is not None and shot.get_editor_property('magazine_ammo') == 2 and not shot.get_editor_property('weapon'))

    # --- дробовик щелчком из ящика в кейс: ствол появляется с тем же магазином и встаёт в колесо
    cursor(on(shot, True))
    yield from key('LeftMouseButton')
    check('hold from box', case.is_holding() and case.is_holding_from_container() and case.get_held_magazine() == 2)
    to = free_spot(case, item('Shotgun'), False)
    cursor(held_cursor(*to, box=False))
    yield from key('LeftMouseButton')
    cshot = entry_of(case, 'Shotgun')
    wpn = cshot.get_editor_property('weapon') if cshot else None
    check('box -> case', not case.is_holding() and entry_of(store, 'Shotgun') is None and cshot is not None and ecell(cshot) == to,
          describe(case))
    check('weapon keeps magazine', wpn is not None and wpn.get_magazine_ammo() == 2 and wpn in list(wc.get_weapons()),
          'mag %s' % (wpn.get_magazine_ammo() if wpn else None))
    check('auto wheel', case.find_wheel_slot_of(eid(cshot)) >= 0)

    # --- перетаскиванием обратно: ствол исчезает из рук, в ящике помнит магазин
    sid = eid(cshot)
    cursor(on(cshot, False))
    cmd('NoFear.Inv.Key LeftMouseButton press')
    yield 0.05
    to = free_spot(store, item('Shotgun'), False)
    cursor(held_cursor(*to, box=True))
    yield 0.05
    cmd('NoFear.Inv.Key LeftMouseButton release')
    yield 0.05
    shot = entry_of(store, 'Shotgun')
    check('case -> box by drag', not case.is_holding() and entry_of(case, 'Shotgun') is None and shot is not None and ecell(shot) == to,
          describe(store))
    check('box keeps magazine', shot is not None and shot.get_editor_property('magazine_ammo') == 2 and wpn not in list(wc.get_weapons()))
    check('wheel freed', case.find_wheel_slot_of(sid) == -1)

    # --- патроны из ящика доливаются в стопку кейса
    ammo = item('PistolAmmo')
    total = case.count_item(ammo) + store.count_item(ammo)
    cursor(on(entry_of(store, 'PistolAmmo'), True))
    yield from key('LeftMouseButton')
    cursor(on(entry_of(case, 'PistolAmmo'), False))
    yield from key('LeftMouseButton')
    if case.is_holding():
        yield from key('Escape')
    check('merge into case', case.count_item(ammo) > 24 and case.count_item(ammo) + store.count_item(ammo) == total
          and len([e for e in case.get_entries() if e.get_editor_property('item') == ammo]) == 1,
          'case %d box %d' % (case.count_item(ammo), store.count_item(ammo)))

    # --- R в ящике поворачивает на месте; красная цель — предмет остаётся на курсоре
    shot = entry_of(store, 'Shotgun')
    sid = eid(shot)
    rot0 = shot.get_editor_property('rotated')
    can = store.can_place_at(item('Shotgun'), unreal.IntPoint(*ecell(shot)), not rot0, sid)
    cursor(on(shot, True))
    yield from key('R')
    rot = by_id(store, sid).get_editor_property('rotated')
    check('R rotates in box', rot == (rot0 != can), 'room %s rotated %s' % (can, rot))
    shot = by_id(store, sid)
    cell0 = ecell(shot)
    cursor(on(shot, True))
    yield from key('LeftMouseButton')
    shells = entry_of(store, 'Shells')
    cursor(held_cursor(*ecell(shells), box=True))
    yield from key('LeftMouseButton')
    check('blocked in box keeps holding', case.is_holding() and ecell(by_id(store, sid)) == cell0)
    yield from key('Escape')
    check('Esc returns held', case.is_screen_open() and not case.is_holding() and ecell(by_id(store, sid)) == cell0)

    # --- меню ПКМ: «Забрать в кейс» и «Переложить» обратно
    med = entry_of(store, 'Medkit')
    cursor(on(med, True))
    yield from key('RightMouseButton')
    acts = list(case.get_menu_actions())
    check('box menu', case.is_menu_open() and case.is_menu_for_container() and acts == [A.TAKE, A.DROP], str(acts))
    menu_click(A.TAKE)
    yield from key('LeftMouseButton')
    cmed = entry_of(case, 'Medkit')
    check('take to case', entry_of(store, 'Medkit') is None and cmed is not None, describe(case))
    cursor(on(cmed, False))
    yield from key('RightMouseButton')
    acts = list(case.get_menu_actions())
    check('case menu has store', A.STORE in acts and not case.is_menu_for_container(), str(acts))
    menu_click(A.STORE)
    yield from key('LeftMouseButton')
    check('store to box', entry_of(case, 'Medkit') is None and entry_of(store, 'Medkit') is not None, describe(store))

    # --- разделение в ящике: Esc возвращает часть; положенная в кейс часть — «взято»
    shells = entry_of(store, 'Shells')
    hid = eid(shells)
    cursor(on(shells, True))
    yield from key('RightMouseButton')
    menu_click(A.SPLIT)
    yield from key('LeftMouseButton')
    check('split in box', case.is_holding_split() and case.is_holding_from_container() and case.get_held_count() == 6
          and ecount(by_id(store, hid)) == 6)
    yield from key('Escape')
    check('split back by Esc', case.is_screen_open() and not case.is_holding() and ecount(by_id(store, hid)) == 12
          and store.count_item(item('Shells')) == 12)
    cursor(on(by_id(store, hid), True))
    yield from key('RightMouseButton')
    menu_click(A.SPLIT)
    yield from key('LeftMouseButton')
    to = free_spot(case, item('Shells'), False)
    cursor(held_cursor(*to, box=False))
    yield from key('LeftMouseButton')
    check('split into case', not case.is_holding() and case.count_item(item('Shells')) == 6 and store.count_item(item('Shells')) == 6)

    # --- 1–8 на предмете ящика: в колесо только из кейса
    wheel0 = [case.get_wheel_slot(i) for i in range(8)]
    cursor(on(by_id(store, hid), True))
    yield from key('One')
    check('box item not to wheel', [case.get_wheel_slot(i) for i in range(8)] == wheel0)

    # --- из ящика за край — на землю
    n = len(pickups())
    # в L_Demo в Z8_Items уже лежат гранаты: считаем только новые
    grenade_pickups0 = len([p for p in pickups() if p.get_editor_property('item') == item('Grenade')])
    grenades = store.count_item(item('Grenade'))
    cursor(on(entry_of(store, 'Grenade'), True))
    yield from key('LeftMouseButton')
    cursor(case.get_outside_point())
    yield from key('LeftMouseButton')
    new = [p for p in pickups() if p.get_editor_property('item') == item('Grenade')]
    check('drop from box', store.count_item(item('Grenade')) == grenades - 1 and len(pickups()) == n + 1 and len(new) == grenade_pickups0 + 1
          and not case.is_holding() and case.count_item(item('Grenade')) == 0)

    # --- расширение из ящика на сетку кейса увеличивает кейс
    size0 = case.get_grid_size()
    cursor(on(entry_of(store, 'CaseExpansion'), True))
    yield from key('LeftMouseButton')
    cursor(center(1, 1, False))
    yield from key('LeftMouseButton')
    size1 = case.get_grid_size()
    check('expansion from box', entry_of(store, 'CaseExpansion') is None and not case.is_holding() and size1.x > size0.x and size1.y > size0.y,
          '%dx%d -> %dx%d' % (size0.x, size0.y, size1.x, size1.y))

    # --- E закрывает и сообщает, какое хранилище закрыли
    yield from key('E')
    dil = unreal.GameplayStatics.get_global_time_dilation(w)
    check('E closes', not case.is_screen_open() and case.get_open_container() is None and abs(dil - 1.0) < 0.01, 'dilation %.2f' % dil)
    check('closed event', len(closed) == 1 and closed[0] == store, str(closed))

    # --- экран кейса уже открыт — ящик не открывается; с этим же ящиком — да
    yield from key('Tab')
    check('busy screen refuses', case.is_screen_open() and not case.open_container_screen(store) and case.get_open_container() is None)
    yield from key('Tab')
    check('reopen', case.open_container_screen(store) and case.open_container_screen(store) and case.get_open_container() == store)

    # --- отошёл — экран закрывается
    put_player(9, 0)
    yield 0.2
    check('walk away closes', not case.is_screen_open() and len(closed) == 2, str(closed))
    check('far refuses', not case.open_container_screen(store) and not case.is_screen_open())
    put_player(2, 0, 0)
    yield 0.2

    # --- ящик уничтожен с частью стопки на курсоре: экран закрывается, часть остаётся у героя
    check('reopen near', case.open_container_screen(store))
    yield 0.1
    shells0 = case.count_item(item('Shells'))
    cursor(on(by_id(store, hid), True))
    yield from key('RightMouseButton')
    menu_click(A.SPLIT)
    yield from key('LeftMouseButton')
    held = case.get_held_count()
    box.destroy_actor()
    yield 0.2
    check('destroy closes', not case.is_screen_open() and not case.is_holding() and len(closed) == 3 and closed[2] is None, str(closed))
    check('held split kept', held == 3 and case.count_item(item('Shells')) == shells0 + held, 'held %d, shells %d' % (held, case.count_item(item('Shells'))))
    check('case intact', entry_of(case, 'Pistol') is not None and case.count_item(item('Bandage')) == 1, describe(case))

    for q in pickups():
        q.destroy_actor()
    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
