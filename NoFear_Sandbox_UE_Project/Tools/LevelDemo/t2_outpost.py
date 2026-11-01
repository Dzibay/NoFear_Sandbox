# -*- coding: utf-8 -*-
# T2: игрок (бессмертный) встаёт между блокпостом и рейдерами и стреляет; ждём бой, помощь резерва и панику в сарае.
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

G = ['outpost', 'raiders', 'reserve', 'shed', 'village']


def alive(g):
    return sum(1 for a, _, _, _ in groups.get(g, []) if not dead(a))


def scenario():
    yield 1.0
    god()
    put_player(106, 0, 0)
    yield 1.0
    look_at(130, 0)
    wpn = weapon()
    log('weapon %s' % wpn)
    wpn.start_fire()
    yield 1.5
    wpn.stop_fire()
    report(G, 'after burst')
    first_assist = None
    for i in range(14):
        yield 8.0
        god()
        report(G, 'fight')
        for g in ('outpost', 'raiders', 'reserve'):
            log('  ' + squad_line(g))
        for a, g, k, sp in groups['reserve']:
            if not dead(a):
                try:
                    asq = SQ.get_assisting_squad(a)
                    if asq and str(asq) != 'None' and first_assist is None:
                        first_assist = (now() - T0, str(asq))
                except Exception:
                    pass
                break
        log('  alive outpost=%d raiders=%d reserve=%d first_assist=%s' % (alive('outpost'), alive('raiders'), alive('reserve'), first_assist))
        if alive('raiders') == 0:
            break
    log('escaped shed=%d pen=%d' % (escaped('shed', (111, 14, 121, 23)), escaped('pen', (152, 49, 164, 63))))


run(scenario)
