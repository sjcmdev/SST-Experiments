"""Package phase-2 files, fit forward diodes and check saved SI datasets."""
from pathlib import Path
import csv
import hashlib
import json
import math
import re
import runpy
import shutil

import numpy as np
from scipy.optimize import least_squares

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
KB = 8.617333262145e-5  # eV/K
Q = 1.602176634e-19
numeric_rows = runpy.run_path(str(HERE.parent / 'phase-GB/analyze.py'))['numeric_rows']


def read(name):
    return (HERE / name).read_text(encoding='cp1252')


def curves(name):
    result = []
    for block in re.split(r'(?=SCAPS 3\.3\.12 ELIS-UGent: Version)', read(name))[1:]:
        t = float(re.search(r'^Temperature\s+([\d.]+)', block, re.M)[1])
        assert 'Calculation in dark' in block and 'voltage V applied to left contact' in block
        assert float(re.search(r'^Frequency\s+([\d.eE+\-]+)', block, re.M)[1]) == 1e6
        rows = np.array(numeric_rows(block, 11))
        assert rows.shape == (121, 11), (name, t, rows.shape)
        np.testing.assert_allclose(rows[:, 0], np.linspace(-.4, .2, 121), atol=1e-8)
        assert rows[0, 1] < 0 < rows[-1, 1] and rows[80, 1] == 0
        result.append((t, rows))
    return result


def diode_fit(rows, t, low=.12, high=.24):
    v = -rows[:, 0]
    select = (v >= low-1e-9) & (v <= high+1e-9)
    v, j = v[select], -10*rows[select, 1]
    assert len(v) >= 10 and np.all(j > 0)
    def residual(p):
        return p[0] + np.log(np.expm1(v/(p[1]*KB*t))) - np.log(j)
    fit = least_squares(residual, [-5, 1.04], bounds=([-100, .5], [30, 3]))
    assert fit.success
    error = residual(fit.x)
    return {'temperature_K': t, 'J0_A_m2': math.exp(fit.x[0]), 'ideality_factor': fit.x[1],
            'max_relative_fit_error': float(np.max(np.abs(np.expm1(error)))),
            'R_squared_logJ': float(1-np.sum(error**2)/np.sum((np.log(j)-np.mean(np.log(j)))**2))}


def richardson(fits):
    x = np.array([1/f['temperature_K'] for f in fits])
    y = np.array([math.log(f['J0_A_m2']/f['temperature_K']**2) for f in fits])
    a, b = np.polyfit(x, y, 1)
    return {'barrier_eff_eV': float(-a*KB), 'A_eff_A_m2_K2': float(math.exp(b)),
            'A_eff_A_cm2_K2': float(math.exp(b)/1e4),
            'R_squared': float(1-np.sum((y-a*x-b)**2)/np.sum((y-np.mean(y))**2))}


def write_csv(path, headers, rows):
    with path.open('w', newline='', encoding='utf8') as stream:
        writer = csv.writer(stream)
        writer.writerow(headers)
        writer.writerows(rows)


