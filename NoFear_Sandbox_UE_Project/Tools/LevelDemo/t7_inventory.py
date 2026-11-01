# -*- coding: utf-8 -*-
# T7: кейс героя (Docs/Inventory_Design.md, этап 1). Стартовый минимум, подбор на E с автораскладкой и поворотом,
# перезарядка и гранаты из кейса, выброс ствола с магазином, нет места, расширение, частичный подбор, набор Kit,
# кейс нового тела после возрождения.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []


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


def describe():
    out = []
    for e in entries():
        it = e.get_editor_property('item')
        c = e.get_editor_property('cell')
        wpn = e.get_editor_property('weapon')
        out.append('#%d %s x%d (%d,%d)%s%s' % (
            e.get_editor_property('id'), it.get_name().replace('DA_Item_', '') if it else '?', e.get_editor_property('count'),
            c.x, c.y, ' rot' if e.get_editor_property('rotated') else '', ' mag%d' % wpn.get_magazine_ammo() if wpn else ''))
    return ' '.join(out)


def entry_of(name):
    for e in entries():
        it = e.get_editor_property('item')
        if it and it.get_name() == 'DA_Item_' + name:
            return e
    return None


def pickups():
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearPickup))


def press_e():
    """То же, что E: взаимодействие с тем, что под прицелом."""
    f = pc.get_focused_interactable()
    if f is None:
        return None
    owner = f.get_owner()
    f.interact(pawn)
    return owner


def spawn(name, count, dist=150):
    """Положить предмет на землю перед героем (NoFear.Inv.Spawn); возвращает новый актор."""
    before = set(p.get_name() for p in pickups())
    cmd('NoFear.Inv.Spawn %s %d %d' % (name, count, dist))
    new = [p for p in pickups() if p.get_name() not in before]
    return new[0] if new else None


