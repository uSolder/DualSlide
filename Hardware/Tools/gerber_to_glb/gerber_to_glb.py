#!/usr/bin/env python3
"""Convert Gerber files to a GLB model."""
from __future__ import annotations
import argparse
import json
import math
import struct
import time
from pathlib import Path
import numpy as np
from shapely import constrained_delaunay_triangles
import gerber_to_glb_geometry as g
from gerber_to_glb_drills import load_drills
from gerber_to_glb_layers import load_internal

VERSION = '1.4'
MASK_COLORS = {'black':(.012,.012,.016), 'green':(.02,.25,.06),
               'blue':(.02,.10,.35), 'red':(.38,.02,.02),
               'purple':(.20,.03,.28), 'white':(.82,.82,.78)}


class Mesh:
    def __init__(self, name, material=None):
        self.name = name
        self.material = material or name
        self.triangles = []

    def cap(self, polygon, z, upward):
        # Constrained triangulation retains concavities and holes; do not use
        # unconstrained Delaunay triangulation plus centroid filtering.
        triangles = constrained_delaunay_triangles(polygon)
        total = 0.0
        for tri in triangles.geoms:
            xy = np.asarray(tri.exterior.coords, dtype=np.float64)[:3,:2]
            a,b = xy[1]-xy[0], xy[2]-xy[0]
            cross = a[0]*b[1]-a[1]*b[0]
            total += abs(cross)/2
            if (cross>0) != upward:
                xy = xy[[0,2,1]]
            self.triangles.append(np.column_stack((xy,np.full(3,z))))
        if abs(total-polygon.area)>max(1e-8,polygon.area*1e-8):
            raise RuntimeError(f'{self.name}: triangulation area mismatch')

    def wall(self,a,b,low,high,outward=True):
        if high-low<=g.EPS:
            return
        q=np.array([(*a,low),(*b,low),(*b,high),(*a,high)],dtype=np.float64)
        for ids in ((0,1,2),(0,2,3)):
            self.triangles.append(q[list(ids if outward else ids[::-1])])

    def board(self,polygon,thickness,plated=None,barrels=None):
        plated_test=g.prep(plated.buffer(g.GRID*2)) if plated is not None and not plated.is_empty else None
        for p in g._polygons(polygon):
            self.cap(p,thickness/2,True)
            self.cap(p,-thickness/2,False)
            for i,ring in enumerate([p.exterior,*p.interiors]):
                points=list(ring.coords)
                if ring.is_ccw != (i==0): points.reverse()
                for a,b in zip(points,points[1:]):
                    target=self
                    if plated_test is not None and plated_test.covers(g.Point((a[0]+b[0])/2,(a[1]+b[1])/2)):
                        target=barrels
                    target.wall(a,b,-thickness/2,thickness/2)

    def slab(self,polygon,low,high):
        for p in g._polygons(polygon):
            self.cap(p,high,True)
            self.cap(p,low,False)
            for i,ring in enumerate([p.exterior,*p.interiors]):
                points=list(ring.coords)
                if ring.is_ccw != (i==0): points.reverse()
                for a,b in zip(points,points[1:]): self.wall(a,b,low,high)

    def relief(self,board,classes,base,top_side):
        """Visible relief only: omit hidden undersides and connecting plates.

        Walls descend to the substrate at footprint boundaries. Portions
        buried inside lower materials are harmless overlaps, not CAD solids.
        Top and bottom artwork are partitioned independently.
        """
        if not any(not region.is_empty for region,_ in classes): return
        cells=g.classify_cells(board,classes,[],0.0,-1.0)
        edges={}
        for cell in cells:
            h=cell.top
            if h>g.EPS:
                self.cap(cell.polygon,base+h if top_side else base-h,top_side)
            for i,ring in enumerate([cell.polygon.exterior,*cell.polygon.interiors]):
                pts=[(round(x,7),round(y,7)) for x,y in ring.coords]
                if ring.is_ccw != (i==0): pts.reverse()
                for a,b in zip(pts,pts[1:]):
                    if a==b: continue
                    key=(a,b) if a<b else (b,a)
                    edges.setdefault(key,[]).append((h,a,b))
        for uses in edges.values():
            if len(uses)>2:
                raise RuntimeError(f'{self.name}: non-manifold 2-D boundary')
            high,a,b=max(uses,key=lambda entry:entry[0])
            low=min(u[0] for u in uses) if len(uses)==2 else 0.0
            if high-low<=g.EPS: continue
            if top_side:
                self.wall(a,b,base+low,base+high)
            else:
                self.wall(a,b,base-high,base-low)

    def arrays(self,center):
        t=np.asarray(self.triangles,dtype=np.float64)
        self.triangles.clear()
        if not len(t): return None
        # Work locally near zero before float32 conversion, avoiding loss of
        # precision for Gerbers with large drawing-coordinate offsets.
        t[:,:,0]-=center[0]; t[:,:,1]-=center[1]
        # Millimetres/Z-up -> metres/Y-up (right-handed glTF convention).
        t=t[:,:,[0,2,1]] * np.array([.001,.001,-.001])
        normals=np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0])
        lengths=np.linalg.norm(normals,axis=1)
        if np.any(lengths==0) or not np.isfinite(t).all():
            raise RuntimeError(f'{self.name}: degenerate/nonfinite mesh')
        normals/=lengths[:,None]
        # Weld position+normal tuples, retaining hard cap/wall boundaries.
        rows=np.column_stack((t.reshape(-1,3),np.repeat(normals,3,axis=0))).astype('<f4')
        vertices,inverse=np.unique(rows,axis=0,return_inverse=True)
        indices=inverse.astype('<u4')
        # Float32 export must not collapse any valid fine-detail triangles.
        ft=vertices[indices,:3].reshape(-1,3,3).astype(np.float64)
        if np.any(np.linalg.norm(np.cross(ft[:,1]-ft[:,0],ft[:,2]-ft[:,0]),axis=1)==0):
            raise RuntimeError(f'{self.name}: detail below float32 precision; try --simplify 0.005')
        return vertices,indices


