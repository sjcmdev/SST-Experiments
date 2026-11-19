"""Re-extract phase 2/3 activation energies; preserve inputs without rerunning SCAPS."""
from pathlib import Path
import csv
import hashlib
import json
import runpy
import shutil

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT/'datasets/phase-6-activation-energy'
P2 = runpy.run_path(str(HERE.parent/'phase-2/analyze.py'))
P3 = runpy.run_path(str(HERE.parent/'phase-3/analyze.py'))
KB = P2['KB']
Q = P2['Q']


def arrhenius(t, rate, power):
    t, rate = np.asarray(t), np.asarray(rate)
    assert len(t) >= 3 and len(np.unique(t)) == len(t)
    assert np.all(t > 0) and np.all(rate > 0) and np.all(np.isfinite(rate))
    x, y = 1/t, np.log(rate/t**power)
    slope, intercept = np.polyfit(x, y, 1)
    return dict(energy_eV=float(-KB*slope), energy_J=float(-KB*slope*Q),
                R_squared=float(1-np.sum((y-slope*x-intercept)**2)/np.sum((y-y.mean())**2)),
                slope_K=float(slope), log_intercept=float(intercept), temperature_power=power)


def preserve(name, clean):
    source = ROOT/'datasets'/name
    params = json.loads((source/'params.json').read_text(encoding='utf8'))
    assert params['scaps_version'] == '3.3.12'
    for filename, digest in params['sha256'].items():
        assert hashlib.sha256((source/filename).read_bytes()).hexdigest() == digest, (name, filename)
    dest = OUT/'sources'/name
    dest.mkdir(parents=True, exist_ok=True)
    for filename in list(params['sha256'])+['params.json', clean]:
        shutil.copy2(source/filename, dest/filename)
    return params


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    base = 'phase-2-temperature-series'
    p = preserve(base, 'iv.csv')
    source = OUT/'sources'/base
    assert (source/'temperature-series.iv').read_bytes() == (HERE.parent/'phase-2/temperature-series.iv').read_bytes()
    definition = p['device_and_numerical_settings_verbatim']
    barrier = P3['number'](definition, 'Fi_m')-P3['number'](definition, 'chi')
    assert abs(barrier-.75) < 1e-12
    curves = P2['curves']('temperature-series.iv')
    assert [t for t, _ in curves] == list(range(200, 361, 20))
    with (source/'iv.csv').open(encoding='utf8') as f:
        clean_iv = list(csv.DictReader(f))
    np.testing.assert_allclose([[float(r[k]) for k in ('temperature_K', 'voltage_left_minus_right_V', 'current_density_into_left_A_m2')] for r in clean_iv],
                               [[t, r[0], 10*r[1]] for t, a in curves for r in a], rtol=1e-14)
    assert all(r['quality']=='UNRESOLVED_REVERSE_LOW_T' for r in clean_iv if float(r['temperature_K'])<=220 and float(r['voltage_left_minus_right_V'])>0)
    fits = [P2['diode_fit'](a, t) for t, a in curves]
    assert all(f['max_relative_fit_error'] < .02 for f in fits)
    t_iv = np.array([f['temperature_K'] for f in fits])
    j0 = np.array([f['J0_A_m2'] for f in fits])
    rich = arrhenius(t_iv, j0, 2)
    rich['A_eff_A_m2_K2'] = float(np.exp(rich['log_intercept']))
    windows = {f'{lo}-{hi}_V': P2['richardson']([P2['diode_fit'](a,t,lo,hi) for t,a in curves])
               for lo,hi in ((.08,.16),(.16,.24))}
    admittance = []
    for t in (240,270,300,330,360):
        stem = f'cf-defect-T{t}'
        name = 'phase-3-'+stem
        params = preserve(name, 'clean.csv')
        folder = OUT/'sources'/name
        raw = (folder/(stem+'.cf')).read_text(encoding='cp1252')
        definition = params['all_device_contact_defect_numerical_parameters_verbatim']
        assert 'type : acceptor' in definition and 'Reference for defect energy :  2' in definition
        level = P3['number'](definition, 'Et')
        assert level == .4 and P3['number'](definition,'Nt(uniform)') == 1e21
        assert P3['number'](definition,'sigma_n') == P3['number'](definition,'sigma_p') == 1e-19
        assert P3['number'](raw,'Temperature') == params['temperature_K'] == t
        assert P3['number'](raw,'Workpoint bias voltage') == 0 and 'Calculation in dark' in raw
        a = np.array(P3['rows'](raw,7))
        assert a.shape == (211,7) and np.all(np.isfinite(a))
        np.testing.assert_allclose(a[:,0],np.logspace(0,7,211),rtol=5e-9)
        clean = np.loadtxt(folder/'clean.csv',delimiter=',',skiprows=1)
        np.testing.assert_allclose(clean,a[:,:3]*[1,1e-5,1e4],rtol=1e-14)
        np.testing.assert_allclose(1/(clean[:,2]+2j*np.pi*clean[:,0]*clean[:,1]),(a[:,3]+1j*a[:,4])*1e-4,rtol=1e-7)
        debye, error = P3['step_fit'](a)
        admittance.append(dict(temperature_K=t,inflection_frequency_Hz=P3['inflection'](a),
                               local_Debye_frequency_Hz=debye,local_fit_max_error_nF_cm2=error))
    t_cf = np.array([r['temperature_K'] for r in admittance])
    finf = np.array([r['inflection_frequency_Hz'] for r in admittance])
    fdebye = np.array([r['local_Debye_frequency_Hz'] for r in admittance])
    inflection = arrhenius(t_cf,2*np.pi*finf,2)
    debye = arrhenius(t_cf,2*np.pi*fdebye,2)
    summary = dict(barrier_set_eV=barrier,defect_depth_below_Ec_set_eV=level,
                   richardson=rich,IV_window_sensitivity=windows,admittance_inflection=inflection,
                   admittance_local_Debye=debye,
                   uncorrected_ln_J0=arrhenius(t_iv,j0,0),uncorrected_ln_omega=arrhenius(t_cf,2*np.pi*finf,0),
                   IV_fits=fits,admittance=admittance)
    assert abs(rich['energy_eV']-barrier)<.01 and rich['R_squared']>.999
    assert abs(inflection['energy_eV']-level)<.01 and abs(debye['energy_eV']-level)<.005
    # Synthetic curves check extraction without relying on SCAPS reference energies.
    ts = np.array([240.,270.,300.,330.,360.])
    assert abs(arrhenius(ts,1e9*ts**2*np.exp(-.4/(KB*ts)),2)['energy_eV']-.4)<1e-12
    fs = np.logspace(0,7,211)
    synthetic = np.column_stack((fs,30+3/(1+(fs/3000)**2)))
    assert abs(P3['step_fit'](synthetic)[0]/3000-1)<1e-6
    assert abs(P3['inflection'](synthetic)/3000-1)<.003
    v = np.linspace(-.4,.2,121)
    exact = P2['diode_fit'](np.column_stack((v,-2e-6*np.expm1(-v/(1.1*KB*300))/10)),300)
    assert abs(exact['J0_A_m2']/2e-6-1)<1e-7 and abs(exact['ideality_factor']-1.1)<1e-7
    for filename, records in [('iv-fit.csv',fits),('admittance-fit.csv',admittance)]:
        P2['write_csv'](OUT/filename,list(records[0]),[list(r.values()) for r in records])
        np.testing.assert_allclose(np.loadtxt(OUT/filename,delimiter=',',skiprows=1),[list(r.values()) for r in records],rtol=1e-14)
    (HERE/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False),encoding='utf8')
    shutil.copy2(HERE/'summary.json',OUT/'summary.json')
    shutil.copy2(HERE/'analyze.py',OUT/'analyze.py')
    metadata = dict(scaps_version='3.3.12',kind='derived analysis; no new simulations',
                    kB_eV_K=KB,elementary_charge_C=Q,source_parameters='sources/*/params.json, verbatim .def/.act',
                    IV_forward_fit_window_V=[.12,.24],IV_temperature_K=t_iv.tolist(),CF_temperature_K=t_cf.tolist(),
                    formulas=['Jforward=J0*expm1(Vforward/(n*kB*T))','ln(J0/T^2)=b-phi_eff/(kB*T)',
                              'ln(2*pi*f/T^2)=b-Ea/(kB*T)'],
                    limitations=['Low-T reverse IV excluded.','Local Debye is an approximate device-response fit.',
                                 'Sensitivity spans are not confidence intervals.','CODATA kB used; SCAPS constants not independently established.',
                                 'Prior scripted exports had abnormal process exits; raw and provenance checked here.'],
                    helper_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in [HERE.parent/'phase-2/analyze.py',HERE.parent/'phase-3/analyze.py',HERE.parent/'phase-GB/analyze.py']},
                    sha256={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in sorted(OUT.rglob('*')) if p.is_file() and p != OUT/'params.json'})
    (OUT/'params.json').write_text(json.dumps(metadata,indent=2,allow_nan=False),encoding='utf8')
    for name,digest in json.loads((OUT/'params.json').read_text())['sha256'].items():
        assert hashlib.sha256((OUT/name).read_bytes()).hexdigest()==digest
    fig,axes = plt.subplots(1,2,figsize=(11,4.5))
    for ax,t,rate,fit,label in [(axes[0],t_iv,j0,rich,'IV: J0/T²'),(axes[1],t_cf,2*np.pi*finf,inflection,'C-f: inflection')]:
        x=1000/t; y=np.log(rate/t**2)
        ax.plot(x,y,'o',label=label)
        ax.plot(x,fit['slope_K']*x/1000+fit['log_intercept'],label=f"Ea={fit['energy_eV']:.6f} eV")
        ax.set_xlabel('1000/T (K⁻¹)'); ax.grid(alpha=.3)
    axes[0].set_ylabel('ln[(J0/T²)/(A m⁻² K⁻²)]')
    axes[1].plot(1000/t_cf,np.log(2*np.pi*fdebye/t_cf**2),'s',label=f"Local Debye: {debye['energy_eV']:.6f} eV")
    axes[1].set_ylabel('ln[(2πf/T²)/(s⁻¹ K⁻²)]')
    for ax in axes: ax.legend(fontsize=8)
    fig.tight_layout(); fig.savefig(HERE/'analysis.png',dpi=160); plt.close(fig)
    print(json.dumps({k:v for k,v in summary.items() if k not in ('IV_fits','admittance')},indent=2))
    print('PASS: six source datasets, hashes, raw/SI identity, settings, synthetic extraction, CSV/JSON and physics bounds.')


if __name__ == '__main__':
    main()
