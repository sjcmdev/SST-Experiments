"""Eight one-factor SCAPS robustness cases; writes only phase-11 artifacts."""
from pathlib import Path
import json, re, shutil

HERE = Path(__file__).resolve().parent
BASE = HERE.parent / 'phase-5'
base = (BASE / 'si-pn-input.def').read_text(encoding='cp1252')
shutil.copyfile(BASE / 'Si.abs', HERE / 'Si.abs')
base = base.replace(str(BASE), str(HERE))
base = base.replace('voltage V applied to :', 'Series resistance : 0\nRs [Ohm.m2] : 0\nShunt conductance : 0\nGsh [S/m2] : 0\nvoltage V applied to :')

def scalar(text, label, value):
    text, n = re.subn(r'(^'+re.escape(label)+r'\s*:\s*)[-+\d.eE]+', lambda m:m[1]+str(value), text, flags=re.M)
    assert n == 1, (label, n)
    return text

def uniform(text, label, value):
    match = re.search(r'^'+re.escape(label)+r'\s*:[^\n]*', text, re.M)
    assert match, label
    fields = match[0].split(':',1)[1].split()
    for i in (0,1,2,5,6): fields[i] = f'{value:.9e}'
    fields[7:9] = ['0','2']
    return text[:match.start()]+label+' :\t'+'\t'.join(fields)+text[match.end():]

cases = [
    dict(id='baseline', temperature_K=300),
    dict(id='low80', temperature_K=80),
    dict(id='low40', temperature_K=40),
    dict(id='doping', temperature_K=300, doping_m3=2e25),
    dict(id='deep', temperature_K=300, trap_density_m3=1e25),
    dict(id='thin', temperature_K=300, thickness_scale=.01),
    dict(id='thick', temperature_K=300, thickness_scale=100),
    dict(id='forward', temperature_K=300, voltage_stop_V=2),
]
for c in cases:
    head, p, n = base.split('\nlayer\n')
    if 'doping_m3' in c:
        p = uniform(p, 'Na(uniform)', c['doping_m3'])
        n = uniform(n, 'Nd(uniform)', c['doping_m3'])
    if 'trap_density_m3' in c:
        p = uniform(p, 'Nt(uniform)', c['trap_density_m3'])
        for label in ('Nleft','Nright'): p = scalar(p,label,c['trap_density_m3'])
    if 'thickness_scale' in c:
        p = scalar(p,'d',5e-6*c['thickness_scale'])
        n = scalar(n,'d',2e-7*c['thickness_scale'])
    definition = head+'\nlayer\n'+p+'\nlayer\n'+n
    stem = c['id']
    (HERE/(stem+'-input.def')).write_text(definition,encoding='cp1252')
    c.update(voltage_start_V=0, voltage_step_V=.02)
    c.setdefault('voltage_stop_V',.6)
    lines = ['set quitscript.quitSCAPS','set script_display_mode.suppressed',
        'load definitionfile '+str(HERE/(stem+'-input.def')),
        'clear actions','clear simulations','action dark',
        f'action workingpoint.temperature {c["temperature_K"]}',
        'action workingpoint.voltage 0','action workingpoint.frequency 1e6',
        'save settings.definitionfile '+str(HERE/(stem+'.def')),
        'action iv.startv 0',f'action iv.stopv {c["voltage_stop_V"]}',
        'action iv.increment 0.02','action iv.checkaction 1','action iv.continueafterVoc',
        'save settings.actionlistfile '+str(HERE/(stem+'.act')),
        'calculate singleshot','save results.iv '+str(HERE/(stem+'.iv')),
        'save results.eb '+str(HERE/(stem+'-final.eb')),
        'save settings.definitionfile '+str(HERE/(stem+'-final.def'))]
    (HERE/(stem+'.script')).write_text('\n'.join(lines)+'\n',encoding='cp1252')
(HERE/'manifest.json').write_text(json.dumps(cases,indent=2)+'\n')
assert len(cases)==8 and len({c['id'] for c in cases})==8
assert '2e-07' not in scalar('d : 2e-07 [m]','d',1e-9)
print('Prepared eight isolated cases, each with pre-calculation definition and action snapshots.')

for source, name, step, stop, settings in [
    ('forward','forward-safe',.02,1.66,{}),
    ('forward','forward-fine',.005,2,{}),
    ('forward','forward-clamp',.02,2,dict(clamp_psi=.1,clamp_n=.1,clamp_p=.1,maxiter=5000)),
    ('forward','forward-fixed',.02,2,{'recalculate mesh':0}),
    ('forward','forward-logged',.02,2,{}),
    ('doping','doping-tight',.02,.6,{'termination_psi':1e-6,'termination_Fn':1e-6,'termination_Fp':1e-6,'maximum ratio between neighbouring points':1.2}),
    ('low80','low80-tight',.02,.6,dict(termination_psi=1e-6,termination_Fn=1e-6,termination_Fp=1e-6)),
]:
    definition = (HERE/(source+'-input.def')).read_text(encoding='cp1252')
    for key, value in settings.items(): definition = scalar(definition,key,value)
    (HERE/(name+'-input.def')).write_text(definition,encoding='cp1252')
    script = (HERE/(source+'.script')).read_text().replace(source,name)
    script = script.replace('action iv.increment 0.02',f'action iv.increment {step}')
    script = script.replace('action iv.stopv 2',f'action iv.stopv {stop}')
    script = script.replace('set script_display_mode.suppressed','set script_display_mode.suppressed\nset errorhandling.appendtofile')
    (HERE/(name+'.script')).write_text(script)
