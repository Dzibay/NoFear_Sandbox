# -*- coding: utf-8 -*-
# T14: генератор и свет (Docs/Interactables.md, зона Z7_Shelter). Без генератора лампы комнаты не горят, в комнате темно;
# выключатель без питания говорит «Нет питания»; генератор заводится, питает линию Shelter — лампы горят, светло;
# монстр идёт на шум генератора; выключатель гасит лампы; выстрел бьёт плафон; кончилось топливо — генератор глохнет;
# сломанный уроном генератор не заводится.
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


def nearest(cls, x, y, r=1.0):
    best, bd = None, 1e9
    for a in all_of(cls):
        l = a.get_actor_location()
        d = math.hypot(l.x / 100.0 - x, l.y / 100.0 - y)
        if d < bd:
            best, bd = a, d
    return best if bd < r else None


def press_e():
    f = pc.get_focused_interactable()
    if f is None:
        return None, ''
    prompt = str(f.get_interaction_prompt(pawn))
    f.interact(pawn)
    return f.get_owner(), prompt


def light(x, y, z=1.0):
    return unreal.NoFearLightSubsystem.get_light_level(w, V(x, y, z))


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

    gen = nearest(unreal.NoFearGenerator, 83, 34.6)
    lamps = [nearest(unreal.NoFearLamp, x, y) for x, y in [(81.8, 30), (84.2, 30)]]
    porch = nearest(unreal.NoFearLamp, 76.5, 31.2)
    sw = nearest(unreal.NoFearLightSwitch, 80.32, 28.6)
    check('placed', gen and all(lamps) and porch and sw and len(sw.get_lamps()) == 2)

    # --- без генератора: темно, выключатель не помогает
    check('dark without power', not any(l.is_lit() for l in lamps) and light(83, 30) < 0.2 and porch.is_lit(),
          'level %.2f porch %s' % (light(83, 30), porch.is_lit()))
    put_player(81.0, 28.6, 180)
    yield 0.5
    look_at(80.32, 28.6, 1.3)
    yield 0.3
    on0 = sw.is_on()
    got, prompt = press_e()
    yield 0.3
    got2, prompt2 = press_e()
    yield 0.3
    check('switch toggles', got == sw and sw.is_on() == on0 and 'свет' in prompt, 'got %s prompt %s' % (got.get_name() if got else None, prompt))
    check('no light without power', sw.is_on() and not any(l.is_lit() for l in lamps))

    # --- генератор: E заводит (шанс 1 для теста), линия питается, лампы горят
    gen.set_editor_property('start_chance', 1.0)
    put_player(83.0, 36.0, 270)
    yield 0.5
    look_at(83.0, 34.6, 0.5)
    yield 0.3
    got, prompt = press_e()
    yield 0.5
    check('generator started by E', got == gen and gen.is_running() and 'Завести' in prompt,
          'got %s prompt %s fuel %.0f' % (got.get_name() if got else None, prompt, gen.get_fuel()))
    check('lamps lit', all(l.is_lit() for l in lamps) and light(83, 30) > 0.6, 'level %.2f' % light(83, 30))

    # --- монстр идёт на шум генератора
    mon = spawn_bot('Monster', 83.0, 52.0)
    # Дойти до генератора, а не просто идти к герою: ближайшее расстояние за окно.
    d0 = dist_m(mon.get_actor_location(), gen.get_actor_location()) if mon else 0
    d1 = 1e9
    for _ in range(24):
        yield 0.5
        if mon:
            d1 = min(d1, dist_m(mon.get_actor_location(), gen.get_actor_location()))
    check('monster comes to noise', mon is not None and d1 < 3.5, 'dist %.1f -> min %.1f' % (d0, d1))
    if mon:
        mon.destroy_actor()
    yield 0.5

    # --- выключатель гасит лампы при работающем генераторе
    sw.set_on(False, pawn)
    yield 0.2
    check('switch off', not any(l.is_lit() for l in lamps) and light(83, 30) < 0.2)
    sw.set_on(True, pawn)
    yield 0.2

    # --- выстрел бьёт плафон
    unreal.GameplayStatics.apply_damage(lamps[0], 10.0, pc, pawn, None)
    yield 0.3
    check('bulb shot', lamps[0].is_broken() and not lamps[0].is_lit() and lamps[1].is_lit())

    # --- топливо кончилось — глохнет, лампы гаснут
    gen.set_fuel(0.5)
    yield 1.5
    check('out of fuel', not gen.is_running() and gen.get_fuel() <= 0.0 and not lamps[1].is_lit(),
          'running %s fuel %.1f' % (gen.is_running(), gen.get_fuel()))
    got, prompt = press_e()
    yield 0.3
    check('empty tank prompt', not gen.is_running() and ('Бак пуст' in prompt or 'Заправить' in prompt), 'prompt %s' % prompt)

    # --- канистра из кейса заправляет бак
    cmd('NoFear.Inv.Give Fuel')
    yield 0.2
    fuel_item = unreal.load_asset('/Game/Items/DA_Item_Fuel')
    n0 = pawn.get_inventory_component().count_item(fuel_item)
    got, prompt = press_e()
    yield 0.3
    check('refuel from case', 'Заправить' in prompt and gen.get_fuel_fraction() > 0.99
          and pawn.get_inventory_component().count_item(fuel_item) == n0 - 1,
          'prompt %s fuel %.0f cans %d->%d' % (prompt, gen.get_fuel(), n0, pawn.get_inventory_component().count_item(fuel_item)))

    # --- сломанный генератор не заводится и стоит на месте
    gen.set_fuel(60.0)
    unreal.GameplayStatics.apply_damage(gen, 500.0, pc, pawn, None)
    yield 0.5
    check('generator broken', gen.is_broken() and not gen.pull_start(pawn) and not gen.is_running()
          and unreal.SystemLibrary.is_valid(gen))

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
