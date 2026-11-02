# -*- coding: utf-8 -*-
# T8: двери, люки, сундуки (Docs/Interactables.md). E открывает и закрывает дверь от себя, запертую не открыть,
# её навмеш вырезан, выстрелы ломают её и открывают путь; сундук на E перекладывает в кейс, запертый сбивается уроном;
# люк держит героя, открывается вверх, по пандусу — в подвал; мирный сам открывает дверь на пути и закрывает за собой;
# монстр бьёт закрытую дверь между собой и героем.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
LIMIT[0] = 230.0


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def item(name):
    return unreal.load_asset('/Game/Items/DA_Item_' + name)


def nearest(cls, x, y):
    best, bd = None, 1e9
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, cls):
        l = a.get_actor_location()
        d = math.hypot(l.x / 100.0 - x, l.y / 100.0 - y)
        if d < bd:
            best, bd = a, d
    return best if bd < 2.0 else None


def press_e():
    f = pc.get_focused_interactable()
    if f is None:
        return None, ''
    prompt = str(f.get_interaction_prompt(pawn))
    f.interact(pawn)
    return f.get_owner(), prompt


def focus_on(x, y, z=1.0):
    look_at(x, y, z)


def st(d):
    return str(d.get_door_state()).split('.')[-1].split(':')[0]


def case():
    return pawn.get_inventory_component()


