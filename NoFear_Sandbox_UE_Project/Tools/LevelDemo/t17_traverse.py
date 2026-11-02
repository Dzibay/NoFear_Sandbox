# -*- coding: utf-8 -*-
# T17: лестница, перелаз, лифт (Docs/Interactables.md, зона Z7, вышка и забор к востоку от двора). Герой поднимается
# и спускается по лестнице по E, перелезает забор; монстр лезет за ним на вышку и через забор по навигационным
# ссылкам; лифт без питания стоит, с генератором возит вверх и вниз, кнопки вызывают его, обрыв питания его
# останавливает, а с питанием он доезжает.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
LIMIT[0] = 150.0


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def all_of(cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, cls))


def nearest(cls, x, y, r=1.5):
    best, bd = None, 1e9
    for a in all_of(cls):
        l = a.get_actor_location()
        d = math.hypot(l.x / 100.0 - x, l.y / 100.0 - y)
        if d < bd:
            best, bd = a, d
    return best if bd < r else None


def handle(owner, index=0):
    for h in all_of(unreal.NoFearTraversalHandle):
        if h.get_traversal() == owner and h.get_editor_property('index') == index:
            return h
    return None


def use(a):
    # E на ручке или кнопке (как у фокуса игрока): подсказка до нажатия и удалось ли.
    c = a.get_component_by_class(unreal.NoFearInteractableComponent)
    if c is None:
        return '', False
    prompt = str(c.get_interaction_prompt(pawn)) if c.can_interact(pawn) else '-'
    return prompt, c.interact(pawn)


def walking(p):
    return p.get_movement_component().movement_mode == unreal.MovementMode.MOVE_WALKING


def loc():
    l = pawn.get_actor_location()
    return '(%.2f, %.2f, %.2f)' % (l.x / 100, l.y / 100, l.z / 100)


def spawn_monster(x, y):
    before = set(a.get_name() for a in all_of(unreal.NoFearCharacter))
    cmd('NoFearSpawnBot Monster 1')
    new = [a for a in all_of(unreal.NoFearCharacter) if a.get_name() not in before]
    if not new:
        return None
    b = new[0]
    p = proj(x, y) or V(x, y, 1.0)
    b.set_actor_location(unreal.Vector(p.x, p.y, p.z + 100.0), False, True)
    b.get_controller().set_home_location(b.get_actor_location())
    return b


SEEN = [None]


