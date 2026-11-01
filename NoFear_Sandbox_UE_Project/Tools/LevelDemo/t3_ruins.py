# -*- coding: utf-8 -*-
# T3: игрок (бессмертный) заходит в руины с юга и стреляет по стае; потом уходит на юг за полосу без навмеша.
# Ждём: стая дерётся внутри руин и не выходит за них; бешеный в загоне не выбирается.
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

G = ['ruins', 'pen', 'yard']
RUINS = (129, 89, 181, 136)
PEN = (152, 49, 164, 63)


def scenario():
    yield 1.0
    god()
    put_player(145, 93, 90)
    yield 2.0
    report(G, 'enter')
    max_out = 0
    for i in range(10):
        god()
        yield from shoot(['ruins'], 1.2)
        yield 4.0
        god()
        report(G, 'fight')
        out = escaped('ruins', RUINS)
        max_out = max(max_out, out)
        alive = sum(1 for a, _, _, _ in groups['ruins'] if not dead(a))
        log('  ruins alive=%d out=%d pen_out=%d' % (alive, out, escaped('pen', PEN)))
    log('LEAVE south')
    put_player(145, 72, 270)
    for i in range(6):
        yield 5.0
        god()
        report(G, 'after leave')
        out = escaped('ruins', RUINS)
        max_out = max(max_out, out)
        log('  ruins out=%d pen_out=%d' % (out, escaped('pen', PEN)))
    log('RESULT ruins max_out=%d pen_out=%d' % (max_out, escaped('pen', PEN)))


run(scenario)
