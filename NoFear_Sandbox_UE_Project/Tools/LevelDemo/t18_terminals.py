# -*- coding: utf-8 -*-
# T18: записки и терминал (Docs/Interactables.md, зона Z7, лаборатория к северу от вышки). Без питания терминал мёртв;
# с генератором без доступа не пускает; карта лаборатории с тела курьера даёт доступ; записка на вышке открывает панель
# чтения, учит пароль, закрывается по E и если отойти; паролем терминал отпирает и открывает дверь лаборатории и
# показывает журнал; взлом пускает без карты и пароля; без питания журнал закрывается.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []
LIMIT[0] = 90.0


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


def comp(a):
    return a.get_component_by_class(unreal.NoFearInteractableComponent)


def prompt(a):
    return str(comp(a).get_interaction_prompt(pawn))


def use(a):
    # E на предмете (как у фокуса игрока): подсказка до нажатия и удалось ли.
    c = comp(a)
    p = str(c.get_interaction_prompt(pawn)) if c.can_interact(pawn) else '-'
    return p, c.interact(pawn)


def case():
    return pawn.get_inventory_component()


def reading():
    return unreal.NoFearReadingSubsystem.get(w)


def spawn_terminal(x, y, yaw):
    # Свежий терминал без карты и пароля — для взлома (на карте он уже отперт паролем). Чит summon ставит его перед героем.
    before = set(a.get_name() for a in all_of(unreal.NoFearTerminal))
    cmd('EnableCheats')
    cmd('summon /Script/NoFear.NoFearTerminal')
    new = [a for a in all_of(unreal.NoFearTerminal) if a.get_name() not in before]
    if not new:
        log('spawn terminal failed')
        return None
    t = new[0]
    t.set_actor_location_and_rotation(V(x, y, 0.0), unreal.Rotator(0.0, 0.0, yaw), False, True)
    return t


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()

    term = nearest(unreal.NoFearTerminal, 95.3, 33.6, 0.5)
    note = nearest(unreal.NoFearNote, 97.3, 28.6, 0.5)
    door = nearest(unreal.NoFearDoor, 97, 34.15, 0.5)
    chest = nearest(unreal.NoFearChest, 98.8, 37.2, 0.5)
    body = nearest(unreal.NoFearBody, 87.6, 35.6)
    gen = nearest(unreal.NoFearGenerator, 83, 34.6)
    card = unreal.load_asset('/Game/Items/DA_Item_KeyCard_Lab')
    body_items = [e.get_editor_property('item') for e in body.get_editor_property('contents')] if body else []
    check('placed', term and note and door and chest and gen and card and door.is_locked()
          and term.get_editor_property('doors') and term.get_editor_property('doors')[0] == door)
    check('keycard on body', card in body_items, '%s' % body_items)
    rs = reading()
    check('reading subsystem', rs is not None)

    # --- без питания: экран тёмный, не пускает
    gen.stop()
    put_player(95.3, 32.6, 90)
    yield 0.4
    p, ok = use(term)
    yield 0.2
    check('no power', 'нет питания' in p and not term.is_unlocked() and rs.get_source(pawn) is None, 'prompt %s' % p)

    # --- генератор завёлся, но доступа нет
    gen.set_editor_property('start_chance', 1.0)
    gen.set_editor_property('run_noise_loudness', 0.0)
    gen.pull_start(pawn)
    yield 0.3
    check('powered', term.is_powered() and gen.is_running())
    check('hack prompt', 'Взломать' in prompt(term) and not term.has_access(pawn), 'prompt %s' % prompt(term))
    check('login denied', not term.log_in(pawn) and not term.is_unlocked() and door.is_locked())

    # --- карта лаборатории даёт доступ (не входим — проверим пароль)
    case().add_item(card, 1, -1)
    yield 0.1
    check('card access', term.has_access(pawn) and 'Войти' in prompt(term), 'prompt %s' % prompt(term))
    case().remove_item(card, 1)
    yield 0.1
    check('card removed', not term.has_access(pawn))

    # --- записка на вышке: панель, пароль, закрыть по E
    pawn.set_actor_location(V(97.0, 29.6, 3.95), False, True)
    yield 0.4
    p, ok = use(note)
    yield 0.2
    body_text = str(rs.get_body(pawn))
    check('note read', ok and 'Читать' in p and rs.get_source(pawn) == note and '4-1-7-7' in body_text
          and note.was_read(), 'prompt %s body %s' % (p, body_text[:40]))
    check('code learned', rs.knows_code(pawn, 'LabCode'))
    p, ok = use(note)
    yield 0.2
    check('note closes on E', 'Закрыть' in p and rs.get_source(pawn) is None, 'prompt %s' % p)

    # --- отошёл — панель закрылась
    use(note)
    yield 0.2
    opened = rs.get_source(pawn) == note
    pawn.set_actor_location(V(93.0, 29.6, 1.0), False, True)
    yield 0.5
    check('note closes when away', opened and rs.get_source(pawn) is None)

    # --- пароль: терминал отпирает и открывает дверь, показывает журнал
    put_player(95.3, 32.6, 90)
    yield 0.4
    p, ok = use(term)
    yield 1.2
    check('password prompt', 'пароль' in p, 'prompt %s' % p)
    check('terminal unlocked', ok and term.is_unlocked() and rs.get_source(pawn) == term and 'Журнал' in str(rs.get_body(pawn)))
    check('door unlocked', not door.is_locked() and door.is_open(), 'locked %s open %s' % (door.is_locked(), door.is_open()))
    p, ok = use(term)
    yield 0.2
    check('screen closes on E', 'Закрыть' in p and rs.get_source(pawn) is None)
    check('read log prompt', 'журнал' in prompt(term), 'prompt %s' % prompt(term))

    # --- в лабораторию есть путь
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(w, V(97, 32.5, 0.2), V(97.5, 36.2, 0.2))
    pts = list(p.get_editor_property('path_points')) if p else []
    check('path into lab', p and not p.is_partial() and pts and pts[-1].y > 3550, '%d points' % len(pts))

    # --- без питания журнал закрывается
    use(term)
    yield 0.2
    shown = rs.get_source(pawn) == term
    gen.stop()
    yield 0.3
    check('screen off without power', shown and rs.get_source(pawn) is None and 'нет питания' in prompt(term), 'prompt %s' % prompt(term))

    # --- взлом: свежий терминал без карты и пароля, двери нет
    gen.pull_start(pawn)
    yield 0.3
    t2 = spawn_terminal(91.0, 33.0, -90)
    yield 0.3
    if t2:
        t2.set_editor_property('circuit', 'Shelter')
        put_player(91.0, 32.0, 90)
        yield 0.3
        p, ok = use(t2)
        yield 0.3
        check('hack', ok and 'Взломать' in p and t2.is_unlocked() and rs.get_source(pawn) == t2, 'prompt %s' % p)
        t2.destroy_actor()
    else:
        check('hack (spawn)', False)
    gen.stop()

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
