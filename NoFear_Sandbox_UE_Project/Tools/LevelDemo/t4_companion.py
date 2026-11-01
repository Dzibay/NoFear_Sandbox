# -*- coding: utf-8 -*-
# T4: игрок входит в дом, вербует напарника (E) и идёт на север к складу. Ждём: Макс идёт следом.
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_lib.py'), encoding='utf-8').read())

G = ['yard']


def comp_line(c):
    ctl = c.get_controller()
    act = '?'
    try:
        act = str(ctl.get_activity()).split('.')[-1].split(':')[0]
        act += ' rec=%s fol=%s' % (ctl.is_recruited(), ctl.is_following())
    except Exception as e:
        act = 'ctl err %s' % e
    l = c.get_actor_location()
    return 'Макс%s dist=%.1fm %s player%s' % (m2(l), dist_m(l, pawn.get_actor_location()), act, m2(pawn.get_actor_location()))


def scenario():
    yield 1.0
    god()
    c = find_bot('Companion')
    log('companion %s' % c)
    put_player(156, 162.3, 90)
    yield 1.5
    rc = c.get_component_by_class(unreal.NoFearRecruitableComponent)
    log('recruit comp %s can=%s range=%s prompt=%s' % (rc, rc.can_interact(pawn), rc.is_in_range(pawn), rc.get_interaction_prompt(pawn)))
    ok = rc.interact(pawn)
    log('interact -> %s' % ok)
    yield 1.0
    log('  ' + comp_line(c))
    route.extend([(156, 158.5), (167.5, 158), (167.5, 169.5), (155.5, 169.5), (155.5, 176),
                  (155.5, 190), (155, 205), (154, 211)])
    t_end = None
    max_d = 0.0
    for i in range(40):
        yield 2.0
        god()
        d = dist_m(c.get_actor_location(), pawn.get_actor_location())
        max_d = max(max_d, d)
        log('  t=%.0f left=%d %s' % (now() - T0, len(route), comp_line(c)))
        if not route:
            if t_end is None:
                t_end = now()
            elif now() - t_end > 10.0:
                break
    log('RESULT companion final=%s max_dist=%.1fm' % (comp_line(c), max_d))


run(scenario)
