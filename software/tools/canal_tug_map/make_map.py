"""
Make Canal Tug's countryside map.

Lays out winding canals, a river and natural lakes with turning basins,
shallows and buoys; meadows, fields, hedgerows and woods; villages and
lanes; and the sites where cargo is picked up and delivered. Writes:

    apps/games/canal_tug/src/tug_city.c   the map (run-length encoded), sites,
                                          moored narrowboats, preview route
    apps/games/canal_tug/inc/tug_sites.h  map size, counts and site numbers

Run it from the software folder:   python tools/canal_tug_map/make_map.py

Change the canals, sites or villages in the tables below and run it again.
The map is random but repeatable: the same seed gives the same map.
"""
import heapq
import math
import os
import random
from collections import deque

SEED = 11
W, H = 480, 320          # tiles
T = 20                   # pixels per tile

HERE = os.path.dirname(os.path.abspath(__file__))
SOFTWARE = os.path.normpath(os.path.join(HERE, "..", ".."))
CITY_C = os.path.join(SOFTWARE, "apps", "games", "canal_tug", "src", "tug_city.c")
SITES_H = os.path.join(SOFTWARE, "apps", "games", "canal_tug", "inc", "tug_sites.h")

# ---- Waterways: control points (tiles) and half-width (tiles) ------------------------------------------
CANALS = {
    "main":   ([(20, 296), (76, 276), (120, 244), (140, 196), (184, 164), (248, 168), (300, 140), (340, 100), (392, 72), (452, 44)], 5.0),
    "north":  ([(140, 196), (104, 148), (80, 100), (100, 52), (156, 28), (212, 36)], 4.6),
    "river":  ([(248, 168), (272, 208), (316, 240), (380, 256), (452, 282)], 6.0),
    "link":   ([(340, 100), (372, 140), (396, 188), (380, 256)], 4.6),
    "south":  ([(120, 244), (150, 286), (210, 302), (280, 298), (330, 278), (380, 256)], 4.6),
    "west":   ([(80, 100), (44, 132), (30, 190), (48, 248), (76, 276)], 4.6),
    "farnorth": ([(212, 36), (270, 22), (330, 40), (392, 72)], 4.6),
    "east":   ([(452, 282), (456, 210), (440, 130), (452, 44)], 4.6),
}

# Lakes: centre, radii (tiles), and which waterway point they join.
LAKES = [
    ((236, 92), (36, 22), (212, 36)),
    ((206, 236), (26, 18), (210, 302)),
    ((400, 200), (22, 16), (396, 188)),
]

# A turning basin every this many tiles along each waterway, this much wider than it.
BASIN_SPACING = (55, 85)
BASIN_EXTRA = 4.5