def package(stem, members):
    out = ROOT / 'datasets' / ('phase-2-'+stem)
    out.mkdir(parents=True, exist_ok=True)
    sources = [stem+'.iv', stem+'.act', stem+'.def', 'n-schottky-input.def', 'series.script', 'analyze.py']
    if stem == 'temperature-series':
        sources += ['temperature.sbf', 'equilibrium.script'] + [f'equilibrium-T{t}.eb' for t in (200, 300, 360)]
    for name in sources:
        shutil.copyfile(HERE/name, out/name)
    # Keep every numerical, grading and contact setting verbatim; batch T overrides are explicit below.
    params = {'scaps_version': '3.3.12', 'quality': 'FORWARD_VALIDATED_REVERSE_LOW_T_UNRESOLVED',
              'method': 'script; manual attempts did not change the GUI',
              'illumination': 'dark', 'frequency_Hz': 1e6, 'workpoint_voltage_V': 0,
              'voltage_convention': 'left minus right', 'current_convention': 'positive entering left',
              'voltage_range_V': [-.4, .2], 'voltage_step_V': .005, 'points_per_curve': 121,
              'device_and_numerical_settings_verbatim': read(stem+'.def'),
              'actions_verbatim': read(stem+'.act'),
              'batch_temperature_override_K': [t for t, _ in members],
              'members': [{'id': f'T{int(t)}K', 'temperature_K': t} for t, _ in members],
              'fit': {'forward_voltage_range_V': [.12, .24], 'objective': 'log current residual',
                      'model': 'abs(J)=J0*expm1(Vforward/(n*kB*T/q))'},
              'limitations': ['Reverse current at 200 and 220 K is unresolved; excluded from extraction.',
                             'Series process exited 0xC0000409 after exports; files independently checked.',
                             'A_eff is an apparent transport-dependent intercept, not an intrinsic material constant.'],
              'sha256': {name: hashlib.sha256((out/name).read_bytes()).hexdigest() for name in sources}}
    (out/'params.json').write_text(json.dumps(params, indent=2), encoding='utf8')
    header = ['temperature_K', 'voltage_left_minus_right_V', 'current_density_into_left_A_m2', 'quality']
    data = [[t, r[0], 10*r[1]] for t, rows in members for r in rows]
    qualities = ['UNRESOLVED_REVERSE_LOW_T' if t <= 220 and v > 0 else
                 'FORWARD_VALIDATED' if v < 0 else 'NOT_USED_IN_FIT' for t, v, _ in data]
    write_csv(out/'iv.csv', header, [r+[quality] for r, quality in zip(data, qualities)])
    persisted = list(csv.reader((out/'iv.csv').open(encoding='utf8')))
    assert persisted[0] == header
    np.testing.assert_allclose(np.array([r[:3] for r in persisted[1:]], dtype=float), data, rtol=1e-14)
    assert [r[3] for r in persisted[1:]] == qualities
    loaded = json.loads((out/'params.json').read_text())
    for name, digest in loaded['sha256'].items():
        assert hashlib.sha256((out/name).read_bytes()).hexdigest() == digest
    return str(out.relative_to(ROOT))


