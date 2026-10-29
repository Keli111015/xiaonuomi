from pathlib import Path
import json, math
import vtk
from PIL import Image, ImageDraw, ImageFont
R=Path(__file__).resolve().parent
geo=json.loads((R/'geometry.json').read_text())

def offset(name,mode):
    if mode!='exploded': return (0,0,0)
    if name=='01_body_shell': return (0,0,100)
    if name.startswith(('09_','10_','11_','12_')): return (0,-50,100)
    if name.startswith(('07_','08_','REF_main','REF_audio')): return (0,0,65)
    if name=='02_internal_floor': return (0,0,42)
    if name=='05_rotating_platform': return (0,0,24)
    if name.startswith(('04_','REF_PTFE')): return (0,0,10)
    return (0,0,0)

def render(mode):
    renderer=vtk.vtkRenderer()
    renderer.SetBackground(.91,.93,.91)
    renderer.SetBackground2(.99,.98,.95)
    renderer.GradientBackgroundOn()
    for name,g in geo.items():
        if mode=='beauty' and g['reference']: continue
        if mode=='inside' and name.startswith(('01_','09_','10_','11_','12_')): continue
        points=vtk.vtkPoints(); off=offset(name,mode)
        for p in g['vertices']: points.InsertNextPoint(*[p[i]+off[i] for i in range(3)])
        cells=vtk.vtkCellArray()
        for tri in g['faces']:
            cells.InsertNextCell(3)
            for i in tri: cells.InsertCellPoint(i)
        poly=vtk.vtkPolyData(); poly.SetPoints(points); poly.SetPolys(cells)
        normals=vtk.vtkPolyDataNormals(); normals.SetInputData(poly); normals.SetFeatureAngle(45)
        mapper=vtk.vtkPolyDataMapper(); mapper.SetInputConnection(normals.GetOutputPort())
        actor=vtk.vtkActor(); actor.SetMapper(mapper)
        c=g['color'].lstrip('#'); rgb=[int(c[i:i+2],16)/255 for i in (0,2,4)]
        actor.GetProperty().SetColor(*rgb); actor.GetProperty().SetAmbient(.26)
        actor.GetProperty().SetDiffuse(.72); actor.GetProperty().SetSpecular(.18); actor.GetProperty().SetSpecularPower(35)
        renderer.AddActor(actor)
    cam=renderer.GetActiveCamera()
    target=120 if mode=='exploded' else 75
    cam.SetPosition(245,-370,target+175); cam.SetFocalPoint(0,0,target); cam.SetViewUp(0,0,1)
    cam.ParallelProjectionOn(); cam.SetParallelScale(165 if mode=='exploded' else 99)
    light=vtk.vtkLight(); light.SetPosition(-200,-300,400); light.SetFocalPoint(0,0,70); light.SetIntensity(.85); renderer.AddLight(light)
    win=vtk.vtkRenderWindow(); win.SetOffScreenRendering(1); win.SetSize(1200,1200); win.SetMultiSamples(8); win.AddRenderer(renderer); win.Render()
    capture=vtk.vtkWindowToImageFilter(); capture.SetInput(win); capture.Update()
    writer=vtk.vtkPNGWriter(); writer.SetFileName(str(R/(mode+'.png'))); writer.SetInputConnection(capture.GetOutputPort()); writer.Write(); win.Finalize()
    im=Image.open(R/(mode+'.png')).convert('RGB'); draw=ImageDraw.Draw(im)
    font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',28)
    small=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',19)
    titles={'beauty':'小诺米 · 圆润桌面旋转款','exploded':'结构拆解 · 自下而上装配','inside':'内部布局 · 板卡为待实测占位'}
    draw.text((48,35),titles[mode],font=font,fill='#263d40')
    draw.text((48,79),'V1A  /  试装概念模型  /  单位 mm',font=small,fill='#597067')
    draw.text((48,1140),'尺寸约 126 × 112 × 148  |  装饰眼睛，无显示屏  |  打印前核对硬件',font=small,fill='#597067')
    im.save(R/(mode+'.png'))
for mode in ('beauty','exploded','inside'): render(mode)
print('Preview PNGs generated from CAD geometry.')
