# -*- coding: utf-8 -*-
# T5: игрок (бессмертный) у южного входа склада стреляет по отряду. Ждём: бой, отход к задней линии, пленные сбегают со склада.
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

G = ['warehouse']
ROOM = (130.5, 232, 138.5, 239.5)


def prisoners_out():
    n = 0
    for a, g, k, sp in groups['warehouse']:
        if k == 'Civilian' and not dead(a):
            l = a.get_actor_location()
            if not (ROOM[0] <= l.x / 100 <= ROOM[2] and ROOM[1] <= l.y / 100 <= ROOM[3]):
                n += 1
    return n


def scenario():
    yield 1.0
    god()
    put_player(154, 209, 90)
    yield 2.0
    report(G, 'start')
    for i in range(12):
        god()
        yield from shoot(['warehouse'], 1.0)
        yield 5.0
        god()
        report(G, 'fight')
        log('  ' + squad_line('warehouse'))
        alive = sum(1 for a, _, k, _ in groups['warehouse'] if k != 'Civilian' and not dead(a))
        log('  wh alive=%d prisoners_out=%d' % (alive, prisoners_out()))
        if alive == 0:
            break
    yield 6.0
    report(G, 'end')
    log('RESULT prisoners_out=%d' % prisoners_out())


run(scenario)
