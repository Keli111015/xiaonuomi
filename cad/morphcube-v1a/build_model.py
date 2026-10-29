"""Xiaonuomi V1A fit prototype. Units mm. Run with cadquery 2.8 / trimesh.
All electronics envelopes are assumptions, not measured production drawings.
Coordinates: X right, Y rear, Z up; robot looks toward -Y.
"""
from pathlib import Path
import json, math
import cadquery as cq
import trimesh

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'exports'
OUT.mkdir(exist_ok=True)
P = dict(body_width=126, body_depth=104, body_height=100, body_z=48,
         corner_radius=24, roof_radius=18, wall=2.4,
         servo_width=22.4, servo_depth=12.5, servo_height=23.8,
         servo_clearance=0.8, servo_axis_offset=5.5, servo_lift=3.2, base_radius=56,
         main_board_width=60, main_board_height=85, main_board_depth=18)
if (ROOT/'parameters.json').exists():
    P.update(json.loads((ROOT/'parameters.json').read_text(encoding='utf-8')))
(ROOT/'parameters.json').write_text(json.dumps(P, indent=2), encoding='utf-8')

def box(w,d,h,x=0,y=0,z=0):
    return cq.Workplane('XY').box(w,d,h,centered=(True,True,False)).translate((x,y,z))
def cyl(r,h,x=0,y=0,z=0):
    return cq.Workplane('XY').circle(r).extrude(h).translate((x,y,z))
def rounded(w,d,h,r,z=0):
    return box(w,d,h,z=z).edges('|Z').fillet(r)
def front(w,h,t,r,x=0,y=-52,z=100):
    # XZ workplane extrudes toward -Y.
    return cq.Workplane('XZ').rect(w,h).extrude(t).edges('|Y').fillet(r).translate((x,y,z))
def front_hole(r,depth,x,y,z):
    return cq.Workplane('XZ').circle(r).extrude(depth).translate((x,y,z))
def hole_z(part, points, radius, z, depth):
    for x,y in points:
        part=part.cut(cyl(radius,depth,x,y,z))
    return part

parts={}; refs={}; colors={}
def add(name, shape, color, ref=False):
    assert shape.val().isValid(), name+' invalid CAD'
    assert len(shape.solids().vals()) == 1, name+' must be one solid'
    (refs if ref else parts)[name]=shape
    colors[name]=color

w,d,h,t=P['body_width'],P['body_depth'],P['body_height'],P['wall']
z=P['body_z']; r=P['corner_radius']; roof=P['roof_radius']
outer=rounded(w,d,h,r,z).edges('>Z').fillet(roof)
inner=rounded(w-2*t,d-2*t,h+2-t,r-t,z-2).edges('>Z').fillet(roof-t)
shell=outer.cut(inner)
shell_points=[(x,y) for x in (-52,52) for y in (-39,39)]
for x,y in shell_points:
    shell=shell.union(cyl(5.5,12,x,y,z+3))
shell=hole_z(shell,shell_points,1.3,z+2,14)
# Front plate M2 pilot holes; microphone/camera provisional ports.
face_points=[(x,zz) for x in (-34,34) for zz in (89,123)]
for x,zz in face_points:
    shell=shell.cut(front_hole(0.9,8,x,-48,zz))
shell=shell.cut(front_hole(4,10,0,-46,73))
shell=shell.cut(front_hole(1.5,10,0,-46,66))
# Rear cable/service access (not a final aligned USB socket).
shell=shell.cut(front(32,18,12,4,y=58,z=69))
# Side speaker grille: slots on right side; bracket is intentionally uncommitted.
for yy in (-12,-6,0,6,12):
    shell=shell.cut(box(12,2.5,24,x=61,y=yy,z=83))
add('01_body_shell',shell,'#eee8de')

