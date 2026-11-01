# -*- coding: utf-8 -*-
# T1: связность навмеша, перепись ботов, 60 с простоя (никто не должен убежать из своей зоны).
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

CHECKS = [
    # (что, откуда, куда, ожидание)
    ('start->village', 3, 0, 20, 0, 'ok'),
    ('start->outpost', 3, 0, 90, 0, 'ok'),
    ('start->raiders', 3, 0, 135, 0, 'ok'),
    ('start->reserve', 3, 0, 52, -15, 'ok'),
    ('start->ruinsGate', 3, 0, 145.5, 66, 'ok'),
    ('start->ruinsInside', 3, 0, 158, 115, 'PARTIAL'),
    ('start->shedInside', 3, 0, 116, 19, 'PARTIAL'),
    ('start->penInside', 3, 0, 158, 56, 'PARTIAL'),
    ('ruins->outside', 158, 115, 145.5, 66, 'PARTIAL'),
    ('shed->outside', 116, 19, 116, 11, 'PARTIAL'),
    ('pen->outside', 158, 56, 150, 56, 'PARTIAL'),
    ('yard->warehouse', 156, 164, 150, 230, 'ok'),
    ('yard->arena', 156, 164, 55, 228, 'ok'),
    ('yard->start', 156, 164, 3, 0, 'PARTIAL'),
    ('cell1->arena', 11, 216, 55, 228, 'ok'),
    ('cell2->arena', 11, 234.5, 55, 228, 'ok'),
    ('cell3->arena', 60, 275.5, 55, 228, 'ok'),
    ('arena->cell1', 55, 228, 11, 216, 'PARTIAL'),
    ('arena->cell3', 55, 228, 60, 275.5, 'PARTIAL'),
    ('arena->corr1', 55, 228, 29, 216, 'ok'),
    ('arena->corr3', 55, 228, 60, 257.5, 'ok'),
    ('warehouse->prisoners', 150, 230, 134, 236.5, 'ok'),
]


def scenario():
    yield 1.0
    god()
    bad = 0
    for name, ax, ay, bx, by, exp in CHECKS:
        r = path(ax, ay, bx, by)
        good = r.startswith(exp) or (exp == 'PARTIAL' and (r.startswith('noproj') or r in ('nopath', 'invalid')))
        if not good:
            bad += 1
        log('NAV %-20s %-8s %s %s' % (name, exp, 'OK ' if good else 'BAD', r))
    # Полосы без навмеша между зонами: точек там быть не должно.
    for name, x, y in [('strip_S_ruins', 150, 80), ('strip_ruins_yard', 155, 145), ('cellGap1', 20, 216), ('cellGap2', 20, 234.5), ('cellGap3', 60, 266)]:
        p = proj(x, y, 60.0)
        log('NAVPT %-16s %s' % (name, 'none OK' if p is None else 'HAS %s BAD' % m2(p)))
        if p is not None:
            bad += 1
    log('NAV bad=%d' % bad)
    kinds = {}
    for a, g, k, sp in bots:
        kinds[k] = kinds.get(k, 0) + 1
    log('kinds %s' % kinds)
    # Игрок стоит на старте и не стреляет: 60 с наблюдения.
    allg = [g for g, _, _, _, _ in REGIONS]
    report(allg, 'idle')
    for i in range(NIDLE):
        yield 20.0
        report(allg, 'idle')
        log('  ' + squad_line('outpost'))
        log('  ' + squad_line('raiders'))
        log('  ' + squad_line('reserve'))
    log('escaped ruins=%d pen=%d shed=%d cells=%d' % (
        escaped('ruins', (130, 88, 181, 136)), escaped('pen', (152, 49, 164, 63)),
        escaped('shed', (111, 14, 121, 23)), escaped('cells', (8, 212, 64, 278))))


NIDLE = globals().get('NIDLE', 3)
run(scenario)
