"""Standalone 2-D Gerber preprocessing for gerber_to_glb.py; no CAD dependency."""
from __future__ import annotations
import math
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Sequence
from shapely import affinity, make_valid, set_precision, union_all
from shapely.geometry import GeometryCollection, LinearRing, LineString, MultiPolygon, Point, Polygon, box
from shapely.ops import polygonize, unary_union
from shapely.prepared import prep
EPS = 1.0e-7
# 0.1 micrometre topology grid, suitable for metre-valued float32 meshes.
GRID = 1.0e-4
RENDER_GRID = 1.0e-7


@dataclass(frozen=True)
class Stackup:
    board: float = 1.0
    copper: float = 0.035
    mask: float = 0.020
    finish: float = 0.004
    silk: float = 0.015

    @property
    def top(self) -> float:
        return self.board / 2.0

    @property
    def bottom(self) -> float:
        return -self.board / 2.0

@dataclass
class Artwork:
    board: Polygon | MultiPolygon
    top_cu: Polygon | MultiPolygon
    bottom_cu: Polygon | MultiPolygon
    top_mask_open: Polygon | MultiPolygon
    bottom_mask_open: Polygon | MultiPolygon
    top_silk: Polygon | MultiPolygon
    bottom_silk: Polygon | MultiPolygon

@dataclass(frozen=True)
class Cell:
    polygon: Polygon
    top: float
    bottom: float

def _empty() -> Polygon:
    return Polygon()

def _polygons(g) -> Iterator[Polygon]:
    if g is None or g.is_empty:
        return
    if isinstance(g, Polygon):
        yield g
    elif isinstance(g, (MultiPolygon, GeometryCollection)):
        for item in g.geoms:
            yield from _polygons(item)

def clean(g, clip=None, grid=GRID):
    if g is None or g.is_empty:
        return _empty()
    g = make_valid(g)
    if clip is not None:
        g = g.intersection(clip)
    g = set_precision(g, grid, mode="valid_output")
    return unary_union(list(_polygons(g))) if not g.is_empty else _empty()

def patch_pygerber_full_circle() -> None:
    """Patch PyGerber 3.0.0a4's start==end full-circle arc assertion."""
    from pygerber.vm.shapely.vm import ShapelyVirtualMachine

    if getattr(ShapelyVirtualMachine, "_pcb_pretty_full_circle_patch", False):
        return
    original = ShapelyVirtualMachine._calculate_arc_points

    def fixed(self, command):
        start = command.get_relative_start_point().angle_between(command.get_relative_start_point().__class__.unit.x) % 360
        end = command.get_relative_end_point().angle_between(command.get_relative_end_point().__class__.unit.x) % 360
        if not math.isclose(start, end, abs_tol=1e-12):
            yield from original(self, command)
            return
        radius = command.get_radius()
        circumference = radius * 2.0 * math.pi
        count = max(24, self.angle_length_to_segment_count(circumference))
        direction = -1.0 if command.clockwise else 1.0
        for i in range(count):
            angle = math.radians(start + direction * 360.0 * i / count)
            yield (command.center.x + radius * math.cos(angle), command.center.y + radius * math.sin(angle))

    ShapelyVirtualMachine._calculate_arc_points = fixed
    ShapelyVirtualMachine._pcb_pretty_full_circle_patch = True

def render_gerber(path: Path):
    patch_pygerber_full_circle()
    from pygerber.gerber.api import GerberFile, Style

    image = GerberFile.from_file(path).render_with_shapely(Style.presets.BLACK_WHITE)
    geom = image._result.shape
    units = str(image._image_space.units).lower()
    if "inch" in units or units.endswith("in"):
        geom = affinity.scale(geom, 25.4, 25.4, origin=(0, 0))
    return clean(geom, grid=RENDER_GRID)

LAYER_EXTENSIONS = {
    ".gtl": "top_cu",
    ".gbl": "bottom_cu",
    ".gts": "top_mask_open",
    ".gbs": "bottom_mask_open",
    ".gto": "top_silk",
    ".gbo": "bottom_silk",
    ".gm": "outline",
    ".gko": "outline",
}