floor=rounded(w-2*t-1.2,d-2*t-1.2,3,r-t-0.6,z)
floor=hole_z(floor,shell_points,1.7,z-1,5)
drive_points=[(x,y) for x in (-12,12) for y in (-12,12)]
floor=hole_z(floor,drive_points,1.7,z-1,5)
floor=floor.cut(cyl(5,5,26,0,z-1)).cut(cyl(4,5,0,0,z-1))
carrier_points=[(-25,20),(25,20),(-18,-14),(18,-14)]
floor=hole_z(floor,carrier_points,1.7,z-1,5)
add('02_internal_floor',floor,'#ccc8be')

base=cyl(P['base_radius'],38).cut(cyl(P['base_radius']-t,39,z=3))
base_pts=[(45*math.cos(math.radians(a)),45*math.sin(math.radians(a))) for a in (45,135,225,315)]
for x,y in base_pts:
    # Radial ribs bridge bosses to shell.
    a=math.degrees(math.atan2(y,x))
    rib=box(12,5,33,x=49,z=3).rotate((0,0,0),(0,0,1),a)
    base=base.union(rib).union(cyl(4.5,35,x,y,3))
base=hole_z(base,base_pts,1.3,26,15)
base=base.cut(front(16,10,12,3,y=60,z=13))
# Universal cradle mounting pilot holes (M3 self tapping after fit check).
cradle_pts=[(-22,0),(16,0)]
base=hole_z(base,cradle_pts,1.3,-1,5)
add('03_fixed_base',base,'#a9c3b7')
cap=cyl(P['base_radius']-0.2,3,z=38).cut(cyl(40,5,z=37))
cap=hole_z(cap,base_pts,1.7,37,5)
add('04_base_cap',cap,'#b5ccc0')

# PTFE thrust washer: bought/cut sheet, NOT an FDM structural bearing claim.
washer=cyl(46,1,z=41).cut(cyl(40,3,z=40))
add('REF_PTFE_thrust_washer_1mm',washer,'#f4efe5',True)
rotor=cyl(47,3,z=42).union(cyl(20,3,z=45))
collar=cyl(39.5,3,z=39).cut(cyl(36.5,5,z=38))
rotor=rotor.union(collar)
rotor=hole_z(rotor,drive_points,1.3,43,6)
rotor=hole_z(rotor,[(-8,0),(8,0)],1.1,41,8)
rotor=rotor.cut(cyl(2,9,z=41)).cut(cyl(5,5,26,0,41))
add('05_rotating_platform',rotor,'#819f92')

# Cradle is removable; servo dimensions are only an SG90 assumption.
sx=-P['servo_axis_offset']; sw=P['servo_width']+P['servo_clearance']; sd=P['servo_depth']+P['servo_clearance']
cradle=box(48,24,3,x=-3,z=3)
cradle=cradle.union(box(sw+4,sd+4,23,x=sx,z=6).cut(box(sw,sd,26,x=sx,z=6)))
# Cable exit in cradle rear wall; retained by cable tie, not assumed horn spline.
cradle=cradle.cut(box(8,10,8,x=sx,y=sd/2,z=6))
cradle=hole_z(cradle,cradle_pts,1.7,2,5)
for xx in (-18,10):
    cradle=cradle.cut(box(3,6,5,x=xx,y=-9,z=2))
add('06_servo_cradle',cradle,'#8fb4a5')
add('06b_servo_height_shim',box(P['servo_width']-.4,P['servo_depth']-.4,P['servo_lift'],x=sx,z=6),'#dac49e')

def carrier(name,width,height,y,bolt_x):
    c=box(width,24,3,y=y,z=51).union(box(width,3,height,y=y+10.5,z=54))
    c=hole_z(c,[(-bolt_x,y),(bolt_x,y)],1.7,50,5)
    for zz in (70,54+height-14):
        for xx in (-width/2+8,width/2-8):
            c=c.cut(front(5,8,6,1,x=xx,y=y+14,z=zz))
    add(name,c,'#c8d0c6')
carrier('07_main_board_carrier',70,80,20,25)
carrier('08_audio_board_carrier',54,58,-14,18)

face=front(86,50,3,14,y=-52,z=106)
for x,zz in face_points:
    face=face.cut(front_hole(1.1,5,x,-51,zz))