def main():
    series = curves('temperature-series.iv')
    assert [t for t, _ in series] == list(range(200, 361, 20))
    datasets = [package('temperature-series', series)]
    pilot_errors = {}
    for t in (200, 360):
        pilot = curves(f'pilot-T{t}.iv')
        assert len(pilot) == 1 and pilot[0][0] == t
        batch = next(r for temp, r in series if temp == t)
        delta = float(np.max(np.abs((pilot[0][1][:, 1]-batch[:, 1])*10)))
        # Reverse low-current values are unstable; only forward data support the extraction.
        forward = batch[:, 0] < 0
        np.testing.assert_allclose(pilot[0][1][forward, 1], batch[forward, 1], rtol=2e-6, atol=1e-12)
        pilot_errors[str(t)] = {'all_biases_A_m2': delta,
                               'forward_A_m2': float(np.max(np.abs((pilot[0][1][forward, 1]-batch[forward, 1])*10)))}
        datasets.append(package(f'pilot-T{t}', pilot))
    fits = [diode_fit(rows, t) for t, rows in series]
    assert all(.95 < f['ideality_factor'] < 1.1 and f['max_relative_fit_error'] < .02 for f in fits)
    rich = richardson(fits)
    assert abs(rich['barrier_eff_eV']-.75) < .02 and rich['R_squared'] > .999
    sensitivity = {f'{low}-{high}_V': richardson([diode_fit(r, t, low, high) for t, r in series])
                   for low, high in ((.08, .16), (.16, .24))}
    bands = []
    for t in (200, 300, 360):
        text = read(f'equilibrium-T{t}.eb')
        assert float(re.search(r'Band diagramme at actual bias voltage\s+([\d.eE+\-]+)', text)[1]) == 0
        r = np.array(numeric_rows(text.split('\nbulk\n')[1].split('\ncontacts\n')[0], 23))
        barrier = float(r[-1, 3]-r[-1, 4])
        assert abs(barrier-.75) < 1e-7
        integral = np.trapezoid(1/(.005*r[:, 7]*1e6), r[:, 1]*1e-6)
        contact = 1/(1e3*r[0, 7]*1e6)+1/(1e5*r[-1, 7]*1e6)
        bands.append({'temperature_K': t, 'barrier_eV': barrier,
                      'contact_boundary_J0_A_m2': Q*1e5*r[-1, 7]*1e6,
                      'linear_transport_J0_A_m2': float(Q/(integral/(KB*t)+contact))})
        write_csv(HERE/f'equilibrium-T{t}.csv', ['position_m', 'Ec_J', 'Fn_J', 'Fp_J', 'Ev_J',
                  'electron_density_m3', 'hole_density_m3', 'electric_field_V_m'],
                  [[a[1]*1e-6, *[a[i]*Q for i in (3,4,5,6)], a[7]*1e6, a[8]*1e6, a[12]*100] for a in r])
        shutil.copyfile(HERE/f'equilibrium-T{t}.csv', ROOT/datasets[0]/f'equilibrium-T{t}.csv')
    # Assert extraction against an exact synthetic diode and a known Richardson law.
    v = np.linspace(-.4, .2, 121)
    synthetic = np.column_stack((v, -2e-6*np.expm1(-v/(1.1*KB*300))/10))
    exact = diode_fit(synthetic, 300)
    assert abs(exact['ideality_factor']-1.1) < 1e-7 and abs(exact['J0_A_m2']/2e-6-1) < 1e-7
    exact_rich = richardson([{'temperature_K': t, 'J0_A_m2': 1.2e6*t*t*math.exp(-.75/(KB*t))} for t in range(200,361,20)])
    assert abs(exact_rich['barrier_eff_eV']-.75) < 1e-10
    assert abs(exact_rich['A_eff_A_m2_K2']/1.2e6-1) < 1e-10
    summary = {'fits': fits, 'richardson': rich, 'fit_window_sensitivity': sensitivity,
               'equilibrium': bands, 'pilot_batch_max_delta_J_A_m2': pilot_errors, 'datasets': datasets}
    (HERE/'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf8')
    write_csv(HERE/'fit.csv', list(fits[0]), [list(f.values()) for f in fits])
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 3, figsize=(14, 4))
    for t, r in series:
        axes[0].semilogy(-r[r[:,0]<0,0], -10*r[r[:,0]<0,1], label=f'{t:g} K')
    axes[0].set(xlabel='Forward voltage (V)', ylabel='|J| (A/m²)', title='Dark forward I–V(T)')
    axes[0].axvspan(.12,.24,alpha=.1,color='black')
    axes[0].legend(fontsize=7, ncol=2)
    x = np.array([1000/f['temperature_K'] for f in fits])
    y = np.array([math.log(f['J0_A_m2']/f['temperature_K']**2) for f in fits])
    axes[1].plot(x,y,'o'); axes[1].plot(x,np.polyval(np.polyfit(x,y,1),x))
    axes[1].set(xlabel='1000/T (K⁻¹)', ylabel='ln[(J₀/T²)/(A m⁻² K⁻²)]', title=f"φeff={rich['barrier_eff_eV']:.5f} eV; Aeff={rich['A_eff_A_cm2_K2']:.2f} A cm⁻² K⁻²")
    axes[2].plot([f['temperature_K'] for f in fits], [f['ideality_factor'] for f in fits],'o-')
    axes[2].axhline(1,color='grey',linestyle='--'); axes[2].set(xlabel='Temperature (K)',ylabel='Ideality factor n',title='Forward fit, 0.12–0.24 V')
    for ax in axes: ax.grid(alpha=.25)
    fig.tight_layout(); fig.savefig(HERE/'analysis.png',dpi=170); plt.close(fig)
    print(json.dumps(summary, indent=2))
    print('PASS: 9×121 points, two pilots, SI round-trip/hash, barriers, synthetic extraction and fit bounds.')


if __name__ == '__main__':
    main()
