from pathlib import Path
import re
h=Path('runs/phase-7').resolve()
n=Path('runs/phase-3/n-schottky-input.def').read_text()
p=Path('runs/phase-1/p-schottky-gui.def').read_text()
seed=Path('runs/phase-3/defect-seed.def').read_text()
num=n[n.index('convergence'):n.index('back contact')].replace('defectdiscretisations :     7','defectdiscretisations :    51')
block=seed[seed.index('srhrecombination'):seed.index('front contact')]
base=p.replace('back contact',num+'back contact',1)
for model,levels in [('single',[.35]),('gaussian',[.35]),('double',[.28]),('baseline',[])]:
    blocks=[]
    for e in levels:
        b=block.replace('type : neutral','type : donor').replace('leveltype : neutral','leveltype : donor')
        b=re.sub(r'Et :[^\n]+',f'Et : {e:.3f} [eV]',b)
        b=b.replace('1.000000e+20','5.000000e+20').replace('1.000e+20','5.000e+20')
        if model=='gaussian': b=b.replace('energy distribution : single','energy distribution : gauss').replace('Ekar :  0.1000','Ekar :  0.0354')
        blocks.append(b)
    text=base.replace('front contact',''.join(blocks)+'front contact',1)
    (h/f'{model}-input.def').write_text(text)
# Baseline and three devices, all nine temperatures; save equilibrium bands before each C-f scan.
lines=['set quitscript.quitSCAPS']
for model in ['baseline','single','gaussian','double']:
    lines += [f'load definitionfile {h/model}-input.def','clear actions','action dark','action workingpoint.voltage 0']
    if model=='double': lines += ['set layer1.defect1.duplicate','set layer1.defect2.Et 0.43']
    for t in range(150,351,25):
        dest=h/f'{model}-T{t}'
        lines += ['clear actions','action dark','action workingpoint.voltage 0',f'action workingpoint.temperature {t}','clear simulations','calculate singleshot',f'save results.eb {dest}.eb', 'action cf.startf 1','action cf.stopf 1e7','action cf.points.per.decade 30','action cf.checkaction 1','clear simulations','calculate singleshot',f'save results.cf {dest}.cf',f'save settings.definitionfile {dest}.def',f'save settings.actionlistfile {dest}.act']
(h/'series.script').write_text('\n'.join(lines)+'\n')
assert all((h/f'{m}-input.def').read_text().count('srhrecombination')==k for m,k in [('baseline',0),('single',1),('gaussian',1),('double',1)])
print('4 input definitions and 36-scan replay generated')

