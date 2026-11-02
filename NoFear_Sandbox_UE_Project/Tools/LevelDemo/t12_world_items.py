# -*- coding: utf-8 -*-
# T12: предметы для объектов мира (Docs/Inventory_Design.md, «Предметы для мира»). Доски — расходник без колеса,
# ключи находятся по KeyId в кейсе и в хранилище, капкан и растяжка встают на пол перед героем (PlaceItem):
# поворот и место, владелец и инстигатор — герой, предмет тратится; отказ при открытом экране, у стены, без пола,
# если актор не появился или уничтожил себя в BeginPlay — тогда предмет остаётся.
import os
KIT = False
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


def entries_of(storage, name):
    return [e for e in storage.get_entries() if e.get_editor_property('item') == item(name)]


def key(name, hold=0.05):
    cmd('NoFear.Inv.Key %s press' % name)
    yield hold
    cmd('NoFear.Inv.Key %s release' % name)
    yield 0.05


def placed(cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, cls))


def face_wall(gap=70.0):
    """Поставить героя лицом к ближайшей стене уровня в gap см от неё; в -game из Python актор не заспавнить."""
    c = pawn.get_actor_location()
    best = None
    for yaw in (0.0, 90.0, 180.0, 270.0):
        d = unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0.0)
        hit = unreal.SystemLibrary.line_trace_single(w, c, c + d * 3000.0, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                     False, [pawn], unreal.DrawDebugTrace.NONE, True)
        if hit:
            t = hit.to_tuple()
            if best is None or t[3] < best[0]:
                best = (t[3], yaw, d, t[5])
    if best is None:
        return None
    _, yaw, d, point = best
    pawn.set_actor_location(unreal.Vector(point.x - d.x * gap, point.y - d.y * gap, c.z), False, True)
    pc.set_control_rotation(unreal.Rotator(0.0, 0.0, yaw))
    return best[0]


# Актор, который сам уничтожает себя в BeginPlay, как ловушка на плохом месте.
self_destroyed = [0]
try:
    @unreal.uclass()
    class NFTSelfDestroyActor(unreal.Actor):
        @unreal.ufunction(override=True)
        def receive_begin_play(self):
            self_destroyed[0] += 1
            self.destroy_actor()
    SELF_DESTROY = NFTSelfDestroyActor.static_class()
