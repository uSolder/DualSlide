"""Excellon round drills and straight routed slots, in millimetres.

Unsupported geometry commands fail explicitly rather than dropping holes.
No via classification by diameter: soldermask artwork controls tenting.
"""
from dataclasses import dataclass
from pathlib import Path
import re
import zipfile
from shapely.geometry import Point, LineString


@dataclass
class Hit:
    shape: object
    plated: bool | None
    tool: int
    diameter: float
    slot: bool


def parse_excellon(text, name='drill', format_override=None):
    units=None
    fmt=format_override
    zero=None
    header=True
    plated=False if re.search(r'NPTH|NON.?PLATED',name,re.I) else (
        True if re.search(r'PTH|PLATED',name,re.I) else None)
    tools={}
    tool=None
    x=y=0.0
    mode='drill'
    route=None
    result=[]

    def fail(message):
        raise ValueError(f'{name}: {message}')

    def number(value):
        if units is None: fail('missing INCH/METRIC unit declaration')
        if '.' in value: return float(value)*units
        if fmt is None: fail('implicit coordinates without format; use --drill-format 2:5 (or correct format)')
        sign=-1 if value.startswith('-') else 1
        digits=value.lstrip('+-')
        if len(digits)>sum(fmt): fail('coordinate exceeds declared format')
        # Excellon LZ means leading zeros retained; omitted zeros trail.
        if zero=='LZ': digits=digits.ljust(sum(fmt),'0')
        elif zero is None and len(digits)!=sum(fmt):
            fail('short coordinate without LZ/TZ declaration')
        return sign*int(digits)*units/(10**fmt[1])

    def position(line):
        xx=re.search(r'X([+-]?[\d.]+)',line)
        yy=re.search(r'Y([+-]?[\d.]+)',line)
        return (number(xx[1]) if xx else x, number(yy[1]) if yy else y)

    def add(points):
        if tool not in tools: fail(f'undefined tool T{tool}')
        diameter,kind=tools[tool]
        path=Point(points[0]) if len(points)==1 or len(set(points))==1 else LineString(points)
        result.append(Hit(path.buffer(diameter/2,quad_segs=12),kind,tool,diameter,
                          len(points)>1))

    for lineno,raw in enumerate(text.splitlines(),1):
        line=raw.strip().upper()
        if not line: continue
        match=re.search(r'FILE_FORMAT\s*=\s*(\d+)\s*:\s*(\d+)',line)
        if match and format_override is None: fmt=(int(match[1]),int(match[2]))
        if line.startswith(';'):
            if re.search(r'(?:TYPE=|FILEFUNCTION,)(?:NON_PLATED|NONPLATED|NPTH)',line): plated=False
            elif re.search(r'(?:TYPE=|FILEFUNCTION,)(?:PLATED|PTH)',line): plated=True
            continue
        line=line.split(';',1)[0].replace(' ','')
        if line=='M48': continue
        if line.startswith(('INCH','METRIC')) or line in ('M71','M72'):
            units=25.4 if line.startswith('INCH') or line=='M72' else 1.0
            z=re.search(r'\b(LZ|TZ)\b',line)
            if z: zero=z[1]
            f=re.search(r'(0+)\.(0+)',line)
            if f and format_override is None: fmt=(len(f[1]),len(f[2]))
            continue
        if line in ('%','M95'): header=False; continue
        t=re.match(r'T(\d+)',line)
        if t:
            selected=int(t[1]); size=re.search(r'C([\d.]+)',line)
            if size:
                if units is None: fail('tool definition before units')
                diameter=float(size[1])*units
                if diameter<=0: fail('nonpositive drill diameter')
                tools[selected]=(diameter,plated)
            else:
                if route is not None: fail('tool change during routing')
                tool=selected
            continue
        if header:
            if line.startswith(('FMAT,','VER,','ATC','DETECT','M47')): continue
            fail(f'unsupported header at line {lineno}: {line}')
        if line in ('G90','ICI,OFF'): continue
        if line in ('G91','ICI,ON'): fail('incremental coordinates are not supported')
        if line in ('M30','M00'): break
        if line in ('M16','M17'):
            if route is not None:
                add(route); route=None
            continue
        if line=='M15':
            if route is not None: fail('nested routing start')
            route=[(x,y)]; continue
        if line in ('G05','G5'):
            mode='drill'; continue
        if 'G85' in line:
            start,end=line.split('G85',1)
            x,y=position(start)
            first=(x,y); x,y=position(end)
            add([first,(x,y)]); continue
        codes=re.findall(r'G(\d+)',line)
        if any(int(code) not in (0,1,5,90) for code in codes):
            fail(f'unsupported motion at line {lineno}: {line}')
        for code in codes:
            if int(code)==0: mode='rapid'
            elif int(code)==1: mode='linear'
            elif int(code)==5: mode='drill'
        if re.search(r'[XY][+-]?[\d.]',line):
            if re.search(r'[IJAR]',line): fail('arc/repeat coordinates are not supported')
            residual=re.sub(r'[XY][+-]?[\d.]+','',line)
            residual=re.sub(r'G\d+','',residual)
            if residual: fail(f'unsupported coordinate command at line {lineno}: {line}')
            x,y=position(line)
            if mode=='drill': add([(x,y)])
            elif mode=='linear':
                if route is None: fail('linear routing without M15')
                route.append((x,y))
            elif route is not None: fail('rapid move while routing tool is down')
            continue
        if line not in ('G00','G0','G01','G1'):
            fail(f'unsupported command at line {lineno}: {line}')
    if route is not None: fail('unterminated routed slot')
    return result


def load_drills(source: Path, format_override=None):
    suffixes={'.txt','.drl','.xln','.exc','.ncd'}
    result=[]
    def read_one(name,data):
        if Path(name).suffix.lower() not in suffixes: return
        text=data.decode('utf-8-sig',errors='replace')
        if not re.search(r'^\s*M48\s*$',text,re.M):
            if Path(name).suffix.lower()!='.txt':
                raise ValueError(f'{name}: expected an Excellon M48 header')
            return
        hits=parse_excellon(text,name,format_override)
        print(f'Drills: {Path(name).name}: {sum(not h.slot for h in hits)} round, '
              f'{sum(h.slot for h in hits)} routed slots',flush=True)
        result.extend(hits)
    if source.is_file() and zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            for name in sorted(z.namelist()):
                if Path(name).suffix.lower() in suffixes: read_one(name,z.read(name))
    else:
        for p in sorted(source.rglob('*')):
            if p.is_file() and p.suffix.lower() in suffixes: read_one(p.name,p.read_bytes())
    return result
