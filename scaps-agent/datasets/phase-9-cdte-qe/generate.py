"""Prepare local copies of SCAPS example cells and a replayable series."""
from pathlib import Path
import re, shutil

HERE = Path(__file__).resolve().parent
INSTALL = Path('D:/Aplications/SCAPS/install')
lines = ['set quitscript.quitSCAPS', 'set script_display_mode.suppressed']
shutil.copyfile(HERE.parent/'phase-5/AM1_5G-1sun.spe', HERE/'AM1_5G-1sun.spe')
for cell, name in [('cigs', 'example CIGS.def'), ('cdte', 'CdTe-base.def')]:
    shutil.copyfile(INSTALL/'def'/name, HERE/(cell+'-original.def'))
    definition = (INSTALL/'def'/name).read_text(encoding='cp1252')
    if cell=='cdte':
        definition = definition.replace('recalculate mesh :  0', 'recalculate mesh :  1')
    for filename in re.findall(r'^absorptionfile[^:]*:\s*(.+)$', definition, re.M):
        filename = filename.strip()
        shutil.copyfile(INSTALL/'absorption'/filename, HERE/filename)
        definition = definition.replace(': '+filename, ': '+str(HERE/filename))
    # Explicitly disable external parasitics so inherited session state cannot leak in.
    definition = definition.replace('voltage V applied to :', 'Series resistance : 0 (1: Rs enabled; 0: Rs disabled)\nRs [Ohm.m2] : 0.000000e+00\nShunt conductance : 0 (1: Gsh enabled; 0: Gsh disabled)\nGsh [S/m2] : 0.000000e+00\nvoltage V applied to :')
    (HERE/(cell+'-input.def')).write_text(definition, encoding='cp1252')
    lines += ['load definitionfile '+str(HERE/(cell+'-input.def'))]
    for mode in ['dark', 'light', 'qe']:
        for temperature in ([300] if mode=='qe' else range(200,341,20)):
            stem = f'{cell}-{mode}-T{temperature}'
            lines += ['clear actions', 'clear simulations', f'action workingpoint.temperature {temperature}', 'action workingpoint.voltage 0', 'action workingpoint.frequency 1e6']
            if mode=='light':
                lines += ['action generationfrominternalmodel', 'load spectrumfile '+str(HERE/'AM1_5G-1sun.spe'), 'action illumination.fromright', 'action light', 'action intensity.T 100', 'action spectrumcutoff.off']
            else:
                lines += ['action dark']
            if mode=='qe':
                lines += ['action qe.startlambda 300', 'action qe.stoplambda 1200', 'action qe.increment 5', 'action qe.checkaction 1']
            else:
                stop = round((1.2-.0015*temperature) if cell=='cigs' else (1.24-.001*temperature), 3)
                lines += ['action iv.startv -0.2', f'action iv.stopv {stop}', 'action iv.increment 0.005', 'action iv.checkaction 1', 'action iv.continueafterVoc']
            lines += ['calculate singleshot', f'save results.{"qe" if mode=="qe" else "iv"} '+str(HERE/(stem+('.qe' if mode=='qe' else '.iv'))), 'save settings.definitionfile '+str(HERE/(stem+'.def')), 'save settings.actionlistfile '+str(HERE/(stem+'.act'))]
(HERE/'series.script').write_text('\n'.join(lines)+'\n', encoding='cp1252')
assert len([x for x in lines if x=='calculate singleshot'])==34
print('Prepared 32 IV scans and 2 QE scans; example physics unchanged, external Rs/Gsh disabled.')