def aim(p):
    loc = p.get_actor_location()
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location() + unreal.Vector(0, 0, 60), loc))


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

    # --- стартовый минимум
    case = inv()
    size = case.get_grid_size()
    wc = weapon()
    weapons = list(wc.get_weapons())
    cur = wc.get_current_weapon()
    log('start: %s' % describe())
    check('case active', case.is_case_active())
    check('grid 8x5', size.x == 8 and size.y == 5, '%dx%d' % (size.x, size.y))
    check('start pistol in hands', len(weapons) == 1 and cur is not None and cur.get_weapon_data().get_name() == 'DA_Pistol',
          '%d weapons, current %s' % (len(weapons), cur.get_weapon_data().get_name() if cur else None))
    check('start ammo 24', case.count_item(item('PistolAmmo')) == 24 and cur.get_reserve_ammo() == 24,
          'case %d reserve %d' % (case.count_item(item('PistolAmmo')), cur.get_reserve_ammo() if cur else -1))
    check('start bandage', case.count_item(item('Bandage')) == 1)
    check('start no grenades', pawn.get_grenade_component().get_grenade_count() == 0)
    pistol_entry = entry_of('Pistol')
    check('wheel 1 = pistol', pistol_entry is not None and case.get_wheel_slot(0) == pistol_entry.get_editor_property('id'),
          'slot1 #%d' % case.get_wheel_slot(0))

    # --- подбор на E: патроны доливаются в неполную стопку
    p = spawn('PistolAmmo', 30)
    yield 1.5
    aim(p)
    yield 0.3
    f = pc.get_focused_interactable()
    check('pickup focused', f is not None and f.get_owner() == p,
          'focus %s prompt %s' % (f.get_owner().get_name() if f else None, f.get_interaction_prompt(pawn) if f else ''))
    picked = press_e()
    yield 0.2
    ammo = entry_of('PistolAmmo')
    check('pickup ammo merged', case.count_item(item('PistolAmmo')) == 54 and len([e for e in entries() if e.get_editor_property('item') == item('PistolAmmo')]) == 1,
          describe())
    check('pickup actor gone', p not in pickups())

    # --- перезарядка из кейса
    mag0 = cur.get_magazine_ammo()
    wc.start_fire()
    yield 0.8
    wc.stop_fire()
    yield 0.3
    mag1 = cur.get_magazine_ammo()
    fired = mag0 - mag1
    wc.reload()
    yield 3.5
    mag2 = cur.get_magazine_ammo()
    left = case.count_item(item('PistolAmmo'))
    check('reload from case', fired > 0 and mag2 == mag0 and left == 54 - fired and cur.get_reserve_ammo() == left,
          'mag %d -> %d -> %d, case %d, reserve %d' % (mag0, mag1, mag2, left, cur.get_reserve_ammo()))

    # --- гранаты из кейса
    gc = pawn.get_grenade_component()
    cmd('NoFear.Inv.Give Grenade 2')
    check('grenades in case', gc.get_grenade_count() == 2 and case.count_item(item('Grenade')) == 2, describe())
    thrown = gc.throw_grenade_at(V(14, 0, 0))
    yield 2.0
    check('grenade thrown from case', thrown and gc.get_grenade_count() == 1 and case.count_item(item('Grenade')) == 1,
          'thrown %s count %d' % (thrown, gc.get_grenade_count()))
    yield 3.0
    god()

    # --- выброс ствола с магазином и подбор обратно
    cmd('NoFear.Inv.Give Rifle')
    rifle_entry = entry_of('Rifle')
    rifle = rifle_entry.get_editor_property('weapon') if rifle_entry else None
    weapons = list(wc.get_weapons())
    check('rifle added', rifle is not None and rifle in weapons, describe())
    wc.equip_weapon(weapons.index(rifle))
    yield 2.0
    look_at(14, 0)
    wc.start_fire()
    yield 0.6
    wc.stop_fire()
    yield 0.3
    rifle_mag = rifle.get_magazine_ammo()
    check('rifle fired', wc.get_current_weapon() == rifle and rifle_mag < rifle.get_weapon_data().get_editor_property('magazine_size'),
          'mag %d' % rifle_mag)
    dropped = case.drop_entry(rifle_entry.get_editor_property('id'), 0)
    yield 0.2
    weapons = list(wc.get_weapons())
    check('rifle dropped', dropped is not None and entry_of('Rifle') is None and len(weapons) == 1,
          'pickup %s mag %s, weapons %d, current %s' % (dropped.get_name() if dropped else None,
                                                     dropped.get_editor_property('magazine_ammo') if dropped else None, len(weapons),
                                                     wc.get_current_weapon()))
    check('dropped keeps magazine', dropped is not None and dropped.get_editor_property('magazine_ammo') == rifle_mag)
    yield 2.0
    aim(dropped)
    yield 0.3
    got = press_e()
    yield 0.2
    rifle_entry = entry_of('Rifle')
    rifle = rifle_entry.get_editor_property('weapon') if rifle_entry else None
    check('rifle picked back with magazine', got == dropped and rifle is not None and rifle.get_magazine_ammo() == rifle_mag,
          describe())

    # --- поворот при автораскладке: после двух автоматов пистолет влезает только боком
    case.clear_case()
    cmd('NoFear.Inv.Give Rifle')
    cmd('NoFear.Inv.Give Rifle')
    cmd('NoFear.Inv.Give Pistol')
    pe = entry_of('Pistol')
    c = pe.get_editor_property('cell') if pe else None
    check('rotated placement', pe is not None and pe.get_editor_property('rotated') and c.x == 6 and c.y == 0, describe())

    # --- нет места, расширение, частичный подбор
    case.clear_case()
    cmd('NoFear.Inv.Give Shells %d' % (24 * 40))
    check('case full', len(entries()) == 40 and case.get_room_for(item('Shells'), 10) == 0, '%d entries' % len(entries()))
    p = spawn('Shells', 10)
    yield 1.5
    aim(p)
    yield 0.3
    got = press_e()
    yield 0.2
    check('no room keeps pickup', got == p and p in pickups() and p.get_count() == 10 and case.count_item(item('Shells')) == 960)
    x = spawn('CaseExpansion', 1, 120)
    yield 1.5
    aim(x)
    yield 0.3
    got = press_e()
    yield 0.2
    size = case.get_grid_size()
    check('expansion picked', got == x and x not in pickups() and size.x == 9 and size.y == 6 and case.get_expansion_level() == 1,
          '%dx%d level %d' % (size.x, size.y, case.get_expansion_level()))
    room = case.get_room_for(item('Shells'), 1000)
    check('room after expansion', room == 14 * 24, 'room %d' % room)
    big = spawn('Shells', 400, 120)
    yield 1.5
    aim(big)
    yield 0.3
    got = press_e()
    yield 0.2
    check('partial pickup', got == big and big in pickups() and big.get_count() == 400 - room and case.count_item(item('Shells')) == 960 + room,
          'left %d, case %d' % (big.get_count() if big in pickups() else -1, case.count_item(item('Shells'))))
    # Что осталось лежать — убрать.
    for q in pickups():
        q.destroy_actor()

    # --- набор старых сценариев
    cmd('NoFear.Inv.Kit')
    yield 2.0
    weapons = list(wc.get_weapons())
    cur = wc.get_current_weapon()
    size = case.get_grid_size()
    check('kit', len(weapons) == 3 and gc.get_grenade_count() == 3 and cur is not None and cur.get_weapon_data().get_name() == 'DA_Rifle'
          and size.x == 10 and size.y == 7 and cur.get_reserve_ammo() > 0,
          '%d weapons, grenades %d, current %s, reserve %d, grid %dx%d' % (
              len(weapons), gc.get_grenade_count(), cur.get_weapon_data().get_name() if cur else None,
              cur.get_reserve_ammo() if cur else -1, size.x, size.y))
    log('kit: %s' % describe())

    # --- возрождение: новое тело получает кейс со стартовым минимумом (уберётся вместе с возрождением на этапе 4)
    old = pawn
    old.get_health_component().kill()
    yield 0.5
    pc.respawn_player()
    yield 1.5
    body = pc.get_controlled_pawn()
    new_case = body.get_inventory_component() if body else None
    new_weapons = list(body.get_weapon_component().get_weapons()) if body else []
    new_cur = body.get_weapon_component().get_current_weapon() if body else None
    check('respawn gets start kit', body is not None and body != old and new_case.is_case_active()
          and len(new_case.get_entries()) == 3 and new_case.count_item(item('PistolAmmo')) == 24 and len(new_weapons) == 1
          and new_cur is not None and new_cur.get_weapon_data().get_name() == 'DA_Pistol' and new_cur.get_reserve_ammo() == 24,
          'entries %d, weapons %d, current %s' % (len(new_case.get_entries()) if new_case else -1, len(new_weapons),
                                                   new_cur.get_weapon_data().get_name() if new_cur else None))

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
