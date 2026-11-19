"""Prepare phase 10 inputs and SCAPS replay; no product code is changed."""
from pathlib import Path
import json, re, shutil

HERE = Path(__file__).resolve().parent
BASE = HERE.parent

def read(path): return path.read_text(encoding='cp1252')
def write(path, value): path.write_text(value, encoding='cp1252')

def uniform(text, label, value):
    pattern = r'^'+re.escape(label)+r'\s*:[^\n]*'
    match = re.search(pattern, text, re.M)
    assert match, label
    fields = match[0].split(':', 1)[1].split()
    for i in [0,1,2,5,6]: fields[i] = f'{value:.9e}'
    fields[7:9] = ['0','2']
    return text[:match.start()]+label+' :\t'+'\t'.join(fields)+text[match.end():]

def scalar(text, label, value):
    text, n = re.subn(r'(^'+re.escape(label)+r'\s*:\s*)[-+\d.eE]+',lambda m:m[1]+str(value),text,flags=re.M)
    assert n==1, (label,n)
    return text

def disable_external(text):
    return text.replace('voltage V applied to :','Series resistance : 0 (1: Rs enabled; 0: Rs disabled)\nRs [Ohm.m2] : 0.000000e+00\nShunt conductance : 0 (1: Gsh enabled; 0: Gsh disabled)\nGsh [S/m2] : 0.000000e+00\nvoltage V applied to :')

def defect(density, kind='neutral', distribution='single', energy=.75, width=.1):
    return f'''\ninterface recombination

type : {kind}
sigma_nleft : 1.000e-19 [m^2]
sigma_nright : 1.000e-19 [m^2]
sigma_pleft : 1.000e-19 [m^2]
sigma_pright : 1.000e-19 [m^2]
Tunneling to trap: 0 (0: no tunneling, 1: with tunneling)
Relative electron mass : 1.00e+00
Relative hole mass : 1.00e+00
energy distribution : {distribution}
Reference for defect energy : 7 ; above EV left
Et : {energy} [eV]
Ekar : {width} [eV]
N : {density:.9e} [/m^2{'.eV' if distribution=='uniform' else ''}]
'''

header = ['set quitscript.quitSCAPS','set script_display_mode.suppressed']
scripts = {'offset':header.copy(),'density':header.copy(),'schottky':header.copy()}
members=[]
for f in ['CdTe-base.abs','CdS-base.abs','AM1_5G-1sun.spe']:
    shutil.copyfile(BASE/'phase-5'/f,HERE/f)
solar = disable_external(read(BASE/'phase-5/cdte-cds-input.def'))
solar = solar.replace(str(BASE/'phase-5'),str(HERE)).replace(str(BASE/'phase-5').replace('\\','/'),str(HERE))
parts=solar.split('\nlayer\n')
assert len(parts)==3
absorber,buffer=parts[1],parts[2]
interface=absorber.index('\ninterface properties')

def add(group, stem, definition, fields, light):
    write(HERE/(stem+'-input.def'),definition)
    lines=scripts[group]
    lines += ['load definitionfile '+str(HERE/(stem+'-input.def'))]
    if group=='schottky' and fields['interface_Dit_m2_eV']:
        lines += ['set interface1.IFdefect1.duplicate','set interface1.IFdefect2.singleacceptor','set interface1.IFdefect2.Et 0.84']
    lines += ['clear actions','clear simulations','action dark','action workingpoint.temperature 300','action workingpoint.voltage 0','action workingpoint.frequency 1e6','calculate singleshot','save results.eb '+str(HERE/(stem+'.eb'))]
    if light:
        stop=1.6 if fields['conduction_band_offset_eV']>=.8 else 1.15
        fields['voltage_stop_V']=stop
        lines += ['action generationfrominternalmodel','load spectrumfile '+str(HERE/'AM1_5G-1sun.spe'),'action illumination.fromright','action light','action intensity.T 100','action spectrumcutoff.off','action iv.startv -0.2',f'action iv.stopv {stop}','action iv.increment 0.005','action iv.checkaction 1','action iv.continueafterVoc']
        suffix='iv'
    else:
        lines += ['action cv.startv -0.2','action cv.stopv 0','action cv.increment 0.02','action cv.checkaction 1']
        suffix='cv'
    lines += ['calculate singleshot','save results.'+suffix+' '+str(HERE/(stem+'.'+suffix)),'save settings.definitionfile '+str(HERE/(stem+'.def')),'save settings.actionlistfile '+str(HERE/(stem+'.act'))]
    members.append({'id':stem,'group':group,'measurement':suffix,**fields})

for offset in [-.2,-.1,0,.1,.2,.3,.4,.6,.8]:
    newbuffer=uniform(buffer,'chi',3.9-offset)
    add('offset',f'offset-{"m" if offset<0 else "p"}{round(abs(offset)*100):02}',parts[0]+'\nlayer\n'+absorber+'\nlayer\n'+newbuffer,{'conduction_band_offset_eV':offset,'interface_density_m2':0},True)
for offset in [.1,-.2]:
    for density in [0,1e13,1e14,1e15,1e16,1e17]:
        newabs=absorber+(defect(density) if density else '')
        stem=f'density-{int(density):d}' if offset==.1 else f'cliff-density-{int(density):d}'
        add('density',stem,parts[0]+'\nlayer\n'+newabs+'\nlayer\n'+uniform(buffer,'chi',3.9-offset),{'conduction_band_offset_eV':offset,'interface_density_m2':density},True)

# A finite interlayer capacitance permits a dipole; a fixed metal boundary alone cannot pin.
schottky=scalar(disable_external(read(BASE/'phase-3/n-schottky-input.def')),'defectdiscretisations',51)
head,rest=schottky.split('\nlayer\n')
si,tail=rest.split('\nfront contact\n')
interlayer=si
interlayer=scalar(interlayer,'d',5e-10)
interlayer=re.sub(r'^name :.*','name : transparent interlayer surrogate',interlayer,flags=re.M)
for label,value in [('eps',1),('Eg',3),('Na(uniform)',0),('Nd(uniform)',0)]: interlayer=uniform(interlayer,label,value)
ifr='\ninterface properties\n\ninterfacename : Si / interlayer\nIntraband tunneling : 0 (0: no tunneling, 1: with tunneling)\nRelative electron mass : 1\nRelative hole mass : 1\n'
for dit in [0,1e17,1e18]:
    for phi in [4.5,4.6,4.7,4.8,4.9]:
        contact=scalar(tail,'Fi_m',phi)
        # Donors below and acceptors above the midgap neutral level (width 0.56 eV each).
        states='' if not dit else defect(dit,'donor','uniform',.28,.56)
        definition=head+'\nlayer\n'+si+ifr+states+'\nlayer\n'+interlayer+'\nfront contact\n'+contact
        add('schottky',f'pinning-D{int(dit):d}-M{round(phi*100):d}',definition,{'interface_Dit_m2_eV':dit,'metal_workfunction_eV':phi,'interlayer_thickness_m':5e-10,'interlayer_relative_permittivity':1},False)
for group,lines in scripts.items(): write(HERE/(group+'.script'),'\n'.join(lines)+'\n')
write(HERE/'series.script','\n'.join(header+[line for group in scripts.values() for line in group[2:]])+'\n')
(HERE/'manifest.json').write_text(json.dumps(members,indent=2)+'\n')
assert len(members)==36
assert all('phase-5' not in read(HERE/(m['id']+'-input.def')) for m in members if m['group']!='schottky')
print('Prepared 21 light IV + 15 dark CV scans and 36 equilibrium band diagrams.')