# ---- Sites: name, look, waterway, place along it (0 to 1), bank (+1 or -1) --------------------------------
SITES = [
    ("BOATYARD", "BOATYARD", "main", 0.02, 1),
    ("ASHWOOD LUMBER", "LUMBER", "main", 0.10, -1),
    ("GREYSTONE QUARRY", "QUARRY", "main", 0.18, 1),
    ("MIDDLETON", "TOWN", "main", 0.27, -1),
    ("FORGEMOOR STEEL", "STEEL", "main", 0.36, 1),
    ("MOTORWORKS NORTH", "CAR_FACTORY", "main", 0.45, -1),
    ("HOPSWELL BREWERY", "BREWERY", "main", 0.55, 1),
    ("WESTGATE CARS", "DEALER", "main", 0.64, -1),
    ("LAKESIDE MARINA", "MARINA", "main", 0.72, 1),
    ("SPACE CENTRE", "SPACE", "main", 0.82, -1),
    ("LAUNCH SITE", "SPACE", "main", 0.95, 1),
    ("MEADOW FARM", "FARM", "north", 0.12, 1),
    ("PCB FACTORY", "PCB", "north", 0.28, -1),
    ("USOLDER", "USOLDER", "north", 0.45, 1),
    ("CANAL MUSEUM", "MUSEUM", "north", 0.62, -1),
    ("CASTLE", "CASTLE", "north", 0.80, 1),
    ("YACHT BUILDERS", "BOATYARD", "north", 0.94, -1),
    ("DEEPDALE COLLIERY", "COLLIERY", "river", 0.10, -1),
    ("NORTH POWER STATION", "POWER", "river", 0.30, 1),
    ("CHEMICAL WORKS", "CHEMICAL", "river", 0.50, -1),
    ("DEEP STORE", "NUCLEAR", "river", 0.70, 1),
    ("RIVERSIDE MARINA", "MARINA", "river", 0.90, -1),
    ("ROCKET WORKS", "ROCKET", "link", 0.25, 1),
    ("HILLTOP WIND FARM", "WINDFARM", "link", 0.55, -1),
    ("NUCLEAR STORE", "NUCLEAR", "link", 0.85, 1),
    ("WILLOW FARM", "FARM", "south", 0.15, -1),
    ("OAKLEY LUMBER", "LUMBER", "south", 0.35, 1),
    ("BROOKFORD", "TOWN", "south", 0.55, -1),
    ("BLACKHEATH STEEL", "STEEL", "south", 0.75, 1),
    ("EASTGATE CARS", "DEALER", "south", 0.92, -1),
    ("FLINT HILL QUARRY", "QUARRY", "west", 0.20, 1),
    ("COLDHAM COLLIERY", "COLLIERY", "west", 0.40, -1),
    ("SOUTH POWER STATION", "POWER", "west", 0.60, 1),
    ("SCIENCE MUSEUM", "MUSEUM", "west", 0.80, -1),
    ("PCB WORKS", "PCB", "farnorth", 0.20, 1),
    ("USOLDER WORKSHOP", "USOLDER", "farnorth", 0.45, -1),
    ("PALACE", "CASTLE", "farnorth", 0.70, 1),
    ("BARLEYCOMBE BREWERY", "BREWERY", "farnorth", 0.90, -1),
    ("MOTORWORKS SOUTH", "CAR_FACTORY", "east", 0.20, -1),
    ("REFINERY", "CHEMICAL", "east", 0.45, 1),
    ("ENGINE TEST SITE", "ROCKET", "east", 0.70, -1),
    ("MOORSIDE WIND FARM", "WINDFARM", "east", 0.90, 1),
]

# Waterside towns: waterway, middle (0 to 1), bank (+1 or -1), length along the bank (0 to 1).
TOWNS = [
    ("main", 0.32, 1, 0.07), ("main", 0.60, -1, 0.06), ("main", 0.88, 1, 0.06), ("north", 0.54, -1, 0.12),
    ("river", 0.40, -1, 0.14), ("river", 0.80, 1, 0.12), ("south", 0.45, 1, 0.12), ("west", 0.30, -1, 0.14),
    ("farnorth", 0.58, 1, 0.16), ("east", 0.55, -1, 0.14), ("link", 0.40, -1, 0.16),
]

# Lanes across the country between towns, by their place in TOWNS.
LANES = [(0, 3), (1, 6), (4, 9), (2, 8), (5, 9), (7, 0), (10, 4)]

MOORED_COUNT = 16

# ---- Map tiles ------------------------------------------------------------------------------------------------
# Water is written by its depth: '0' beside the bank up to '3' well out in the middle.
DEEP = "0123"
WATERY = "0123so"
random.seed(SEED)
g = [["G"] * W for _ in range(H)]


def inside(x, y):
    return 0 <= x < W and 0 <= y < H


def is_water(x, y):
    return inside(x, y) and g[y][x] in "~so"


def catmull(points, steps=16):
    out = []
    pts = [points[0]] + points + [points[-1]]
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for s in range(steps):
            t = s / steps
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                                    + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in (0, 1)))
    out.append(points[-1])
    return out


def stamp(cx, cy, r, c, only=None):
    for y in range(int(cy - r - 1), int(cy + r + 2)):
        for x in range(int(cx - r - 1), int(cx + r + 2)):
            if inside(x, y) and (x - cx) ** 2 + (y - cy) ** 2 <= r * r and (only is None or g[y][x] in only):
                g[y][x] = c