def find_layers(root: Path) -> dict[str, Path]:
    import re
    found: dict[str, Path] = {}
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        key = LAYER_EXTENSIONS.get(p.suffix.lower())
        if key is None and p.suffix.lower() in ('.gbr','.ger','.gerber'):
            with p.open(errors='replace') as f: header=f.read(16384)
            copper=re.search(r'TF\.FileFunction,Copper,L\d+,(Top|Bot)',header,re.I)
            coating=re.search(r'TF\.FileFunction,(Soldermask|Legend),(Top|Bot)',header,re.I)
            if copper: key='top_cu' if copper[1].lower()=='top' else 'bottom_cu'
            elif coating:
                key=('top_' if coating[2].lower()=='top' else 'bottom_')+('mask_open' if coating[1].lower()=='soldermask' else 'silk')
            elif re.search(r'TF\.FileFunction,Profile',header,re.I): key='outline'
            if key is None:
                suffixes={'F_Cu':'top_cu','B_Cu':'bottom_cu','F_Mask':'top_mask_open',
                          'B_Mask':'bottom_mask_open','F_Silkscreen':'top_silk','B_Silkscreen':'bottom_silk',
                          'Edge_Cuts':'outline'}
                for label,value in suffixes.items():
                    if p.stem.lower().endswith(label.lower()): key=value; break
        if key and key not in found:
            found[key] = p
    if "outline" not in found:
        raise ValueError("No board profile found (.GM or .GKO)")
    return found

def reconstruct_board(profile):
    """Recover the board face from either a filled profile or routed linework.

    Filled profiles pass through directly.  For the common stroked closed-loop
    export, polygonizing all stroke boundaries produces the enclosed material
    cell; the largest non-stroke cell is selected.  Nested cells inside it are
    treated as routed cutouts when they do not overlap the profile ink.
    """
    profile = clean(profile, grid=RENDER_GRID)
    polys = list(_polygons(profile))
    if not polys:
        raise ValueError("The board profile rendered empty")
    # Mechanical Gerbers frequently mix an enclosing stroked panel contour
    # with positive filled regions describing material to route AWAY. Detect
    # that before considering any filled region to be FR4. Choosing the
    # largest ink polygon instead incorrectly turns routed voids into boards.
    frame = max(polys, key=lambda p: Polygon(p.exterior).area)
    envelope = Polygon(frame.exterior)
    if frame.interiors and frame.area < envelope.area * 0.05:
        inner = max((Polygon(r) for r in frame.interiors), key=lambda p: p.area)
        if all(envelope.covers(p) for p in polys):
            # Restore the centreline of the narrow outline stroke. For a
            # constant-width stroke, area / average perimeter is its width.
            width = 2.0 * frame.area / (envelope.length + inner.length)
            board = inner.buffer(width / 2.0, join_style=2)
            routed = unary_union([p for p in polys if p is not frame])
            # A hollow inner stroke describes a routed contour, so remove its
            # enclosed area too, not only the thin ink ring.
            voids = [Polygon(p.exterior) for p in _polygons(routed)]
            board = clean(board.difference(unary_union(voids)), grid=RENDER_GRID)
            if not isinstance(board, Polygon):
                raise ValueError('Profile describes disconnected boards; the five-solid '
                                 'architecture requires a physically connected panel.')
            print(f'Board profile: 1 material island; {len(board.interiors)} routed cutouts', flush=True)
            return board
    # A genuinely filled region is much less perimeter-heavy than routed ink.
    largest = max(polys, key=lambda p: p.area)
    compactness = 4.0 * math.pi * largest.area / max(largest.length * largest.length, EPS)
    if compactness > 0.12:
        return clean(profile)
    cells = list(polygonize(unary_union([p.boundary for p in polys])))
    candidates = [p for p in cells if p.area > EPS and not profile.covers(p.representative_point())]
    if not candidates:
        # Polygon exteriors often directly contain the enclosed board cell.
        candidates = [p for p in cells if p.area > EPS]
    board = max(candidates, key=lambda p: p.area)
    holes = [p for p in candidates if p is not board and board.contains(p.representative_point())]
    if holes:
        board = board.difference(unary_union(holes))
    return clean(board)