def follow(mon, trav, cond, secs, noisy=False):
    # Ждём, пока монстр выполнит cond (заберётся, перелезет); заодно отмечаем, проходил ли он объект trav.
    SEEN[0] = (False, False)
    used = done = False
    t = 0.0
    while t < secs and mon:
        yield 0.25
        t += 0.25
        used = used or trav.is_traversing(mon)
        if noisy and abs(t - round(t / 2.0) * 2.0) < 0.01:
            # Герой шумит (как выстрел) — монстр идёт на звук.
            unreal.AISense_Hearing.report_noise_event(w, pawn.get_actor_location(), 1.0, pawn, 3000.0, 'Test')
        if cond(mon.get_actor_location()):
            done = True
            break
        if abs(t - round(t / 3.0) * 3.0) < 0.01:
            l = mon.get_actor_location()
            log('mon t%.0f (%.2f, %.2f, %.2f) %s' % (t, l.x / 100, l.y / 100, l.z / 100, state(mon)))
    SEEN[0] = (used, done)


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()

    ladder = nearest(unreal.NoFearLadder, 95, 29.5)
    lift = nearest(unreal.NoFearLift, 99.15, 29.5)
    vault = nearest(unreal.NoFearVault, 102, 30)
    btn_ground = nearest(unreal.NoFearLiftButton, 99.15, 27.7, 0.3)
    btn_deck = nearest(unreal.NoFearLiftButton, 97.7, 30.6, 0.3)
    gen = nearest(unreal.NoFearGenerator, 83, 34.6)
    check('placed', ladder and lift and vault and btn_ground and btn_deck and gen
          and handle(ladder, 0) and handle(ladder, 1) and handle(lift) and handle(vault))

    # --- навигация: площадка в навмеше, путь с земли наверх идёт через лестницу
    try:
        r = NAV.project_point_to_navigation(w, V(96.6, 29.5, 3.2), None, None, unreal.Vector(50, 50, 100))
        r = r[0] if isinstance(r, tuple) else r
        deck = r if r and r.z > 250 else None
    except Exception:
        deck = None
    check('deck navmesh', deck is not None, '%s' % deck)
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(w, V(92, 29.5, 0.2), V(96.6, 29.5, 3.1))
    pts = list(p.get_editor_property('path_points')) if p else []
    check('path up via ladder', p and not p.is_partial() and pts and pts[-1].z > 250,
          'points %s' % ['(%.1f %.1f %.1f)' % (q.x / 100, q.y / 100, q.z / 100) for q in pts])

    # --- лестница вверх
    put_player(94.3, 29.5, 0)
    yield 0.3
    prompt, ok = use(handle(ladder, 0))
    yield 0.3
    busy = ladder.is_traversing(pawn)
    yield 4.0
    check('ladder up', ok and busy and 'Подняться' in prompt and pawn.get_actor_location().z > 350 and walking(pawn)
          and not ladder.is_traversing(pawn), 'prompt %s at %s' % (prompt, loc()))
    check('ladder top prompt', 'Спуститься' in str(handle(ladder, 1).get_component_by_class(unreal.NoFearInteractableComponent).get_interaction_prompt(pawn)))
    check('bottom handle off on top', not handle(ladder, 0).get_component_by_class(unreal.NoFearInteractableComponent).can_interact(pawn))

    # --- лестница вниз
    prompt, ok = use(handle(ladder, 1))
    yield 4.5
    check('ladder down', ok and pawn.get_actor_location().z < 130 and walking(pawn), 'prompt %s at %s' % (prompt, loc()))

    # --- перелаз
    put_player(101.0, 30.4, 0)
    yield 0.3
    prompt, ok = use(handle(vault))
    yield 1.8
    l = pawn.get_actor_location()
    check('vault over', ok and 'Перелезть' in prompt and l.x > 10260 and l.z < 130 and walking(pawn), 'at %s' % loc())
    prompt, ok = use(handle(vault))
    yield 1.8
    check('vault back', ok and pawn.get_actor_location().x < 10140, 'at %s' % loc())
    put_player(104.5, 30.4, 0)
    yield 0.2
    check('vault out of reach', not handle(vault).get_component_by_class(unreal.NoFearInteractableComponent).can_interact(pawn))

    # --- лифт без питания
    gen.stop()
    put_player(98.9, 29.3, 0)
    yield 0.4
    prompt, ok = use(handle(lift))
    yield 0.5
    check('lift needs power', not ok or not lift.is_moving(), 'prompt %s' % prompt)
    check('lift no power prompt', 'нет питания' in prompt and lift.get_current_floor() == 0, 'prompt %s' % prompt)

    # --- с генератором: вверх и вниз с героем
    gen.set_editor_property('start_chance', 1.0)
    gen.set_editor_property('run_noise_loudness', 0.0)
    gen.pull_start(pawn)
    yield 0.3
    prompt, ok = use(handle(lift))
    yield 3.5
    check('lift up', gen.is_running() and ok and 'вверх' in prompt and lift.get_current_floor() == 1
          and pawn.get_actor_location().z > 350, 'prompt %s at %s' % (prompt, loc()))
    prompt, ok = use(handle(lift))
    yield 3.5
    check('lift down', ok and 'вниз' in prompt and lift.get_current_floor() == 0 and pawn.get_actor_location().z < 130,
          'prompt %s at %s' % (prompt, loc()))

    # --- кнопка на площадке вызывает лифт наверх
    put_player(97.0, 30.0, 0)
    pawn.set_actor_location(V(97.0, 30.0, 3.95), False, True)
    yield 0.3
    prompt, ok = use(btn_deck)
    yield 3.5
    check('call button', ok and 'Вызвать' in prompt and lift.get_current_floor() == 1, 'prompt %s' % prompt)
    check('call here off', not btn_deck.get_component_by_class(unreal.NoFearInteractableComponent).can_interact(pawn))

    # --- обрыв питания на ходу: стоит; питание вернулось — доезжает
    put_player(99.15, 26.8, 90)
    yield 0.3
    prompt, ok = use(btn_ground)
    yield 1.0
    gen.stop()
    yield 0.2
    z0 = lift.get_actor_location().z
    yield 1.0
    z1 = lift.get_actor_location().z
    check('lift stops without power', ok and lift.is_moving() and abs(z1 - z0) < 1.0 and 30 < z1 < 290, 'z %.0f -> %.0f' % (z0, z1))
    gen.pull_start(pawn)
    yield 3.0
    check('lift resumes', lift.get_current_floor() == 0, 'floor %d z %.0f' % (lift.get_current_floor(), lift.get_actor_location().z))
    gen.stop()

    # --- монстр лезет на вышку за героем
    # Герой у проёма в перилах над лестницей — монстр снизу его видит.
    pawn.set_actor_location(V(96.2, 29.5, 3.95), False, True)
    pc.set_control_rotation(unreal.Rotator(0.0, 0.0, 180))
    mon = spawn_monster(90.5, 29.5)
    yield from follow(mon, ladder, lambda l: l.z > 300, 20.0, noisy=True)
    used, done = SEEN[0]
    check('monster climbs ladder', mon is not None and used and done, 'used %s up %s' % (used, done))
    if mon:
        mon.destroy_actor()
    yield 0.3

    # --- монстр перелезает забор за героем
    put_player(99.0, 33.5, 0)
    mon = spawn_monster(106.0, 30.0)
    yield from follow(mon, vault, lambda l: l.x < 10150, 20.0, noisy=True)
    used, done = SEEN[0]
    check('monster vaults fence', mon is not None and used and done, 'used %s over %s' % (used, done))
    if mon:
        mon.destroy_actor()

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
