"""Discover and place any number of internal Gerber copper layers."""
from dataclasses import dataclass
from pathlib import Path
import math
import re
import tempfile
import zipfile
import gerber_to_glb_geometry as g


@dataclass
class InternalLayer:
    number: int
    source: str
    shape: object
    z: float
    thickness: float


def identify(name, text):
    """Return (physical copper index, side) or None for non-copper files."""
    x2=re.search(r'TF\.FileFunction,Copper,L(\d+),(Top|Bot|Inr)',text,re.I)
    if x2: return int(x2[1]),x2[2].lower()
    path=Path(name)
    suffix=path.suffix.lower()
    if suffix=='.gtl': return 1,'top'
    physical=re.search(r'Layer_Physical_Order\s*=\s*(\d+)',text,re.I)
    if suffix=='.gbl': return (int(physical[1]) if physical else None),'bot'
    ordinal=re.fullmatch(r'\.g(\d+)',suffix)
    kicad=re.search(r'(?:^|[-_.])In(\d+)[_.-]Cu(?:\.|$)',path.name,re.I)
    if ordinal or kicad:
        number=int(physical[1]) if physical else int((ordinal or kicad)[1])+1
        return number,'inr'
    return None


def place(numbers, total, thickness, copper, explicit=None):
    if total<2 or any(n<=1 or n>=total for n in numbers):
        raise ValueError('Internal copper numbering conflicts with outer layers')
    z=list(explicit) if explicit is not None else [thickness/2-thickness*(n-1)/(total-1) for n in numbers]
    if len(z)!=len(numbers): raise ValueError('--inner-z needs one centre height per detected internal layer')
    if not math.isfinite(copper) or copper<=0: raise ValueError('Internal copper thickness must be positive')
    if any(not math.isfinite(v) or abs(v)+copper/2>=thickness/2 for v in z):
        raise ValueError('Internal copper must fit inside the board thickness')
    if any(a-b<=copper for a,b in zip(z,z[1:])):
        raise ValueError('Internal copper layers overlap or are not ordered top to bottom')
    return z


def load_internal(source,board,thickness,copper=.035,simplify=.005,explicit=None):
    with tempfile.TemporaryDirectory(prefix='pcb_internal_') as folder:
        source=Path(source)
        if source.is_file() and zipfile.is_zipfile(source):
            with zipfile.ZipFile(source) as z: z.extractall(folder)
            root=Path(folder)
        else: root=source
        found={}; bottom=None
        for p in sorted(root.rglob('*')):
            if not p.is_file(): continue
            if p.suffix.lower() not in {'.gbr','.ger','.gerber','.gtl','.gbl'} and not re.fullmatch(r'\.g\d+',p.suffix,re.I): continue
            with p.open(errors='replace') as f: header=f.read(16384)
            info=identify(p.name,header)
            if info is None: continue
            number,side=info
            if side=='bot':
                if number is not None: bottom=number
                continue
            if side!='inr': continue
            if number in found: raise ValueError(f'Multiple Gerbers claim copper layer L{number}: {found[number][0].name}, {p.name}')
            found[number]=(p,header)
        numbers=sorted(found)
        total=bottom or (max(numbers)+1 if numbers else 2)
        centers=place(numbers,total,thickness,copper,explicit)
        print(f'Copper stack: {total} layer positions; {len(numbers)} internal Gerbers detected',flush=True)
        missing=sorted(set(range(2,total))-set(numbers))
        if missing: print(f'  No artwork supplied for internal layer positions: {missing}',flush=True)
        if numbers:
            print('  Internal Z: '+('user-specified centres' if explicit is not None else 'evenly spaced estimate; Gerbers do not specify dielectric thickness'),flush=True)
        result=[]
        for number,z in zip(numbers,centers):
            p,header=found[number]
            print(f'Rendering internal L{number}: {p.name} at Z={z:.4f} mm',flush=True)
            shape=g.render_gerber(p)
            if re.search(r'TF\.FilePolarity,Negative',header,re.I):
                shape=board.difference(shape)
            shape=g.clean(shape,board)
            if simplify: shape=g.clean(shape.simplify(simplify,preserve_topology=True),board)
            result.append(InternalLayer(number,p.name,shape,z,copper))
        return result