# Fields: a jittered grid of patches, each meadow, ploughed, wheat or wood, with hedgerows between.
CELL_W, CELL_H = 26, 22
cells = {}
for gy in range(-1, H // CELL_H + 2):
    for gx in range(-1, W // CELL_W + 2):
        cells[(gx, gy)] = (gx * CELL_W + random.randint(-7, 7), gy * CELL_H + random.randint(-6, 6), random.choice("GGFFWWTGFT"))
for y in range(H):
    for x in range(W):
        gx, gy = x // CELL_W, y // CELL_H
        best = second = None
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                cx, cy, crop = cells[(gx + dx, gy + dy)]
                d = math.hypot(x - cx, (y - cy) * 1.2)
                if best is None or d < best[0]:
                    second, best = best, (d, crop)
                elif second is None or d < second[0]:
                    second = (d, crop)
        g[y][x] = "h" if second[0] - best[0] < 1.1 else best[1]

# Waterways and their turning basins.
paths = {name: catmull(pts) for name, (pts, _) in CANALS.items()}
for name, (pts, r) in CANALS.items():
    path = paths[name]
    run = 0.0
    spacing = random.uniform(*BASIN_SPACING)
    for i, (x, y) in enumerate(path):
        stamp(x, y, r, "~")
        if i > 0:
            run += math.hypot(x - path[i - 1][0], y - path[i - 1][1])
        if (run >= spacing) and (0 < i < len(path) - 1):
            # A winding hole: a wide bay let into one bank, big enough to turn a tow round in.
            (x0, y0), (x1, y1) = path[i - 1], path[i + 1]
            n = math.hypot(x1 - x0, y1 - y0) or 1
            side = random.choice((-1, 1))
            nx, ny = -(y1 - y0) / n * side, (x1 - x0) / n * side
            size = r + BASIN_EXTRA + random.uniform(-0.5, 0.8)
            stamp(x + nx * r * 0.45, y + ny * r * 0.45, size, "~")
            stamp(x + nx * r * 0.45 + (x1 - x0) / n * size * 0.4, y + ny * r * 0.45 + (y1 - y0) / n * size * 0.4, size * 0.75, "~")
            run = 0.0
            spacing = random.uniform(*BASIN_SPACING)


# Natural lakes: a wobbly outline, joined to the waterways by a short cut.
def lake_radius(theta, rx, ry, phase):
    wobble = 1.0 + 0.16 * math.sin(3 * theta + phase[0]) + 0.09 * math.sin(5 * theta + phase[1]) + 0.05 * math.sin(8 * theta + phase[2])
    return wobble, rx, ry


lake_tiles = set()
for (cx, cy), (rx, ry), joint in LAKES:
    phase = [random.uniform(0, 6.28) for _ in range(3)]
    for y in range(int(cy - ry * 1.4), int(cy + ry * 1.4)):
        for x in range(int(cx - rx * 1.4), int(cx + rx * 1.4)):
            if not inside(x, y):
                continue
            theta = math.atan2((y - cy) / ry, (x - cx) / rx)
            wobble, _, _ = lake_radius(theta, rx, ry, phase)
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= wobble * wobble:
                g[y][x] = "~"
                lake_tiles.add((x, y))
    for (x, y) in catmull([joint, ((joint[0] + cx) / 2, (joint[1] + cy) / 2), (cx, cy)], 20):
        stamp(x, y, 4.6, "~")


# Distance from the bank for every water tile.
def bank_distance():
    dist = [[0] * W for _ in range(H)]
    queue = deque()
    for y in range(H):
        for x in range(W):
            if g[y][x] in "~so":
                if any(inside(x + dx, y + dy) and g[y + dy][x + dx] not in "~so" for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                    dist[y][x] = 1
                    queue.append((x, y))
                else:
                    dist[y][x] = 99
    while queue:
        x, y = queue.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if inside(nx, ny) and dist[ny][nx] > dist[y][x] + 1:
                dist[ny][nx] = dist[y][x] + 1
                queue.append((nx, ny))
    return dist


# Shallows: round the lake shores, and here and there on the inside of the river's bends.
dist = bank_distance()
for (x, y) in lake_tiles:
    if g[y][x] == "~" and dist[y][x] <= 3:
        g[y][x] = "s"
river = paths["river"]
for i in range(12, len(river) - 8, 11):
    (x0, y0), (x1, y1) = river[i - 1], river[i + 1]
    n = math.hypot(x1 - x0, y1 - y0) or 1
    side = 1 if (i // 11) % 2 else -1
    nx, ny = -(y1 - y0) / n * side, (x1 - x0) / n * side
    stamp(river[i][0] + nx * 4.6, river[i][1] + ny * 4.6, 2.0, "s", "~")

# Buoys along the edge of the shallows where they meet deep water.
count = 0
for y in range(H):
    for x in range(W):
        if g[y][x] == "s" and any(inside(x + dx, y + dy) and g[y + dy][x + dx] == "~" for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            count += 1
            if count % 9 == 0:
                g[y][x] = "o"

# Towpaths along every bank.
for y in range(H):
    for x in range(W):
        if g[y][x] not in "~so" and any(is_water(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            g[y][x] = "p"


# Towns strung along the bank: a lane behind the waterfront, houses either side of it, a few side lanes.
def land_free(x, y):
    return inside(x, y) and g[y][x] in "GFWTh"


def bank_point(path, i, side, out):
    """A point Out tiles from the middle of the waterway, at a (possibly fractional) place I along its path."""
    k = min(int(i), len(path) - 2)
    f = i - k
    px = path[k][0] + (path[k + 1][0] - path[k][0]) * f
    py = path[k][1] + (path[k + 1][1] - path[k][1]) * f
    (x0, y0), (x1, y1) = path[max(0, k - 1)], path[min(len(path) - 1, k + 2)]
    n = math.hypot(x1 - x0, y1 - y0) or 1
    nx, ny = -(y1 - y0) / n * side, (x1 - x0) / n * side
    return px + nx * out, py + ny * out, nx, ny


town_centres = []
for (canal, middle, side, length) in TOWNS:
    path = paths[canal]
    r = CANALS[canal][1]
    first = max(1, int((middle - length / 2) * (len(path) - 1)))
    last = min(len(path) - 2, int((middle + length / 2) * (len(path) - 1)))
    depth = random.uniform(5.0, 8.0)
    for q in range(first * 4, last * 4 + 1):
        i = q / 4.0
        depth = max(4.0, min(9.0, depth + random.uniform(-0.3, 0.3)))
        for step in range(0, 24):
            out = r + 1.6 + step * 0.5
            x, y, nx, ny = bank_point(path, i, side, out)
            xi, yi = int(round(x)), int(round(y))
            if not land_free(xi, yi) and not (inside(xi, yi) and g[yi][xi] == "H"):
                continue
            if abs(out - (r + 3.0)) < 0.3:
                g[yi][xi] = "r"
            elif out < r + 3.0 + depth:
                g[yi][xi] = "H"
        if (q - first * 4) % 36 == 16:
            for step in range(0, 18):
                x, y, nx, ny = bank_point(path, i, side, r + 3.0 + step * 0.5)
                xi, yi = int(round(x)), int(round(y))
                if inside(xi, yi) and g[yi][xi] in "GFWThH":
                    g[yi][xi] = "r"
    x, y, _, _ = bank_point(path, (first + last) // 2, side, r + 3.0)
    town_centres.append((x, y))

# Country lanes between towns; they stop at the water.
for (a, b) in LANES:
    (ax, ay), (bx, by) = town_centres[a], town_centres[b]
    bend = (random.uniform(-0.2, 0.2) * (by - ay), random.uniform(-0.2, 0.2) * (ax - bx))
    for (x, y) in catmull([(ax, ay), ((ax + bx) / 2 + bend[0], (ay + by) / 2 + bend[1]), (bx, by)], 80):
        xi, yi = int(round(x)), int(round(y))
        if inside(xi, yi) and g[yi][xi] in "GFWThH":
            g[yi][xi] = "r"

# ---- Sites -------------------------------------------------------------------------------------------------------
def deep_clear(x, y, r):
    return all(inside(x + dx, y + dy) and g[y + dy][x + dx] == "~" for dx in range(-r, r + 1) for dy in range(-r, r + 1))


placed = []
for (name, look, canal, t, side) in SITES:
    pts = paths[canal]
    i = max(1, min(len(pts) - 2, int(t * (len(pts) - 1))))
    (x0, y0), (x1, y1) = pts[i - 1], pts[i + 1]
    n = math.hypot(x1 - x0, y1 - y0) or 1
    tx, ty = (x1 - x0) / n, (y1 - y0) / n
    nx, ny = -ty * side, tx * side
    r = CANALS[canal][1]
    cx, cy = pts[i]

    # Set the buildings back from the bank until their ground is dry, trying the other bank if need be.
    chosen = None
    for flip in (1, -1):
        for back in (9.5, 10.5, 11.5, 12.5, 13.5):
            sx, sy = cx + nx * flip * (r + back), cy + ny * flip * (r + back)
            if inside(int(sx), int(sy)) and not any(is_water(int(sx) + dx, int(sy) + dy) for dx in range(-5, 6) for dy in range(-5, 6)):
                chosen = (sx, sy, flip)
                break
        if chosen:
            break
    assert chosen, name
    sx, sy, flip = chosen
    nx, ny = nx * flip, ny * flip
    for y in range(int(sy) - 6, int(sy) + 7):
        for x in range(int(sx) - 7, int(sx) + 8):
            if inside(x, y) and g[y][x] in "GFWTHhr" and ((x - sx) / 7.5) ** 2 + ((y - sy) / 6.5) ** 2 <= 1:
                g[y][x] = "Q"

    # The dock: in the water near the site's bank, so cargo lies along the bank and the channel stays clear.
    best = None
    want = (cx + nx * (r - 2.6), cy + ny * (r - 2.6))
    for dy in range(-6, 7):
        for dx in range(-6, 7):
            x, y = int(round(want[0])) + dx, int(round(want[1])) + dy
            if deep_clear(x, y, 1):
                d = (x - want[0]) ** 2 + (y - want[1]) ** 2
                if best is None or d < best[0]:
                    best = (d, x, y)
    assert best, name
    heading = math.atan2(tx, -ty)
    placed.append((name, best[1], best[2], int(round(heading * 1000)), int(round(sx * T + T / 2)), int(round(sy * T + T / 2)), look))

# ---- Narrowboats moored along the banks, away from the docks ------------------------------------------------------------
moored = []
candidates = [(c, k / 48.0 + 0.01, 1 if (k % 2) else -1) for k in range(1, 48) for c in CANALS]
random.Random(SEED + 1).shuffle(candidates)
for canal, t, side in candidates:
    if len(moored) >= MOORED_COUNT:
        break
    pts = paths[canal]
    i = max(1, min(len(pts) - 2, int(t * (len(pts) - 1))))
    (x0, y0), (x1, y1) = pts[i - 1], pts[i + 1]
    n = math.hypot(x1 - x0, y1 - y0) or 1
    nx, ny = -(y1 - y0) / n * side, (x1 - x0) / n * side
    r = CANALS[canal][1]
    # Only on a straight stretch, so a moored boat never pinches a bend.
    if i < 3 or i > len(pts) - 4:
        continue
    (ax, ay), (cx2, cy2) = pts[i - 3], pts[i + 3]
    turn = abs(math.atan2(y1 - y0, x1 - x0) - math.atan2(cy2 - ay, cx2 - ax))
    if min(turn, 2 * math.pi - turn) > 0.12:
        continue
    # Tight against the bank.
    bx, by = pts[i][0] + nx * (r - 0.8), pts[i][1] + ny * (r - 0.8)
    ox, oy = int(round(bx + nx * 1.4)), int(round(by + ny * 1.4))
    if not inside(ox, oy) or is_water(ox, oy):
        continue
    if any(math.hypot((bx - dx_) * T, (by - dy_) * T) < 220 for (_, dx_, dy_, _, _, _, _) in placed):
        continue
    if any(math.hypot(bx * T + T / 2 - mx, by * T + T / 2 - my) < 200 for (mx, my, _, _) in moored):
        continue
    moored.append((int(round(bx * T + T / 2)), int(round(by * T + T / 2)), int(round(math.atan2(x1 - x0, -(y1 - y0)) * 1000)), len(moored) % 4))
assert len(moored) == MOORED_COUNT, len(moored)

# ---- Depth: write each water tile as its distance from the bank -------------------------------------------------------------
dist = bank_distance()
for y in range(H):
    for x in range(W):
        if g[y][x] == "~":
            g[y][x] = DEEP[min(dist[y][x] - 1, 3)]

# The edges of the map are dry land.
for x in range(W):
    for y in (0, H - 1):
        if g[y][x] in WATERY:
            g[y][x] = "G"
for y in range(H):
    for x in (0, W - 1):
        if g[y][x] in WATERY:
            g[y][x] = "G"

# ---- Check: every dock reachable from the boatyard through open water at least 5 tiles wide -------------------------------------
def open5(x, y):
    return all(inside(x + dx, y + dy) and g[y + dy][x + dx] in DEEP for dx in range(-2, 3) for dy in range(-2, 3))


start = (placed[0][1], placed[0][2])
seen = set()
for sx in range(start[0] - 3, start[0] + 4):
    for sy in range(start[1] - 3, start[1] + 4):
        if open5(sx, sy):
            seen.add((sx, sy))
todo = list(seen)
while todo:
    x, y = todo.pop()
    for nx_, ny_ in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
        if (nx_, ny_) not in seen and open5(nx_, ny_):
            seen.add((nx_, ny_))
            todo.append((nx_, ny_))
for (name, x, y, *_rest) in placed:
    assert any((x + dx, y + dy) in seen for dx in range(-4, 5) for dy in range(-4, 5)), ("unreachable", name)

# ---- Route lengths: the shortest way by open water between every pair of sites, in tiles ---------------------------------------
def sail_from(x0, y0):
    """Distance by open water (diagonals allowed where both sides are open) from a dock to every open tile."""
    best = {}
    heap = []
    for dx in range(-4, 5):
        for dy in range(-4, 5):
            if (x0 + dx, y0 + dy) in seen:
                d = math.hypot(dx, dy)
                best[(x0 + dx, y0 + dy)] = d
                heapq.heappush(heap, (d, x0 + dx, y0 + dy))
    while heap:
        d, x, y = heapq.heappop(heap)
        if d > best[(x, y)]:
            continue
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if (dx or dy) and (x + dx, y + dy) in seen and (x + dx, y) in seen and (x, y + dy) in seen:
                    n = d + (1.4142 if (dx and dy) else 1.0)
                    if n < best.get((x + dx, y + dy), 1e9):
                        best[(x + dx, y + dy)] = n
                        heapq.heappush(heap, (n, x + dx, y + dy))
    return best


routes = []
for (name, x, y, *_rest) in placed:
    far = sail_from(x, y)
    row = []
    for (other, ox, oy, *_r) in placed:
        row.append(int(round(min(far.get((ox + dx, oy + dy), 1e9) + math.hypot(dx, dy) for dx in range(-4, 5) for dy in range(-4, 5)))))
    routes.append(row)
assert all(r < 65535 for row in routes for r in row)

# ---- Write the map, run-length encoded row by row ---------------------------------------------------------------------------------
# Only what was designed is stored: water, shallows, buoys, lanes, houses and yards. The game works the
# rest out as it goes: water depth from the distance to the bank, towpaths along every bank, and the
# fields, hedgerows and woods from a patchwork of its own.
STORED = {"0": "~", "1": "~", "2": "~", "3": "~", "p": "G", "F": "G", "W": "G", "T": "G", "h": "G"}
g = [[STORED.get(c, c) for c in row] for row in g]
runs = []
starts = []
for y in range(H):
    starts.append(len(runs))
    x = 0
    while x < W:
        c = g[y][x]
        n = 1
        while x + n < W and g[y][x + n] == c and n < 255:
            n += 1
        runs.extend((n, ord(c)))
        x += n
starts.append(len(runs))


def c_bytes(data, per_line=24):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join("%3d" % b for b in data[i:i + per_line]) + ",")
    return "\n".join(lines)


def c_words(data, per_line=12):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join("%6dU" % b for b in data[i:i + per_line]) + ",")
    return "\n".join(lines)


def ident(name):
    return "TUG_AT_" + "".join(ch if ch.isalnum() else "_" for ch in name)


main_path = paths["main"]
route = [(int(round(x * T + T / 2)), int(round(y * T + T / 2))) for (x, y) in main_path[int(len(main_path) * 0.30):int(len(main_path) * 0.42)]]

city = """/**
 * @file tug_city.c
 * @brief The countryside Canal Tug is played in.
 *
 * Made by tools/canal_tug_map/make_map.py: change the map there and run it
 * again rather than editing this file.
 *
 * The map is 480 by 320 tiles of 20 pixels, stored row by row as runs of
 * (count, tile) pairs. Only what was designed is stored:
 *
 *   '~' water   's' shallows   'o' buoy   'r' lane   'H' houses   'Q' a site's
 *   yard   'G' open country
 *
 * The game works out the rest as each row is read: how deep the water is,
 * the towpath along every bank, and the fields, hedgerows and woods.
 */

#include "tug_internal.h"

/* -------------------------------------------------------------------------- */
/* Map                                                                        */
/* -------------------------------------------------------------------------- */

const uint8_t Tug_MapRuns[] =
{
%s
};

/* Where each row's runs begin in Tug_MapRuns, and one past the last row. */
const uint32_t Tug_MapRowStart[TUG_MAP_HEIGHT + 1U] =
{
%s
};

/* -------------------------------------------------------------------------- */
/* Sites                                                                      */
/* -------------------------------------------------------------------------- */

const Tug_DockTypeDef Tug_Docks[TUG_DOCK_COUNT] =
{
    /* Name                    Dock tile    Heading  Buildings (pixels)  Look */
%s
};

/* -------------------------------------------------------------------------- */
/* Moored narrowboats                                                         */
/* -------------------------------------------------------------------------- */

const Tug_MooredBoatTypeDef Tug_MooredBoats[TUG_MOORED_BOAT_COUNT] =
{
    /* X      Y      Heading (thousandths of a radian)  Paint */
%s
};

/* -------------------------------------------------------------------------- */
/* Launcher preview                                                           */
/* -------------------------------------------------------------------------- */

/* A stretch of the main canal for the preview's tug to cruise along, in pixels. */
const int16_t Tug_SplashRoute[][2] =
{
%s
};

const uint8_t Tug_SplashRouteCount = (uint8_t)(sizeof(Tug_SplashRoute) / sizeof(Tug_SplashRoute[0]));

/* -------------------------------------------------------------------------- */
/* Route lengths                                                              */
/* -------------------------------------------------------------------------- */

/* How far it is by water from each site to each other site, in tiles. */
const uint16_t Tug_RouteTiles[TUG_DOCK_COUNT][TUG_DOCK_COUNT] =
{
%s
};
""" % (c_bytes(runs), c_words(starts),
       "\n".join('    { %-24s %3dU, %3dU, %6d, %5d, %5d, TUG_SITE_%s },' % ('"%s",' % n, x, y, hd, sx, sy, lk) for (n, x, y, hd, sx, sy, lk) in placed),
       "\n".join('    { %5d, %5d, %6d, %dU },' % m for m in moored),
       "\n".join("    { %d, %d }," % pt for pt in route),
       "\n".join("    { " + ", ".join("%4dU" % r for r in row) + " }," for row in routes))

sites = """/**
 * @file tug_sites.h
 * @brief The size of Canal Tug's map and the number of each site on it.
 *
 * Made by tools/canal_tug_map/make_map.py: don't edit this file by hand.
 */

#ifndef TUG_SITES_H
#define TUG_SITES_H

#define TUG_MAP_WIDTH           (%d)
#define TUG_MAP_HEIGHT          (%d)
#define TUG_DOCK_COUNT          (%dU)
#define TUG_MOORED_BOAT_COUNT   (%dU)

/* Sites, by their place in Tug_Docks. */
%s

#endif /* TUG_SITES_H */
""" % (W, H, len(placed), len(moored),
       "\n".join("#define %-30s (%dU)" % (ident(n), i) for i, (n, *_r) in enumerate(placed)))

open(CITY_C, "w", newline="\r\n").write(city)
open(SITES_H, "w", newline="\r\n").write(sites)
print("map %dx%d: %d bytes of runs (%.1f per row), %d sites, %d moored boats, %d route points"
      % (W, H, len(runs), len(runs) / H, len(placed), len(moored), len(route)))