add('09_face_plate',face,'#263d40')
for x,label in ((-20,'L'),(20,'R')):
    add('10_eye_'+label,front(14,23,1.2,6.9,x=x,y=-55,z=109),'#9fe0cd')
    add('11_eye_glint_'+label,front(3,5,0.5,1.4,x=x-2,y=-56.2,z=114),'#ffffff')
    add('12_cheek_'+label,front(9,3,0.8,1.4,x=x*1.5,y=-55,z=95),'#e3b6a4')

# Reference envelopes are excluded from printable STL set.
add('REF_main_board_ASSUMED',box(P['main_board_width'],P['main_board_depth'],P['main_board_height'],y=17,z=55),'#286b58',True)
add('REF_audio_board_ASSUMED',box(46,12,48,y=-17,z=55),'#377960',True)
add('REF_servo_SG90_ASSUMED',box(P['servo_width'],P['servo_depth'],P['servo_height'],x=sx,z=6+P['servo_lift']),'#4d85bb',True)
add('REF_servo_shaft_ASSUMED',cyl(2.5,8,z=6+P['servo_lift']+P['servo_height']),'#c5c5bd',True)
horn=cyl(13,2,z=40).cut(cyl(2.5,4,z=39))
horn=hole_z(horn,[(-8,0),(8,0)],1.1,39,4)
add('REF_original_horn_ADAPTER_TBD',horn,'#f6f4eb',True)

assembly=cq.Assembly(name='Xiaonuomi_V1A_FIT_PROTOTYPE')
geometry={}; checks=[]
for name,shape in {**parts,**refs}.items():
    col=colors[name].lstrip('#'); rgb=tuple(int(col[i:i+2],16)/255 for i in (0,2,4))
    assembly.add(shape,name=name,color=cq.Color(*rgb,0.38 if name.startswith('REF_') else 1))
    verts,tris=shape.val().tessellate(0.18,0.12)
    vv=[[v.x,v.y,v.z] for v in verts]; ff=[list(f) for f in tris]
    geometry[name]=dict(vertices=vv,faces=ff,color=colors[name],reference=name.startswith('REF_'))
    if name in parts:
        cq.exporters.export(shape,str(OUT/(name+'.step')))
        mesh=trimesh.Trimesh(vv,ff,process=True)
        mesh.merge_vertices()
        assert mesh.is_watertight, name+' mesh not watertight'
        assert mesh.volume > 0, name+' mesh orientation'
        # STL positioned at Z=0 to aid slicers; orientation remains assembly orientation.
        mesh.apply_translation([0,0,-mesh.bounds[0,2]])
        mesh.export(OUT/(name+'.stl'))
        bb=shape.val().BoundingBox()
        checks.append(dict(part=name,valid=True,watertight=True,volume_mm3=round(shape.val().Volume(),2),size_mm=[round(bb.xlen,2),round(bb.ylen,2),round(bb.zlen,2)]))
assembly.export(str(OUT/'Xiaonuomi_V1A_assembly.step'))
# Pairwise collision checks for printed parts (touching faces allowed).
collisions=[]
names=list(parts)
for i,a in enumerate(names):
    for b in names[i+1:]:
        v=parts[a].intersect(parts[b]).val().Volume()
        if v>0.1: collisions.append(dict(a=a,b=b,overlap_mm3=round(v,3)))
(ROOT/'geometry.json').write_text(json.dumps(geometry,separators=(',',':')),encoding='utf-8')
hardware_collisions=[]
for a in ('REF_main_board_ASSUMED','REF_audio_board_ASSUMED','REF_servo_SG90_ASSUMED'):
    for b in parts:
        v=refs[a].intersect(parts[b]).val().Volume()
        if v>0.1: hardware_collisions.append(dict(a=a,b=b,overlap_mm3=round(v,3)))
(ROOT/'validation.json').write_text(json.dumps(dict(parts=checks,printed_part_collisions=collisions,assumed_hardware_collisions=hardware_collisions,notes='Hardware envelopes and joints are provisional; no torque or tolerance validation.'),indent=2),encoding='utf-8')
print(json.dumps(dict(parts=len(parts),references=len(refs),collisions=collisions),indent=2))
