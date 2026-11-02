# -*- coding: utf-8 -*-
# T16: ключи, тело, радио, сирена (Docs/Interactables.md, зоны Z6 и Z7). Без ключа склад заперт; тело обыскивается и
# открывает экран вещей с ключом; ключ из кейса отпирает дверь склада и сундук; радио играет, приманивает монстра,
# садится батарея, ломается выстрелом; сирена без питания молчит, с генератором воет и собирает монстров,
# без питания глохнет, остывает, ломается уроном.
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


def comp(a):
    return a.get_component_by_class(unreal.NoFearInteractableComponent)


def use(a):
    # E на предмете (как у фокуса игрока), возвращает подсказку до нажатия.
    c = comp(a)
    prompt = str(c.get_interaction_prompt(pawn))
    c.interact(pawn)
    return prompt


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


def lured(mon, target, secs=14.0):
    # Монстр должен дойти до target, а не просто идти в сторону героя: ближайшее расстояние за окно.
    d0, best = 0.0, 1e9
    if mon:
        d0 = dist_m(mon.get_actor_location(), target.get_actor_location())
    t = 0.0
    while t < secs:
        yield 0.5
        t += 0.5
        if mon:
            best = min(best, dist_m(mon.get_actor_location(), target.get_actor_location()))
        if mon and t in (2.0, 6.0, 10.0):
            l = mon.get_actor_location()
            log('lure t%.0f mon (%.1f, %.1f) %s' % (t, l.x / 100, l.y / 100, state(mon)))
    LURE[0] = (d0, best)


LURE = [(0.0, 0.0)]


def case():
    return pawn.get_inventory_component()


def scenario():
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()

    body = nearest(unreal.NoFearBody, 87.6, 35.6)
    radio = nearest(unreal.NoFearRadio, 77.5, 32.45)
    siren = nearest(unreal.NoFearSiren, 92.5, 27.5)
    gen = nearest(unreal.NoFearGenerator, 83, 34.6)
    vdoor = nearest(unreal.NoFearDoor, 64.6, 36.15)
    chest = nearest(unreal.NoFearChest, 71, 31)
    key = unreal.load_asset('/Game/Items/DA_Item_Key_Vault')
    check('placed', body and radio and siren and gen and vdoor and chest and key
          and str(vdoor.get_editor_property('key_id')) == 'Vault')

    # --- без ключа дверь склада заперта
    put_player(64.6, 34.9, 90)
    yield 0.4
    p = use(vdoor)
    yield 0.3
    check('locked without key', vdoor.is_locked() and 'Заперто' in p, 'prompt %s' % p)

    # --- тело: обыск, экран вещей, ключ в кейс
    put_player(87.6, 34.4, 90)
    yield 0.4
    p = use(body)
    yield 0.3
    store = body.get_storage()
    check('body searched', body.is_searched() and 'Обыскать' in p and case().get_open_container() == store,
          'prompt %s open %s' % (p, case().get_open_container()))
    check('key on body', store.find_item_by_key_id('Vault') == key)
    store.remove_item(key, 1)
    case().add_item(key, 1, -1)
    case().close_screen()
    yield 0.2
    check('key in case', case().find_item_by_key_id('Vault') == key and store.find_item_by_key_id('Vault') is None)
    check('body prompt after', 'Вещи' in str(comp(body).get_interaction_prompt(pawn)))

    # --- ключ отпирает дверь склада и открывает её
    put_player(64.6, 34.9, 90)
    yield 0.4
    p = use(vdoor)
    yield 1.0
    check('key unlocks door', not vdoor.is_locked() and vdoor.is_open() and 'Отпереть' in p
          and case().count_item(key) == 1, 'prompt %s open %s' % (p, vdoor.is_open()))

    # --- и сундук с тем же KeyId
    chest.set_editor_property('key_id', 'Vault')
    put_player(70.0, 31.0, 0)
    yield 0.4
    p = use(chest)
    yield 0.5
    check('key unlocks chest', not chest.is_locked() and chest.is_open() and 'Отпереть' in p, 'prompt %s' % p)
    case().close_screen()
    yield 0.2

    # --- радио: включается, приманивает монстра
    put_player(78.5, 31.5, 180)
    yield 0.3
    p = use(radio)
    yield 0.3
    check('radio on', radio.is_on() and 'Включить радио' in p, 'prompt %s' % p)
    put_player(64.6, 33.0, 0)
    mon = spawn_monster(83.0, 52.0)
    yield from lured(mon, radio)
    d0, d1 = LURE[0]
    check('monster comes to radio', mon is not None and d1 < 3.5, 'dist %.1f -> min %.1f' % (d0, d1))
    if mon:
        mon.destroy_actor()
    yield 0.3

    # --- батарея садится — радио молчит и не включается
    radio.set_battery(0.4)
    yield 1.0
    on_after = radio.is_on()
    put_player(78.5, 31.5, 180)
    yield 0.2
    p = use(radio)
    yield 0.2
    check('battery dies', not on_after and not radio.is_on() and 'батарея' in p, 'prompt %s' % p)
    radio.set_battery(30.0)
    radio.set_on(True, pawn)
    unreal.GameplayStatics.apply_damage(radio, 20.0, pc, pawn, None)
    yield 0.3
    check('radio shot', radio.is_broken() and not radio.is_on() and not radio.set_on(True, pawn))

    # --- сирена: без питания молчит
    put_player(91.3, 27.5, 0)
    yield 0.3
    p = use(siren)
    yield 0.2
    check('siren needs power', not siren.is_sounding() and 'нет питания' in p, 'prompt %s' % p)

    # --- генератор даёт питание — сирена воет, монстр идёт к ней
    gen.set_editor_property('start_chance', 1.0)
    # Генератор рядом и тоже шумит — глушим его шум, чтобы проверить именно сирену.
    gen.set_editor_property('run_noise_loudness', 0.0)
    gen.pull_start(pawn)
    yield 0.3
    p = use(siren)
    yield 0.3
    check('siren wails', gen.is_running() and siren.is_sounding() and 'Включить сирену' in p, 'prompt %s gen %s' % (p, gen.is_running()))
    put_player(64.6, 33.0, 0)
    mon = spawn_monster(83.0, 52.0)
    yield from lured(mon, siren, 18.0)
    d0, d1 = LURE[0]
    check('monster comes to siren', mon is not None and d1 < 3.5, 'dist %.1f -> min %.1f' % (d0, d1))
    if mon:
        mon.destroy_actor()
    yield 0.3

    # --- генератор заглох — сирена замолкает и остывает
    gen.stop()
    yield 0.3
    check('siren loses power', not siren.is_sounding() and siren.get_cooldown_left() > 0.0, 'cooldown %.1f' % siren.get_cooldown_left())
    gen.pull_start(pawn)
    yield 0.2
    check('siren cooldown', not siren.activate(pawn) and 'остывает' in str(comp(siren).get_interaction_prompt(pawn)))

    # --- сломанная не воет
    unreal.GameplayStatics.apply_damage(siren, 500.0, pc, pawn, None)
    yield 0.4
    check('siren broken', siren.is_broken() and not siren.is_sounding() and unreal.SystemLibrary.is_valid(siren))
    gen.stop()

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