def simplify_artwork(artwork: Artwork, tolerance: float) -> Artwork:
    """Simplify source artwork before deriving any shared height boundaries.

    Keep the mechanical profile exact. This is a visual approximation, not
    manufacturing geometry; small pads and lettering can look distorted.
    """
    if not math.isfinite(tolerance) or tolerance < 0:
        raise ValueError("simplification tolerance must be finite and nonnegative")
    if tolerance == 0:
        return artwork
    from shapely import get_num_coordinates
    result = {"board": artwork.board}
    print(f"Simplifying artwork: {tolerance:g} mm tolerance (board unchanged)", flush=True)
    for name in artwork.__dataclass_fields__:
        if name == "board":
            continue
        original = getattr(artwork, name)
        reduced = clean(original.simplify(tolerance, preserve_topology=True), artwork.board)
        result[name] = reduced
        print(f"  {name}: {get_num_coordinates(original)} -> "
              f"{get_num_coordinates(reduced)} vertices", flush=True)
    return Artwork(**result)

def load_artwork(source: Path, simplify_tolerance: float = 0.0) -> Artwork:
    temp: tempfile.TemporaryDirectory | None = None
    try:
        if source.is_file() and zipfile.is_zipfile(source):
            temp = tempfile.TemporaryDirectory(prefix="pcb_pretty_step_")
            with zipfile.ZipFile(source) as zf:
                zf.extractall(temp.name)
            root = Path(temp.name)
        else:
            root = source
        layers = find_layers(root)
        rendered = {}
        for name, path in layers.items():
            print(f'Rendering {name}: {path.name}', flush=True)
            rendered[name] = render_gerber(path)
        board = reconstruct_board(rendered["outline"])
        if not isinstance(board, Polygon):
            raise ValueError('Board profile is disconnected: cannot build one solid per material')
        get = lambda key: clean(rendered.get(key, _empty()), board, grid=RENDER_GRID)
        artwork = Artwork(board, get("top_cu"), get("bottom_cu"), get("top_mask_open"),
                          get("bottom_mask_open"), get("top_silk"), get("bottom_silk"))
        # Keep source rendering and clipping precise; apply the CAD topology
        # grid only after clipping, rather than repeatedly rounding contours.
        artwork = Artwork(**{name: clean(getattr(artwork, name))
                             for name in artwork.__dataclass_fields__})
        return simplify_artwork(artwork, simplify_tolerance)
    finally:
        if temp is not None:
            temp.cleanup()

def _arrangement(board, regions: Sequence) -> list[Polygon]:
    # Build ONE globally noded network. Do not repeatedly intersect thousands
    # of cells with entire artwork layers, or re-snap individual cell edges.
    lines = [board.boundary]
    lines.extend(g.boundary for g in regions if not g.is_empty)
    # Limit planar-face complexity without changing a single visible contour.
    # OCC's face checks become expensive for one panel-sized face containing
    # thousands of holes. Coplanar grid seams keep those checks local.
    if regions:
        x0, y0, x1, y1 = board.bounds
        pitch = 15.0
        for i in range(math.floor(x0 / pitch) + 1, math.ceil(x1 / pitch)):
            lines.append(LineString([(i * pitch, y0), (i * pitch, y1)]))
        for j in range(math.floor(y0 / pitch) + 1, math.ceil(y1 / pitch)):
            lines.append(LineString([(x0, j * pitch), (x1, j * pitch)]))
    network = union_all(lines, grid_size=GRID)
    board_test = prep(board)
    cells = [poly for poly in polygonize(network)
             if poly.area > 0 and board_test.covers(poly.representative_point())]
    if not cells:
        raise ValueError("Planar arrangement produced no board cells")
    return cells

