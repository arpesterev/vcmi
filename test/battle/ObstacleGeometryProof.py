"""Geometry-only diagnosis; never a combat outcome oracle or fixture driver.
Neighbour offsets copied from lib/battle/BattleHex.h:147-170.
Native obstacle cells are recorded by the Q9 actual server log.
"""
from collections import deque
BLOCKED={113,114,115,158,159,143,142,141}
def neighbours(h):
    x,y=h%17,h//17
    for a,b in ((x-1 if y%2 else x,y-1),(x if y%2 else x+1,y-1),(x+1,y),(x if y%2 else x+1,y+1),(x-1 if y%2 else x,y+1),(x-1,y)):
        if 1<=a<=15 and 0<=b<11: yield b*17+a
def contact_distance(start,enemy,blocked):
    queue=deque([start]); distance={start:0}; targets=set(neighbours(enemy))-blocked
    while queue:
        current=queue.popleft()
        if current in targets:return distance[current],current
        for nxt in neighbours(current):
            if nxt!=enemy and nxt not in blocked and nxt not in distance:
                distance[nxt]=distance[current]+1;queue.append(nxt)
    raise AssertionError('no contact route')
for start in (112,111):
    print(dict(attacker_start=start,defender_start=116,
        constrained=contact_distance(start,116,BLOCKED),unblocked=contact_distance(start,116,set())))

print(dict(attacker_start=116,defender_start=111,constrained=contact_distance(116,111,BLOCKED),unblocked=contact_distance(116,111,set())))