def build_meshes(a,s,drills=(),internal=(),hole_plating=.025):
    r=g.material_regions(a,s)
    meshes={name:Mesh(name) for name in ('FR4','Copper','Soldermask','Surface_Finish','Silkscreen')}
    holes=g.clean(g.unary_union([h.shape for h in drills]),a.board)
    plated=g.clean(g.unary_union([h.shape for h in drills if h.plated is True]),a.board)
    npth=g.clean(g.unary_union([h.shape for h in drills if h.plated is False]),a.board)
    if drills and holes.is_empty:
        raise ValueError('No drills overlap the board; check coordinate format and units')
    if drills:
        outside=sum(not a.board.intersects(h.shape) for h in drills)
        if outside: print(f'  Drill features outside board ignored: {outside}',flush=True)
        rounds=[h for h in drills if h.plated is True and not h.slot and a.board.intersects(h.shape)]
        for side,openings in [('top',a.top_mask_open),('bottom',a.bottom_mask_open)]:
            covered=sum(h.shape.intersection(openings).area<1e-8 for h in rounds)
            print(f'  {side}: {covered}/{len(rounds)} plated round holes fully tented by mask artwork',flush=True)
    # Drill FR4 and copper, but do not subtract plated bores from mask: doing
    # that would punch through the very tents encoded by the soldermask data.
    # Non-plated mechanical holes remain physically open regardless of mask.
    # Preserve the nominal finished bore. Grow the substrate opening outward
    # to make room for actual copper and finish sleeves, rather than merely
    # recolouring the same wall. NPTH tools never receive sleeves.
    finish_outer=g.clean(plated.buffer(s.finish,quad_segs=12),a.board)
    plating_outer=g.clean(plated.buffer(s.finish+hole_plating,quad_segs=12),a.board)
    substrate_void=g.clean(holes.union(plating_outer),a.board)
    meshes['FR4'].board(g.clean(a.board.difference(substrate_void)),s.board)
    if not plated.is_empty:
        meshes['Hole_Copper']=Mesh('Hole_Copper','Copper')
        meshes['Hole_Finish']=Mesh('Hole_Finish','Surface_Finish')
        copper_sleeve=g.clean(plating_outer.difference(finish_outer).difference(npth),a.board)
        finish_sleeve=g.clean(finish_outer.difference(holes),a.board)
        meshes['Hole_Copper'].slab(copper_sleeve,s.bottom-s.copper,s.top+s.copper)
        meshes['Hole_Finish'].slab(finish_sleeve,s.bottom-s.copper,s.top+s.copper)
        print(f'  Plated bores: {hole_plating:g} mm copper + {s.finish:g} mm finish sleeves',flush=True)
    for key in ('tml','tmh','bml','bmh','tsl','tsh','bsl','bsh'):
        r[key]=g.clean(r[key].difference(npth),a.board)
    top_cu=g.clean(a.top_cu.difference(substrate_void),a.board)
    bottom_cu=g.clean(a.bottom_cu.difference(substrate_void),a.board)
    r['te']=g.clean(r['te'].difference(holes),a.board)
    r['be']=g.clean(r['be'].difference(holes),a.board)
    specifications={
        'Copper': ([(top_cu,s.copper)],[(bottom_cu,s.copper)]),
        'Soldermask': ([(r['tml'],s.mask),(r['tmh'],s.copper+s.mask)],
                       [(r['bml'],s.mask),(r['bmh'],s.copper+s.mask)]),
        'Surface_Finish': ([(r['te'],s.finish)],[(r['be'],s.finish)]),
        'Silkscreen': ([(r['tsl'],s.mask+s.silk),(r['tsh'],s.copper+s.mask+s.silk)],
                       [(r['bsl'],s.mask+s.silk),(r['bsh'],s.copper+s.mask+s.silk)]),
    }
    for name,(top,bottom) in specifications.items():
        started=time.perf_counter()
        offset=s.copper if name=='Surface_Finish' else 0
        meshes[name].relief(a.board,top,s.top+offset,True)
        meshes[name].relief(a.board,bottom,s.bottom-offset,False)
        print(f'  {name}: {len(meshes[name].triangles):,} triangles in {time.perf_counter()-started:.1f} s',flush=True)
    for layer in internal:
        name=f'Copper_L{layer.number:02d}'
        meshes[name]=Mesh(name,'Copper')
        shape=g.clean(layer.shape.difference(substrate_void),a.board)
        meshes[name].slab(shape,layer.z-layer.thickness/2,layer.z+layer.thickness/2)
        print(f'  {name}: {len(meshes[name].triangles):,} triangles at Z={layer.z:.4f} mm',flush=True)
    return meshes


