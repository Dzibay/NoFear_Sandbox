# -*- coding: utf-8 -*-
# T13: взрыв и разрушение (Docs/Interactables.md, зона Z7_Shelter). Окно: стекло закрывает взгляд и пули, E разбивает;
# ящики ломаются (E или урон) и высыпают содержимое; бочка горит после урона, мирный убегает, через BurnTime взрыв;
# взрыв одной бочки рвёт соседние цепочкой.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
LIMIT[0] = 120.0


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def all_of(cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, cls))


def nearest(cls, x, y, r=2.0):
    best, bd = None, 1e9
    for a in all_of(cls):
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


def item(name):
    return unreal.load_asset('/Game/Items/DA_Item_' + name)


def pickups_near(x, y, r=2.5):
    out = []
    for p in all_of(unreal.NoFearPickup):
        l = p.get_actor_location()
        if math.hypot(l.x / 100.0 - x, l.y / 100.0 - y) < r:
            out.append(p)
    return out


def blocker(ax, ay, bx, by, z=1.5):
    # Первый актор на линии Visibility (как у пуль и зрения ботов).
    hit = unreal.SystemLibrary.line_trace_single(w, V(ax, ay, z), V(bx, by, z), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                 False, [pawn], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    for v in hit.to_tuple():
        if isinstance(v, unreal.Actor):
            return v
    return None


def spawn_bot(kind, x, y):
    before = set(a.get_name() for a in all_of(unreal.NoFearCharacter))
    cmd('NoFearSpawnBot %s 1' % kind)
    new = [a for a in all_of(unreal.NoFearCharacter) if a.get_name() not in before]
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

    windows = all_of(unreal.NoFearWindow)
    barrels = all_of(unreal.NoFearExplosiveBarrel)
    crates = all_of(unreal.NoFearBreakableCrate)
    log('windows %d barrels %d crates %d' % (len(windows), len(barrels), len(crates)))
    window = nearest(unreal.NoFearWindow, 85.85, 30)
    chain = [nearest(unreal.NoFearExplosiveBarrel, x, y, 0.5) for x, y in [(88, 24), (88.8, 24.4), (88.3, 25.2)]]
    lone = nearest(unreal.NoFearExplosiveBarrel, 93, 19, 0.5)
    c_planks = nearest(unreal.NoFearBreakableCrate, 90, 30, 0.5)
    c_supply = nearest(unreal.NoFearBreakableCrate, 91.2, 31.2, 0.5)
    c_traps = nearest(unreal.NoFearBreakableCrate, 90.2, 32.4, 0.5)
    check('placed', len(windows) == 1 and len(barrels) == 4 and len(crates) == 3 and window and all(chain) and lone
          and c_planks and c_supply and c_traps)

    # --- окно: мутное стекло закрывает линию, E разбивает
    b = blocker(88.0, 30.0, 83.0, 30.0)
    check('glass blocks line', b == window, 'hit %s' % (b.get_name() if b else None))
    put_player(84.6, 30.0, 0)
    yield 0.5
    look_at(85.85, 30.0, 1.4)
    yield 0.3
    got, prompt = press_e()
    yield 0.6
    check('break window by E', got == window and window.is_broken() and 'Разбить окно' in prompt,
          'got %s prompt %s' % (got.get_name() if got else None, prompt))
    b = blocker(88.0, 30.0, 83.0, 30.0)
    check('line clear after break', b is None or b == pawn, 'hit %s' % (b.get_name() if b else None))
    check('frame stays', alive(window))

    # --- ящик: E разбивает (без удержания клавиши — сразу), доски на полу
    put_player(89.0, 29.0, 0)
    yield 0.5
    look_at(90.0, 30.0, 0.3)
    yield 0.3
    got, prompt = press_e()
    yield 1.0
    planks = [p for p in pickups_near(90, 30, 2.5) if p.get_editor_property('item') == item('Planks')]
    check('crate broken by E', got == c_planks and not alive(c_planks) and 'Разбить ящик' in prompt,
          'got %s prompt %s' % (got.get_name() if got else None, prompt))
    check('crate drops contents', len(planks) == 1 and planks[0].get_editor_property('count') == 2,
          'pickups %d' % len(planks))

    # --- ящик: урон ломает
    hp = c_supply.get_health()
    unreal.GameplayStatics.apply_damage(c_supply, 15.0, pc, pawn, None)
    yield 0.2
    check('crate takes damage', alive(c_supply) and c_supply.get_health() < hp, 'hp %.0f -> %.0f' % (hp, c_supply.get_health()))
    unreal.GameplayStatics.apply_damage(c_supply, 20.0, pc, pawn, None)
    yield 1.0
    want = (item('PistolAmmo'), item('Bandage'))
    near = [p for p in pickups_near(91.2, 31.2, 2.5) if p.get_editor_property('item') in want]
    names = sorted(str(p.get_editor_property('item').get_name()) for p in near)
    check('crate broken by damage', not alive(c_supply) and len(near) == 2, 'pickups %s' % names)

    # --- бочка: урон поджигает, мирный убегает, через BurnTime взрыв
    put_player(84.0, 14.0, 0)
    civ = spawn_bot('Civilian', 94.0, 20.5)
    yield 1.0
    d0 = dist_m(civ.get_actor_location(), lone.get_actor_location()) if civ else 0
    unreal.GameplayStatics.apply_damage(lone, 25.0, pc, pawn, None)
    t0 = now()
    yield 0.3
    check('barrel ignites', alive(lone) and lone.is_burning() and not lone.has_exploded(), 'hp %.0f' % lone.get_health())
    yield 2.5
    d1 = dist_m(civ.get_actor_location(), lone.get_actor_location()) if civ and alive(lone) else 0
    check('civilian flees fire', civ is not None and d1 > d0 + 1.5, 'dist %.1f -> %.1f' % (d0, d1))
    exploded = None
    for i in range(30):
        yield 0.2
        if not alive(lone) or lone.has_exploded():
            exploded = now() - t0
            break
    check('barrel explodes after burn', exploded is not None and 3.5 < exploded < 5.5, 'after %s s' % exploded)
    if civ and alive(civ):
        civ.destroy_actor()

    # --- цепочка: одна бочка рвёт соседние
    unreal.GameplayStatics.apply_damage(chain[0], 100.0, pc, pawn, None)
    yield 2.0
    left = [b for b in chain if alive(b) and not b.has_exploded()]
    check('chain reaction', not left, 'left %s' % [b.get_name() for b in left])
    check('far crate untouched', alive(c_traps) and not c_traps.is_broken(), 'hp %.0f' % (c_traps.get_health() if alive(c_traps) else -1))
    blocked = path(87.0, 23.0, 89.5, 26.0)
    check('path through barrel spot', blocked.startswith('ok'), blocked)

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