def classify_cells(board, top_classes: Sequence[tuple], bottom_classes: Sequence[tuple],
                   default_top: float, default_bottom: float) -> list[Cell]:
    regions = [g for g, _ in (*top_classes, *bottom_classes)]
    cells = _arrangement(board, regions)
    top_prepared = [(prep(g), z) for g, z in top_classes if not g.is_empty]
    bottom_prepared = [(prep(g), z) for g, z in bottom_classes if not g.is_empty]
    result = []
    for poly in cells:
        point = poly.representative_point()
        top = default_top
        bottom = default_bottom
        for region, z in top_prepared:
            if region.covers(point):
                top = z
        for region, z in bottom_prepared:
            if region.covers(point):
                bottom = z
        if top <= bottom + EPS:
            raise ValueError(f"Invalid height interval {bottom:g} .. {top:g}")
        result.append(Cell(poly, top, bottom))
    return result

def material_regions(a: Artwork, s: Stackup):
    board = a.board
    tm = clean(board.difference(a.top_mask_open))
    bm = clean(board.difference(a.bottom_mask_open))
    te = clean(a.top_cu.intersection(a.top_mask_open), board)
    be = clean(a.bottom_cu.intersection(a.bottom_mask_open), board)
    # A coating has lateral thickness as well as top thickness. Keeping this
    # boundary on the copper outline puts the two vertical mesh walls on the
    # same plane, exposing copper through z-fighting. Grow only the raised
    # mask footprint, then clip it to the original mask-present area so pads
    # and other intentional mask openings remain clear.
    top_coated = clean(a.top_cu.buffer(s.mask, quad_segs=8), board)
    bottom_coated = clean(a.bottom_cu.buffer(s.mask, quad_segs=8), board)
    tmh = clean(tm.intersection(top_coated), board)
    bmh = clean(bm.intersection(bottom_coated), board)
    tml = clean(tm.difference(tmh), board)
    bml = clean(bm.difference(bmh), board)
    tsv = clean(a.top_silk.intersection(tm), board)
    bsv = clean(a.bottom_silk.intersection(bm), board)
    # Ink coats the raised mask sidewall as well as its upper surface. Extend
    # the high ink region by the ink thickness, but only inside the existing
    # visible artwork; neither lettering nor pad clearances are expanded.
    tsh = clean(tsv.intersection(tmh.buffer(s.silk, quad_segs=8)), board)
    bsh = clean(bsv.intersection(bmh.buffer(s.silk, quad_segs=8)), board)
    tsl = clean(tsv.difference(tsh), board)
    bsl = clean(bsv.difference(bsh), board)
    return dict(tm=tm, bm=bm, te=te, be=be, tmh=tmh, bmh=bmh, tml=tml,
                bml=bml, tsv=tsv, bsv=bsv, tsh=tsh, bsh=bsh, tsl=tsl, bsl=bsl)

def synthetic_artwork() -> Artwork:
    board = box(0, 0, 40, 28).difference(Point(30, 8).buffer(2.4, quad_segs=24))
    pads = [Point(4 + (i % 12) * 2.6, 4 + (i // 12) * 3.3).buffer(0.72, quad_segs=10) for i in range(72)]
    tracks = [box(2, 11.7, 37, 12.3), box(18.7, 2, 19.3, 26)]
    top_cu = clean(unary_union(pads + tracks), board)
    bottom_cu = clean(unary_union([box(3, 20, 36, 20.8), Point(8, 8).buffer(3)]), board)
    top_open = clean(unary_union([p.buffer(0.18) for p in pads[::3]]), board)
    bottom_open = clean(Point(8, 8).buffer(2.3), board)
    # These lines cross bare-mask and copper-raised-mask height classes.
    top_silk = clean(unary_union([box(1, 11.9, 39, 12.12), box(18.9, 1, 19.12, 27)]), board)
    bottom_silk = clean(box(2, 20.15, 38, 20.42), board)
    return Artwork(clean(board), top_cu, bottom_cu, top_open, bottom_open, top_silk, bottom_silk)