except Exception as e:
    SELF_DESTROY = None
    log('no python actor class: %s' % e)


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

    # --- ассеты
    planks, trap, wire = item('Planks'), item('BearTrap'), item('Tripwire')
    vault, lab = item('Key_Vault'), item('KeyCard_Lab')
    check('assets load', all(a is not None for a in (planks, trap, wire, vault, lab)))
    check('planks asset', planks.get_editor_property('kind') == K.MATERIAL and planks.get_grid_size() == unreal.IntPoint(1, 2)
          and planks.get_max_stack() == 4)
    check('trap asset', trap.get_editor_property('kind') == K.PLACEABLE and trap.get_grid_size() == unreal.IntPoint(2, 2)
          and trap.get_editor_property('place_class') == unreal.NoFearBearTrap.static_class(), str(trap.get_editor_property('place_class')))
    check('wire asset', wire.get_editor_property('kind') == K.PLACEABLE and wire.get_max_stack() == 3
          and wire.get_editor_property('place_class') == unreal.NoFearTripwire.static_class(), str(wire.get_editor_property('place_class')))
    check('key assets', vault.get_editor_property('kind') == K.KEY and str(vault.get_editor_property('key_id')) == 'Vault'
          and lab.get_editor_property('kind') == K.KEY and str(lab.get_editor_property('key_id')) == 'Lab')

    # --- доски: стопки по 4, без колеса, объекты мира забирают по штуке
    cmd('NoFear.Inv.Give Planks 6')
    stacks = sorted(e.get_editor_property('count') for e in entries_of(case, 'Planks'))
    check('planks stacks', stacks == [2, 4] and case.count_item(planks) == 6, str(stacks))
    pid = entries_of(case, 'Planks')[0].get_editor_property('id')
    check('planks not in wheel', case.find_wheel_slot_of(pid) == -1 and not case.assign_wheel_slot(5, pid))
    check('planks taken', case.remove_item(planks, 1) == 1 and case.count_item(planks) == 5)

    # --- ключи: поиск по KeyId в кейсе и в любом хранилище
    check('no key yet', case.find_item_by_key_id('Vault') is None and case.find_item_by_key_id('None') is None)
    cmd('NoFear.Inv.Give Key_Vault')
    check('vault key found', case.find_item_by_key_id('Vault') == vault and case.find_item_by_key_id('Lab') is None)
    cmd('NoFear.Inv.Give KeyCard_Lab')
    kid = entries_of(case, 'KeyCard_Lab')[0].get_editor_property('id')
    check('lab key found', case.find_item_by_key_id('Lab') == lab and case.find_wheel_slot_of(kid) == -1)
    check('empty id finds nothing', case.find_item_by_key_id('') is None)
    cmd('NoFear.Inv.Box 0')
    boxes = list(unreal.GameplayStatics.get_all_actors_with_tag(w, 'NoFearTestBox'))
    store = boxes[-1].get_component_by_class(unreal.NoFearItemContainerComponent) if boxes else None
    check('key in storage', store is not None and store.find_item_by_key_id('Vault') is None
          and store.add_item(vault, 1, -1) == 1 and store.find_item_by_key_id('Vault') == vault)
    for b in boxes:
        b.destroy_actor()

    # --- растяжка: в колесо сама, встаёт на пол перед героем с его поворотом
    cmd('NoFear.Inv.Give Tripwire 3')
    wid = entries_of(case, 'Tripwire')[0].get_editor_property('id')
    check('wire in wheel', case.find_wheel_slot_of(wid) >= 0, 'slot %d' % case.find_wheel_slot_of(wid))
    yield 0.2
    put_player(2, 0, 0)
    feet = pawn.get_actor_location().z - pawn.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
    p0 = pawn.get_actor_location()
    a = case.place_item(wire)
    ok = a is not None and a.get_class() == unreal.NoFearTripwire.static_class()
    if ok:
        loc = a.get_actor_location()
        d = math.hypot(loc.x - p0.x, loc.y - p0.y)
        check('wire placed', abs(d - 120.0) < 2.0 and abs(loc.y - p0.y) < 2.0 and abs(loc.z - feet) < 10.0
              and abs(a.get_actor_rotation().yaw) < 1.0, 'd %.0f dz %.0f yaw %.1f' % (d, loc.z - feet, a.get_actor_rotation().yaw))
        check('wire owner', a.get_owner() == pawn and a.get_instigator() == pawn)
    else:
        check('wire placed', False, str(a))
    check('wire spent', case.count_item(wire) == 2)

    put_player(2, 3, 90)
    p0 = pawn.get_actor_location()
    a = case.place_item(wire)
    if a:
        loc = a.get_actor_location()
        check('turned place', abs(loc.y - p0.y - 120.0) < 2.0 and abs(loc.x - p0.x) < 2.0 and abs(a.get_actor_rotation().yaw - 90.0) < 1.0,
              'dx %.0f dy %.0f yaw %.1f' % (loc.x - p0.x, loc.y - p0.y, a.get_actor_rotation().yaw))
    else:
        check('turned place', False)
    check('wire spent again', case.count_item(wire) == 1)

    # --- отказы: предмет остаётся
    n_wires = len(placed(unreal.NoFearTripwire))
    yield from key('I')
    check('screen refuses', case.is_screen_open() and case.place_item(wire) is None and case.count_item(wire) == 1)
    yield from key('I')
    yield 0.1

    put_player(2, 0, 0)
    p0 = pawn.get_actor_location()
    wall = face_wall()
    check('wall refuses', wall is not None and case.place_item(wire) is None and case.count_item(wire) == 1,
          'wall %s' % ('none' if wall is None else '%.0f' % wall))
    put_player(2, 0, 0)

    pawn.set_actor_location(unreal.Vector(p0.x, p0.y, p0.z + 1500.0), False, True)
    check('no floor refuses', case.place_item(wire) is None and case.count_item(wire) == 1)
    put_player(2, 0, 0)

    check('not placeable refuses', case.place_item(planks) is None and case.count_item(planks) == 5)
    check('none in case refuses', case.place_item(trap) is None)
    check('nothing spawned on refusals', len(placed(unreal.NoFearTripwire)) == n_wires)

    old_class = wire.get_editor_property('place_class')
    wire.set_editor_property('place_class', unreal.NoFearTrap.static_class())
    check('abstract class refuses', case.place_item(wire) is None and case.count_item(wire) == 1)
    if SELF_DESTROY:
        wire.set_editor_property('place_class', SELF_DESTROY)
        check('self destroyed keeps item', case.place_item(wire) is None and case.count_item(wire) == 1 and self_destroyed[0] == 1,
              'begin play %d' % self_destroyed[0])
    else:
        check('self destroyed keeps item', False, 'no python class')
    wire.set_editor_property('place_class', old_class)
    yield 0.1

    # --- капкан: последний уходит из кейса и из колеса
    cmd('NoFear.Inv.Give BearTrap')
    tid = entries_of(case, 'BearTrap')[0].get_editor_property('id')
    slot = case.find_wheel_slot_of(tid)
    put_player(2, -3, 0)
    a = case.place_item(trap)
    check('trap placed', a is not None and a.get_class() == unreal.NoFearBearTrap.static_class() and a.get_instigator() == pawn, str(a))
    check('trap spent', case.count_item(trap) == 0 and slot >= 0 and case.get_wheel_slot(slot) == 0, 'slot %d' % slot)
    cmd('NoFear.Inv.Place Tripwire')
    check('console place', case.count_item(wire) == 0 and len(placed(unreal.NoFearTripwire)) == n_wires + 1)

    for cls in (unreal.NoFearTripwire, unreal.NoFearBearTrap):
        for x in placed(cls):
            x.destroy_actor()
    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
