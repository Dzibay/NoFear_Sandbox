# -*- coding: utf-8 -*-
# T6: игрок (бессмертный) у ворот арены завязывает бой с отрядом. Ждём: бешеные из камер слышат стрельбу,
# выходят по коридорам в арену и дерутся с отрядом.
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

G = ['arena', 'cells']


def cell_status():
    out = []
    for a, g, k, sp in groups['cells']:
        if dead(a):
            out.append('%s:DEAD' % k[:5])
            continue
        l = a.get_actor_location()
        x, y = l.x / 100, l.y / 100
        where = 'cell' if (x < 13 or y > 273.5) else ('ARENA' if (x > 40 or y < 245) else 'corr')
        out.append('%s:%s' % (k[:5], where))
    return ' '.join(out)


def scenario():
    yield 1.0
    god()
    put_player(84, 227, 180)
    yield 2.0
    report(G, 'start')
    first_arena = None
    for i in range(18):
        god()
        if i < 3 or i % 3 == 0:
            yield from shoot(['arena'], 0.8)
        yield 5.0
        god()
        report(G, 'fight')
        st = cell_status()
        if first_arena is None and 'ARENA' in st:
            first_arena = now() - T0
        alive = sum(1 for a, _, _, _ in groups['arena'] if not dead(a))
        log('  cells %s | squad alive=%d first_arena=%s' % (st, alive, first_arena))
        log('  ' + squad_line('arena'))
    log('RESULT cells %s first_arena=%s squad_alive=%d' % (cell_status(), first_arena, sum(1 for a, _, _, _ in groups['arena'] if not dead(a))))


run(scenario)
