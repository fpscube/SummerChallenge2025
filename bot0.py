import sys
import math
from collections import deque

# Constants
MAX_THROW_RANGE = 4
SPLASH_RADIUS = 1
MAX_WETNESS = 100

def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])

def neighbors(pos, width, height):
    x, y = pos
    for dx, dy in [(1,0), (-1,0), (0,1), (0,-1)]:
        nx, ny = x+dx, y+dy
        if 0 <= nx < width and 0 <= ny < height:
            yield (nx, ny)

# Map des distances vers les ennemis
def build_enemy_distance_map(enemy_agents):
    dist_map = [[9999 for _ in range(width)] for _ in range(height)]
    queue = deque()

    for en in enemy_agents:
        ex, ey = en['x'], en['y']
        dist_map[ey][ex] = 0
        queue.append((ex, ey, 0))

    while queue:
        x, y, d = queue.popleft()
        for nx, ny in neighbors((x, y), width, height):
            if tiles[ny][nx] != 0:
                continue
            if dist_map[ny][nx] > d + 1:
                dist_map[ny][nx] = d + 1
                queue.append((nx, ny, d + 1))

    return dist_map

# Lire l'ID du joueur
my_id = int(input())

# Agents info au début
agent_data_count = int(input())
agents_data = {}
for _ in range(agent_data_count):
    agent_id, player, shoot_cooldown, optimal_range, soaking_power, splash_bombs = map(int, input().split())
    agents_data[agent_id] = {
        'player': player,
        'shoot_cooldown': shoot_cooldown,
        'optimal_range': optimal_range,
        'soaking_power': soaking_power,
        'splash_bombs': splash_bombs,
    }

# Map
width, height = map(int, input().split())
tiles = []
for _ in range(height):
    row = list(map(int, input().split()))
    row_tiles = [row[i*3+2] for i in range(width)]
    tiles.append(row_tiles)

def compute_controlled_area(my_agents, enemy_agents):
    controlled_by_me = set()
    controlled_by_enemy = set()

    for y in range(height):
        for x in range(width):
            def dist_min(agents):
                dists = []
                for ag in agents:
                    eff_dist = manhattan((x,y), (ag['x'], ag['y']))
                    if ag['wetness'] >= 50:
                        eff_dist *= 2
                    dists.append(eff_dist)
                return min(dists) if dists else 9999
            d_my = dist_min(my_agents)
            d_en = dist_min(enemy_agents)
            if d_my < d_en:
                controlled_by_me.add((x,y))
            elif d_en < d_my:
                controlled_by_enemy.add((x,y))
    return controlled_by_me, controlled_by_enemy

def valid_moves(pos):
    x,y = pos
    moves = []
    for nx, ny in neighbors(pos, width, height):
        if tiles[ny][nx] == 0:
            moves.append((nx, ny))
    return moves

def best_move(agent, my_agents, enemy_agents, enemy_dist_map):
    current_pos = (agent['x'], agent['y'])
    best_pos = current_pos
    best_score = -1

    controlled_now, _ = compute_controlled_area(my_agents, enemy_agents)
    current_score = len(controlled_now)

    moved = False
    for pos in valid_moves(current_pos):
        sim_my_agents = [dict(a) for a in my_agents]
        for a in sim_my_agents:
            if a['id'] == agent['id']:
                a['x'], a['y'] = pos
        controlled_after, _ = compute_controlled_area(sim_my_agents, enemy_agents)
        score_after = len(controlled_after)
        if score_after > best_score:
            best_score = score_after
            best_pos = pos
            moved = True

    if moved:
        return best_pos
    else:
        # Agent bloqué → se rapprocher d'un ennemi
        min_dist = enemy_dist_map[current_pos[1]][current_pos[0]]
        best_dir = current_pos
        for pos in valid_moves(current_pos):
            px, py = pos
            if enemy_dist_map[py][px] < min_dist:
                min_dist = enemy_dist_map[py][px]
                best_dir = pos
        return best_dir

def find_shoot_target(agent, enemy_agents):
    for en in enemy_agents:
        dist = manhattan((agent['x'], agent['y']), (en['x'], en['y']))
        if dist <= agent['optimal_range']:
            if agent['cooldown'] == 0:
                return en['id']
    for en in enemy_agents:
        dist = manhattan((agent['x'], agent['y']), (en['x'], en['y']))
        if dist <= agent['optimal_range']*2:
            if agent['cooldown'] == 0:
                return en['id']
    return None

def find_splash_zone(agent, launcher_pos, enemy_agents, my_agents):
    ax, ay = launcher_pos
    for en in enemy_agents:
        tx, ty = en['x'], en['y']
        dist = manhattan((ax, ay), (tx, ty))
        if dist <= MAX_THROW_RANGE and agent['splash_bombs'] > 0:
            splash_zone = [
                (tx + dx, ty + dy)
                for dx in [-1, 0, 1]
                for dy in [-1, 0, 1]
                if 0 <= tx + dx < width and 0 <= ty + dy < height
            ]
            if (ax, ay) in splash_zone:
                continue
            if any((ally['x'], ally['y']) in splash_zone for ally in my_agents):
                continue
            return (tx, ty)
    return None

# Game loop
while True:
    agent_count = int(input())
    my_agents = []
    enemy_agents = []

    for _ in range(agent_count):
        agent_id, x, y, cooldown, splash_bombs, wetness = map(int, input().split())
        data = agents_data[agent_id]
        agent_info = {
            'id': agent_id,
            'player': data['player'],
            'x': x,
            'y': y,
            'cooldown': cooldown,
            'optimal_range': data['optimal_range'],
            'soaking_power': data['soaking_power'],
            'splash_bombs': splash_bombs,
            'wetness': wetness
        }
        if data['player'] == my_id:
            my_agents.append(agent_info)
        else:
            enemy_agents.append(agent_info)

    my_agent_count = int(input())

    enemy_dist_map = build_enemy_distance_map(enemy_agents)

    for agent in my_agents:
        move_pos = best_move(agent, my_agents, enemy_agents, enemy_dist_map)
        actions = []

        if move_pos != (agent['x'], agent['y']):
            actions.append(f"MOVE {move_pos[0]} {move_pos[1]}")

        splash_target = find_splash_zone(agent, move_pos, enemy_agents, my_agents)
        if splash_target:
            actions.append(f"THROW {splash_target[0]} {splash_target[1]}")
        else:
            shoot_target = find_shoot_target(agent, enemy_agents)
            if shoot_target is not None:
                actions.append(f"SHOOT {shoot_target}")
            else:
                actions.append("HUNKER_DOWN")

        print(f"{agent['id']};{';'.join(actions)}")