def write_glb(meshes,output,center,mask_color,finish):
    colors={'FR4':(.16,.22,.10),'Copper':(.72,.28,.08),
            'Soldermask':MASK_COLORS[mask_color],
            'Surface_Finish':(.88,.67,.18) if finish=='enig' else (.72,.74,.76),
            'Silkscreen':(.92,.92,.88)}
    doc={'asset':{'version':'2.0','generator':f'Gerber to GLB {VERSION}'},
         'scene':0,'scenes':[{'nodes':[]}], 'nodes':[], 'meshes':[],
         'materials':[], 'bufferViews':[], 'accessors':[]}
    binary=bytearray()
    def view(data,target,stride=None):
        binary.extend(b'\0'*(-len(binary)%4))
        result={'buffer':0,'byteOffset':len(binary),'byteLength':len(data),'target':target}
        if stride is not None: result['byteStride']=stride
        doc['bufferViews'].append(result); binary.extend(data)
        return len(doc['bufferViews'])-1
    def accessor(v,offset,component,count,kind,minimum=None,maximum=None):
        item={'bufferView':v,'byteOffset':offset,'componentType':component,'count':count,'type':kind}
        if minimum is not None: item.update(min=minimum,max=maximum)
        doc['accessors'].append(item); return len(doc['accessors'])-1
    for name,mesh in meshes.items():
        arrays=mesh.arrays(center)
        if arrays is None: continue
        vertices,indices=arrays
        v=view(vertices.tobytes(),34962,24)
        pos=accessor(v,0,5126,len(vertices),'VEC3',vertices[:,:3].min(axis=0).tolist(),vertices[:,:3].max(axis=0).tolist())
        norm=accessor(v,12,5126,len(vertices),'VEC3')
        iv=view(indices.tobytes(),34963)
        ind=accessor(iv,0,5125,len(indices),'SCALAR')
        number=len(doc['meshes'])
        material=mesh.material
        metal=material in ('Copper','Surface_Finish')
        doc['materials'].append({'name':name,'pbrMetallicRoughness':{
            'baseColorFactor':[*colors[material],1.0],'metallicFactor':1.0 if metal else 0.0,
            'roughnessFactor':.28 if metal else (.38 if material=='Soldermask' else .65)},
            'alphaMode':'OPAQUE','doubleSided':False})
        doc['meshes'].append({'name':name,'primitives':[{'attributes':{'POSITION':pos,'NORMAL':norm},
                                                      'indices':ind,'material':number,'mode':4}]})
        doc['nodes'].append({'name':name,'mesh':number}); doc['scenes'][0]['nodes'].append(number)
        print(f'  GLB {name}: {len(indices)//3:,} triangles, {len(vertices):,} vertices',flush=True)
    doc['buffers']=[{'byteLength':len(binary)}]
    encoded=json.dumps(doc,separators=(',',':'),allow_nan=False).encode()
    encoded+=b' '*(-len(encoded)%4); binary.extend(b'\0'*(-len(binary)%4))
    size=12+8+len(encoded)+8+len(binary)
    if size>=2**32: raise ValueError('GLB exceeds 4 GiB format limit')
    output.parent.mkdir(parents=True,exist_ok=True)
    # Atomic replacement prevents an interrupted export leaving a partial GLB.
    temporary=output.with_suffix(output.suffix+'.tmp')
    with temporary.open('wb') as f:
        f.write(struct.pack('<4sII',b'glTF',2,size))
        f.write(struct.pack('<I4s',len(encoded),b'JSON')); f.write(encoded)
        f.write(struct.pack('<I4s',len(binary),b'BIN\0')); f.write(binary)
    temporary.replace(output)
    return doc


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',nargs='?',type=Path)
    p.add_argument('-o','--output',type=Path,default=Path('gerber_to_glb.glb'))
    p.add_argument('--thickness',type=float,default=1.0)
    for name,value in [('copper',.035),('mask',.020),('finish',.004),('silk',.015)]:
        p.add_argument(f'--{name}-thickness',type=float,default=value)
    p.add_argument('--mask-color',choices=MASK_COLORS,default='black')
    p.add_argument('--finish',choices=('enig','hasl'),default='enig')
    p.add_argument('--simplify',type=float,default=.005,help='Artwork tolerance in mm; 0 preserves original contours')
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--drills',choices=('auto','off'),default='auto',help='Read Excellon files from the ZIP/folder (default auto)')
    p.add_argument('--drill-format',help='Override implicit Excellon coordinates, e.g. 2:5; normally read from file')
    p.add_argument('--hole-plating',type=float,default=.025,help='Copper barrel thickness in mm; nominal drill diameter remains the finished opening')
    p.add_argument('--internal-layers',choices=('auto','off'),default='auto')
    p.add_argument('--inner-copper-thickness',type=float,default=.035)
    p.add_argument('--inner-z',help='Comma-separated internal copper centre heights in mm, top-to-bottom; default evenly spaced')
    args=p.parse_args()
    if not args.self_test and args.source is None: p.error('source ZIP/folder is required')
    if args.output.suffix.lower()!='.glb': p.error('output must have a .glb extension')
    thicknesses=[args.thickness,args.copper_thickness,args.mask_thickness,args.finish_thickness,args.silk_thickness]
    if any(not math.isfinite(v) or v<=0 for v in thicknesses): p.error('thicknesses must be finite and positive')
    if not math.isfinite(args.simplify) or args.simplify<0: p.error('--simplify must be finite and nonnegative')
    if any(not math.isfinite(v) or v<=0 for v in (args.hole_plating,args.inner_copper_thickness)):
        p.error('Plating and internal copper thickness must be finite and positive')
    try: inner_z=[float(v) for v in args.inner_z.split(',')] if args.inner_z else None
    except ValueError: p.error('--inner-z must be comma-separated numeric heights')
    drill_format=None
    if args.drill_format:
        try:
            drill_format=tuple(int(n) for n in args.drill_format.split(':'))
            if len(drill_format)!=2 or any(n<1 or n>8 for n in drill_format): raise ValueError()
        except ValueError: p.error('--drill-format must be integer:decimal digits, e.g. 2:5')
    started=time.perf_counter()
    print(f'Gerber to GLB v{VERSION}',flush=True)
    a=g.synthetic_artwork() if args.self_test else g.load_artwork(args.source,args.simplify)
    drills=[] if args.self_test or args.drills=='off' else load_drills(args.source,drill_format)
    internal=[] if args.self_test or args.internal_layers=='off' else load_internal(
        args.source,a.board,args.thickness,args.inner_copper_thickness,args.simplify,inner_z)
    if not args.self_test and args.drills=='auto' and not drills:
        print('No supported Excellon drill files found.',flush=True)
    bounds=a.board.bounds
    center=((bounds[0]+bounds[2])/2,(bounds[1]+bounds[3])/2)
    print('Computing material overlaps and meshes...',flush=True)
    meshes=build_meshes(a,g.Stackup(*thicknesses),drills,internal,args.hole_plating)
    print('Writing GLB...',flush=True)
    doc=write_glb(meshes,args.output,center,args.mask_color,args.finish)
    if args.self_test:
        assert len(doc['meshes'])==5
        print('Synthetic geometry and GLB serialization check passed (not a viewer import test).')
    print(f'Wrote {args.output}: {args.output.stat().st_size/1048576:.1f} MiB in {time.perf_counter()-started:.1f} s',flush=True)


if __name__=='__main__':
    main()
