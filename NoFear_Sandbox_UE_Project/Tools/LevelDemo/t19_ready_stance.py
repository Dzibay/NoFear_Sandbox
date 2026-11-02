# -*- coding: utf-8 -*-
# T19: боевая стойка героя (Docs/Weapons_Setup.md, «Боевая стойка»). Без ПКМ GASP держит оружие внизу; пока зажат огонь и
# ещё ReadyStanceHold (2.5 с) после выстрела, герой держит оружие перед собой: компонент пишет WantsToStrafe (корпус по
# камере) и на время расчёта анимации WantsToAim. Прицельная камера при этом не включается (она на ПКМ), ПКМ работает как
# раньше, боты стойкой не ведаются, убранное оружие и лечение стойку снимают.
# Сама поза проверяется по скриншотам (в -nullrhi анимации нет): здесь флаг стойки и положение камеры.
# Клавиши идут через NoFear.Inv.Key, как с клавиатуры.
import os
KIT = False
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

fails = []


def check(name, cond, detail=''):
    log('%s %s %s' % ('PASS' if cond else 'FAIL', name, detail))
    if not cond:
        fails.append(name)


def press(name):
    cmd('NoFear.Inv.Key %s press' % name)


def release(name):
    cmd('NoFear.Inv.Key %s release' % name)


def cam():
    return unreal.GameplayStatics.get_player_camera_manager(w, 0).get_camera_location()


def dist(a, b):
    return (a - b).length()


def scenario():
    wc = pawn.get_weapon_component()
    case = pawn.get_inventory_component()

    # --- боты стойкой не ведаются: WantsToAim им ставит контроллер
    bot = find_bot('Enemy')
    bw = bot.get_weapon_component() if bot else None
    check('bot has a weapon component', bw is not None)
    if bw is not None:
        bw.start_fire()
        yield 0.5
        check('bot is not in the ready stance', not bw.is_ready_stance())
        bw.stop_fire()
    for a, _, _, _ in bots:
        try:
            a.destroy_actor()
        except Exception:
            pass
    yield 1.0
    god()
    put_player(2, 0, 0)
    cmd('NoFear.Inv.Give Bandage 3')
    yield 3.0

    # --- в покое оружие внизу, камера обычная
    cam0 = cam()
    check('idle: not ready', not wc.is_ready_stance() and not wc.is_aiming())

    # --- ЛКМ поднимает оружие; камера остаётся обычной
    press('LeftMouseButton')
    yield 0.6
    check('fire: ready', wc.is_ready_stance() and not wc.is_aiming(), 'ready %s aiming %s' % (wc.is_ready_stance(), wc.is_aiming()))
    check('fire: camera stays', dist(cam(), cam0) < 12.0, 'moved %.1f cm' % dist(cam(), cam0))
    release('LeftMouseButton')
    yield 1.5
    check('hold: still ready 1.5 s after the shot', wc.is_ready_stance())
    yield 1.2
    check('hold over: lowered', not wc.is_ready_stance())
    yield 0.5
    check('lowered: camera is the usual one', dist(cam(), cam0) < 12.0, 'moved %.1f cm' % dist(cam(), cam0))

    # --- ПКМ прицеливается как раньше: камера уходит на плечо, стойка не нужна
    press('RightMouseButton')
    yield 1.5
    cam_aim = cam()
    check('RMB: aiming without the stance', wc.is_aiming() and not wc.is_ready_stance())
    check('RMB: aim camera engages', dist(cam_aim, cam0) > 15.0, 'moved %.1f cm' % dist(cam_aim, cam0))

    # --- огонь в прицеле: стойка поднимается и опускается, ПКМ и прицельная камера не теряются
    press('LeftMouseButton')
    yield 0.5
    check('aim+fire: ready too', wc.is_ready_stance() and wc.is_aiming())
    check('aim+fire: camera stays aimed', dist(cam(), cam_aim) < 12.0, 'moved %.1f cm' % dist(cam(), cam_aim))
    release('LeftMouseButton')
    yield 3.2
    check('aim+fire over: stance lowered, RMB still aiming', not wc.is_ready_stance() and wc.is_aiming())
    check('aim+fire over: aim camera kept', dist(cam(), cam_aim) < 12.0, 'moved %.1f cm' % dist(cam(), cam_aim))
    release('RightMouseButton')
    yield 1.5
    check('RMB released: not aiming', not wc.is_aiming() and not wc.is_ready_stance())
    check('RMB released: camera back', dist(cam(), cam0) < 12.0, 'moved %.1f cm' % dist(cam(), cam0))

    # --- убранное оружие стойку снимает, даже если только что стреляли
    press('LeftMouseButton')
    yield 0.3
    release('LeftMouseButton')
    check('shot: ready', wc.is_ready_stance())
    wc.holster_weapon()
    yield 2.5
    check('holstered: not ready', not wc.is_ready_stance() and wc.get_current_weapon() is None)
    wc.equip_weapon(0)
    yield 3.5
    check('drawn again: not ready until a shot', not wc.is_ready_stance() and wc.get_current_weapon() is not None)

    # --- RaiseWeapon поднимает оружие на заданное время
    wc.raise_weapon(1.0)
    yield 0.4
    check('RaiseWeapon: ready', wc.is_ready_stance())
    yield 1.2
    check('RaiseWeapon: lowered after its time', not wc.is_ready_stance())

    # --- лечение: руки заняты бинтом, оружие не поднимаем
    pawn.get_health_component().set_health(50000.0)
    press('LeftMouseButton')
    yield 0.3
    release('LeftMouseButton')
    check('before healing: ready', wc.is_ready_stance())
    band = unreal.load_asset('/Game/Items/DA_Item_Bandage')
    started = case.start_healing(band)
    yield 0.4
    check('healing: not ready', started and case.is_healing() and not wc.is_ready_stance())
    case.cancel_healing(False)
    yield 0.3

    log('RESULT %s' % ('OK' if not fails else 'FAILED ' + ', '.join(fails)))


run(scenario)