def loot(chest):
    # E открыла экран сундука: проверить, забрать всё, закрыть экран.
    screen = case().get_open_container() == chest.get_storage()
    chest.take_all(pawn)
    case().close_screen()
    return screen


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


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()

    doors = list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearDoor))
    chests = list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NoFearChest))
    hatches = [d for d in doors if isinstance(d, unreal.NoFearHatch)]
    shack = nearest(unreal.NoFearDoor, 70, 33.85)
    vault = nearest(unreal.NoFearDoor, 64.6, 36.15)
    hatch = hatches[0] if hatches else None
    log('doors %d (hatches %d), chests %d' % (len(doors), len(hatches), len(chests)))
    check('placed', len(doors) == 8 and len(hatches) == 1 and len(chests) == 4 and shack and vault and hatch)
    check('vault locked, shack not', vault.is_locked() and not shack.is_locked() and st(shack) == 'CLOSED')

    # --- E открывает от себя и закрывает
    put_player(70, 35.6, 270)
    yield 0.5
    focus_on(70, 33.85)
    yield 0.3
    got, prompt = press_e()
    check('shack focus', got == shack, 'got %s prompt %s' % (got.get_name() if got else None, prompt))
    yield 1.0
    leaf = shack.get_components_by_class(unreal.StaticMeshComponent)
    tips = [c.get_world_location().y for c in leaf if c.is_visible()]
    check('shack opened away', shack.is_open() and shack.get_open_amount() > 0.99 and tips and min(tips) / 100.0 < 33.5,
          'state %s amount %.2f leaf y %s' % (st(shack), shack.get_open_amount(), ['%.1f' % (t / 100.0) for t in tips]))
    route[:] = [(70, 32)]
    yield 2.5
    inside = pawn.get_actor_location().y / 100.0 < 33.0
    check('walk through open door', inside, 'player %s' % m2(pawn.get_actor_location()))
    focus_on(70, 33.85)
    yield 0.3
    got, prompt = press_e()
    yield 1.0
    check('shack closed by E', st(shack) == 'CLOSED', 'prompt %s state %s' % (prompt, st(shack)))

    # --- запертый сундук: E не открывает, удар сбивает замок
    lchest = nearest(unreal.NoFearChest, 71, 31)
    focus_on(71, 31, 0.3)
    yield 0.3
    got, prompt = press_e()
    yield 0.6
    check('locked chest stays shut', got == lchest and lchest.is_locked() and not lchest.is_open(), 'prompt %s' % prompt)
    unreal.GameplayStatics.apply_damage(lchest, 100.0, pc, pawn, None)
    yield 0.8
    check('chest lock broken', not lchest.is_locked() and lchest.is_open() and lchest.get_lid_amount() > 0.5)
    shells0 = case().count_item(item('Shells'))
    got, prompt = press_e()
    yield 0.3
    screen = loot(lchest)
    yield 0.3
    check('locked chest taken', screen and lchest.is_empty() and case().count_item(item('Shells')) == shells0 + 16
          and case().count_item(item('Bandage')) == 3, 'prompt %s shells %d bandage %d' % (
              prompt, case().count_item(item('Shells')), case().count_item(item('Bandage'))))

    # --- запертая дверь: E не открывает, путь закрыт, выстрелы ломают
    put_player(64.6, 34.6, 90)
    yield 0.5
    focus_on(64.6, 36.15)
    yield 0.3
    got, prompt = press_e()
    yield 0.8
    blocked = path(64.6, 34.0, 64.6, 38.5)
    check('vault E keeps it shut', got == vault and 'Заперто' in prompt and st(vault) == 'CLOSED', 'prompt %s state %s' % (prompt, st(vault)))
    check('vault cut from navmesh', not blocked.startswith('ok'), blocked)
    hp0 = vault.get_health_fraction()
    cur = weapon().get_current_weapon()
    mag0 = cur.get_magazine_ammo() if cur else -1
    for i in range(3):
        weapon().start_fire()
        yield 0.1
        weapon().stop_fire()
        yield 0.4
    hp1 = vault.get_health_fraction()
    check('bullets hit door', hp1 < hp0, 'health %.2f -> %.2f weapon %s mag %d -> %d' % (
        hp0, hp1, cur.get_name() if cur else None, mag0, cur.get_magazine_ammo() if cur else -1))
    for i in range(10):
        if vault.is_broken():
            break
        unreal.GameplayStatics.apply_damage(vault, 40.0, pc, pawn, None)
        yield 0.2
    yield 1.5
    opened = path(64.6, 34.0, 64.6, 38.5)
    debris = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.StaticMeshActor)
              if dist_m(a.get_actor_location(), V(64.6, 36.15)) < 6.0 and a.get_editor_property('initial_life_span') > 0]
    check('vault broken', vault.is_broken() and not vault.is_locked(), 'state %s' % st(vault))
    check('vault path open', opened.startswith('ok'), opened)
    check('debris', len(debris) >= 3, '%d pieces' % len(debris))

    # --- сундук: E открывает и перекладывает в кейс
    vchest = nearest(unreal.NoFearChest, 65.5, 39.8)
    route[:] = [(64.6, 37.5), (65.0, 38.6)]
    yield 3.0
    focus_on(65.5, 39.8, 0.3)
    yield 0.3
    ammo0, gren0 = case().count_item(item('RifleAmmo')), case().count_item(item('Grenade'))
    got, prompt = press_e()
    yield 0.8
    screen = loot(vchest)
    yield 0.3
    check('chest take all', screen and got == vchest and vchest.is_open() and vchest.is_empty()
          and case().count_item(item('RifleAmmo')) == ammo0 + 60 and case().count_item(item('Grenade')) == gren0 + 2
          and case().count_item(item('Medkit')) >= 1, 'prompt %s player %s left %d' % (prompt, m2(pawn.get_actor_location()), vchest.get_item_count()))
    got, prompt = press_e()
    yield 0.8
    check('empty chest closes', not vchest.is_open() and vchest.get_lid_amount() < 0.05, 'prompt %s' % prompt)

    # --- люк: держит, открывается вверх, вниз по пандусу в подвал
    put_player(51.7, 40.7, 0)
    yield 1.0
    z_on = pawn.get_actor_location().z / 100.0
    check('stand on closed hatch', z_on > 0.5 and st(hatch) == 'CLOSED', 'z %.2f state %s' % (z_on, st(hatch)))
    put_player(49.0, 40.7, 0)
    yield 0.5
    focus_on(51.7, 40.7, 0.0)
    yield 0.3
    got, prompt = press_e()
    yield 1.3
    check('hatch opened', got == hatch and hatch.is_open() and hatch.get_open_amount() > 0.99, 'got %s prompt %s state %s' % (
        got.get_name() if got else None, prompt, st(hatch)))
    route[:] = [(50.6, 40.7), (54.5, 40.7), (56.0, 41.6)]
    yield 6.0
    z_in = pawn.get_actor_location().z / 100.0
    check('walked into cellar', z_in < -1.5, 'player %s z %.2f' % (m2(pawn.get_actor_location()), z_in))
    cchest = nearest(unreal.NoFearChest, 56.5, 42.3)
    focus_on(56.5, 42.3, -2.7)
    yield 0.3
    lvl = case().get_expansion_level()
    got, prompt = press_e()
    yield 0.5
    screen = loot(cchest) if got == cchest else False
    yield 0.3
    check('cellar chest', screen and got == cchest and cchest.is_empty() and case().count_item(item('Rifle')) == 1 and case().get_expansion_level() == lvl + 1,
          'got %s left %d' % (got.get_name() if got else None, cchest.get_item_count() if cchest else -1))
    check('cellar path', path(48.0, 40.7, 56.0, 42.0).startswith('ok'), path(48.0, 40.7, 56.0, 42.0))

    # --- мирный сам открывает дверь на пути и закрывает за собой
    put_player(60, 26, 0)
    yield 0.3
    civ = spawn_bot('Civilian', 70, 37)
    yield 0.6
    ctrl = civ.get_controller()
    opened0 = shack.get_bot_open_count()
    # Дом мирного — в хижине; дерево поведения стоит, чтобы он не ушёл бродить, идёт только приказом.
    ctrl.set_home_location(V(70, 31.5, 0.1))
    try:
        ctrl.get_editor_property('brain_component').stop_logic('t8')
    except Exception as e:
        log('stop_logic: %s' % e)
    yield 0.2
    log('move %s' % ctrl.move_to_location(V(70, 31.5, 0), 30.0))
    log('civ path %s' % path(70, 37, 70, 31.5))
    t_open = None
    for i in range(40):
        yield 0.25
        if i % 4 == 0:
            log('  civ %s %s %s door %s' % (m2(civ.get_actor_location()), state(civ), ctrl.get_move_status(), st(shack)))
        if t_open is None and shack.is_open():
            t_open = i
        if civ.get_actor_location().y / 100.0 < 32.5:
            break
    check('civilian opened door', shack.get_bot_open_count() > opened0 and civ.get_actor_location().y / 100.0 < 33.0,
          'opens %d civ %s state %s' % (shack.get_bot_open_count(), m2(civ.get_actor_location()), st(shack)))
    yield 5.0
    check('door closed behind civilian', st(shack) in ('CLOSED', 'CLOSING'), 'state %s civ %s' % (st(shack), m2(civ.get_actor_location())))

    # --- монстр бьёт закрытую дверь между собой и героем
    civ.destroy_actor()
    yield 0.2
    shack.close_door(None)
    put_player(70, 31.6, 90)
    yield 1.0
    mon = spawn_bot('Monster', 70, 40)
    yield 0.3
    mon.get_controller().report_threat(pawn)
    hp0 = shack.get_health_fraction()
    for i in range(60):
        yield 0.5
        god()
        if i % 6 == 0 and not dead(mon):
            mon.get_controller().report_threat(pawn)
        if shack.is_broken():
            break
    check('monster attacks door', shack.get_monster_attack_count() > 0 and shack.get_health_fraction() < hp0,
          'attacks %d health %.2f state %s monster %s' % (shack.get_monster_attack_count(), shack.get_health_fraction(), st(shack),
                                                         m2(mon.get_actor_location()) if not dead(mon) else 'dead'))
    check('monster breaks door', shack.is_broken(), 'state %s' % st(shack))

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
