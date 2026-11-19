"""Package SCAPS solar-cell series, extract local diode metrics and check JFM import."""
from pathlib import Path
import csv, hashlib, importlib.util, json, re, shutil, sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('gb', HERE.parent/'phase-GB/analyze.py')
gb = importlib.util.module_from_spec(spec); spec.loader.exec_module(gb)
sys.path.insert(0, 'D:/SJCM/01_SemiconductorStudio/SST-Prototypes/Prototypes/JFM/python')
from jfm_import import parse_dat, parse_scaps_iv
KB = gb.KB_EV_K

def text(path): return path.read_text(encoding='cp1252')

def write_csv(path, header, rows, delimiter=','):
    with path.open('w', newline='', encoding='utf8') as f:
        w = csv.writer(f, delimiter=delimiter); w.writerow(header); w.writerows(rows)

def diode(v, j, temperature):
    # shortcut: fixed local window gives effective parameters; use a joint physical fit for device identification.
    mask = (v>=.2)&(v<=.4)&(j>0)
    assert mask.sum()>=20
    a,b = np.polyfit(v[mask], np.log(j[mask]), 1)
    pred = a*v[mask]+b
    r2 = 1-np.sum((np.log(j[mask])-pred)**2)/np.sum((np.log(j[mask])-np.log(j[mask]).mean())**2)
    near = abs(v)<=.02
    g0 = np.polyfit(v[near], j[near], 1)[0]
    high = v>=v.max()-.05
    r_high = np.polyfit(j[high], v[high], 1)[0]
    return {'n_eff':float(1/(a*KB*temperature)), 'J0_eff_A_m2':float(np.exp(b)), 'lnJ_R2':float(r2), 'fit_voltage_range_V':[.2,.4], 'Rsh_slope_Ohm_m2':float(1/g0), 'Rsh_voltage_range_V':[-.02,.02], 'Rs_high_slope_Ohm_m2':float(r_high), 'Rs_voltage_range_V':[float(v[high].min()),float(v[high].max())], 'Rs_current_range_A_m2':[float(j[high].min()),float(j[high].max())], 'resistance_interpretation':'Differential device slopes, not identifiable external Rs/Rsh; upper-bias window may be contact limited.'}

def package(cell, mode):
    out = HERE.parents[1]/'datasets'/f'phase-9-{cell}-{mode}'
    out.mkdir(exist_ok=True)
    sources = [cell+'-input.def', cell+'-original.def', 'series.script', 'AM1_5G-1sun.spe', 'generate.py', 'analyze.py']
    if cell=='cdte': sources += ['CdTe-base.abs','CdS-base.abs','SnOx-base.abs','cdte-series.script']
    else: sources += ['cigs-acquisition.script']
    settings=[]; clean=[]; metrics=[]; manifest=[]
    for temperature in ([300] if mode=='qe' else range(200,341,20)):
        stem = f'{cell}-{mode}-T{temperature}'
        suffix = '.qe' if mode=='qe' else '.iv'
        raw = text(HERE/(stem+suffix)); definition = text(HERE/(stem+'.def')); act=text(HERE/(stem+'.act'))
        assert gb.number(raw,'Temperature')==temperature
        assert 'convergence' in definition and 'mesh' in definition
        assert gb.number(act,'Temperature')==temperature and gb.number(act,'Voltage')==0 and gb.number(act,'Frequency')==1e6
        assert 'Series resistance : 0' in text(HERE/(cell+'-input.def')) and 'Shunt conductance : 0' in text(HERE/(cell+'-input.def'))
        sources += [stem+suffix,stem+'.def',stem+'.act']
        settings.append({'temperature_K':temperature,'definition_verbatim_SI_eV':definition,'actions_verbatim':act})
        if mode=='qe':
            rows = np.array(gb.numeric_rows(raw, 3))
            assert len(rows)==181 and np.allclose(rows[[0,-1],0],[300,1200])
            assert np.all((rows[:,1]>=-1e-6)&(rows[:,1]<=100.001))
            assert np.allclose(rows[:,0]*rows[:,2],1239.9771,rtol=1e-5)
            clean.extend(np.column_stack([np.full(len(rows),temperature),rows[:,0]*1e-9,rows[:,1]/100]))
            continue
        rows = np.array(gb.numeric_rows(raw,11)); v=rows[:,0]; j=rows[:,1]*10
        stop = round((1.2-.0015*temperature) if cell=='cigs' else (1.24-.001*temperature),3)
        assert len(rows)==round((stop+.2)/.005)+1 and np.allclose(v[[0,-1]],[-.2,stop])
        assert ('Calculation under illumination' in raw)==(mode=='light')
        clean.extend(np.column_stack([np.full(len(v),temperature),v,rows[:,1:]*10]))
        if mode=='dark':
            metrics.append({'temperature_K':temperature,**diode(v,j,temperature)})
        else:
            assert 'extrapolated' not in raw and 'AM1_5G-1sun.spe' in raw
            assert gb.number(raw,'Incident power on solar cell structure')==100
            crossing=np.flatnonzero((j[:-1]<0)&(j[1:]>=0)); assert len(crossing)==1
            k=crossing[0]; voc=float(np.interp(0,j[k:k+2],v[k:k+2])); jsc=float(-np.interp(0,v,j))
            power=-v*j; pmax=float(power.max()); ff=pmax/(voc*jsc)
            assert abs(voc-gb.number(raw,'Voc ='))<.001 and 0<ff<1
            at=int(np.argmin(abs(v-voc)))
            metrics.append({'temperature_K':temperature,'Voc_V':voc,'Jsc_A_m2':jsc,'Pmax_W_m2':pmax,'FF':ff,'nearest_Voc_sample_voltage_V':float(v[at]),'bulk_rec_near_Voc_A_m2':float(rows[at,4]*10),'interface_rec_near_Voc_A_m2':float(rows[at,5]*10),'minority_left_near_Voc_A_m2':float(rows[at,6]*10),'minority_right_near_Voc_A_m2':float(rows[at,7]*10)})
        # JFM uses current, so choose and record a normalization area of exactly 1 cm2.
        jfm=out/'jfm'; jfm.mkdir(exist_ok=True)
        target=jfm/(stem+'K.tsv')
        write_csv(target,['voltage_V','current_A'],zip(v,j*1e-4),'\t')
        parsed=parse_dat(target)
        assert len(parsed['curves'])==1, parsed
        curve=parsed['curves'][0]
        units=parse_dat(target,{'column_units':['V','A']})['curves'][0]['columns']
        assert [(c['role'],c['unit']) for c in units]==[('voltage','V'),('current','A')]
        assert curve['metadata']['temperature_K']==temperature, curve['metadata']
        data=np.asarray(curve['data']); assert np.allclose(data[:,0],v) and np.allclose(data[:,1],j*1e-4)
        native=parse_scaps_iv(HERE/(stem+'.iv')); assert len(native['curves'])==1
        manifest.append({'file':str(target.relative_to(out)),'temperature_K':temperature,'area_m2':1e-4,'illumination':mode,'point_count':len(v),'voltage_range_V':[float(v[0]),float(v[-1])]})
    for source in sources: shutil.copyfile(HERE/source,out/source)
    header = ['temperature_K','wavelength_m','external_quantum_efficiency_1'] if mode=='qe' else ['temperature_K','voltage_V']+['current_density_'+s+'_A_m-2' for s in ['total','recombination','generation','bulk','interface','minority_left','minority_right','SRH','radiative','Auger']]
    write_csv(out/'data.csv',header,clean)
    assert np.allclose(np.loadtxt(out/'data.csv',delimiter=',',skiprows=1),clean)
    params={'dataset_id':out.name,'scaps_version':'3.3.12','temperatures_K':[x['temperature_K'] for x in settings], 'workpoint_voltage_V':0,'frequency_Hz':1e6,'illumination':'dark bias for QE' if mode=='qe' else mode,'illumination_side':'right','incident_power_density_W_m2':1000 if mode=='light' else 0,'voltage_range_V':None if mode=='qe' else [-.2,1.0 if cell=='cigs' else 1.25],'voltage_increment_V':None if mode=='qe' else .005,'external_Rs_Ohm_m2':0,'external_Gsh_S_m2':0,'external_Rsh':'disabled (infinite)','all_settings':settings,'jfm_normalization_area_m2':1e-4,'jfm_manifest':manifest,'quality':'Example-cell simulation; local diode slopes are effective, not independently set J0/n/Rs/Rsh. No mesh convergence study.','source_sha256':{s:hashlib.sha256((out/s).read_bytes()).hexdigest() for s in sources}}
    params['voltage_range_V'] = None if mode=='qe' else 'per scan; see jfm_manifest and all_settings'
    params['qe_wavelength_range_m'] = [300e-9,1200e-9] if mode=='qe' else None
    params['qe_wavelength_increment_m'] = 5e-9 if mode=='qe' else None
    params['optical_caveat'] = 'CdTe-base.abs has subgap alpha=100 m^-1; QE retains a nonzero tail. Absorption table edge differs from electrical Eg=1.5 eV.' if cell=='cdte' else 'CIGS uses the analytical sqrt(hv-Eg) absorption model.'
    (out/'params.json').write_text(json.dumps(params,indent=2)+'\n')
    if metrics: (out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
    for source,digest in params['source_sha256'].items(): assert hashlib.sha256((out/source).read_bytes()).hexdigest()==digest
    hashes={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='SHA256.json'}
    (out/'SHA256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    return np.array(clean),metrics

if __name__=='__main__':
    # Synthetic diode checks the extraction and SI resistance conversion.
    vv=np.arange(-.2,1.5001,.005); jj=1e-8*np.expm1(vv/(1.5*KB*300))
    test=diode(vv,jj,300); assert abs(test['n_eff']/1.5-1)<.002 and abs(test['J0_eff_A_m2']/1e-8-1)<.01
    fig,axes=plt.subplots(2,3,figsize=(14,8),constrained_layout=True); summary={}
    for i,cell in enumerate(['cigs','cdte']):
        dark,dm=package(cell,'dark'); light,lm=package(cell,'light'); qe,_=package(cell,'qe')
        t=np.array([m['temperature_K'] for m in lm]); voc=np.array([m['Voc_V'] for m in lm]); a,b=np.polyfit(t,voc,1)
        high=t>=280; ah,bh=np.polyfit(t[high],voc[high],1)
        definition=text(HERE/(cell+'-light-T300.def')); absorber=definition.split('\nlayer\n')[1]; eg=gb.number(absorber,'Eg')
        cutoff=1239.9771/eg
        last_positive=float(qe[qe[:,2]>1e-5,1].max()*1e9)
        edge_one_percent=float(qe[qe[:,2]>.01,1].max()*1e9)
        assert np.all(np.diff(voc)<0) and 0<b<2
        assert abs(edge_one_percent-cutoff)<30
        if cell=='cigs': assert np.all(qe[qe[:,1]*1e9>cutoff,2]==0)
        else: assert np.allclose(qe[qe[:,1]>1e-6,2],.0002887609)
        summary[cell]={'Eg_absorber_eV':eg,'Ea_Voc_intercept_200_340K_eV':float(b),'Ea_Voc_intercept_280_340K_eV':float(bh),'Voc_slope_V_K':float(a),'max_linear_residual_V':float(max(abs(voc-(a*t+b)))),'Ea_minus_Eg_eV':float(b-eg),'QE_bandgap_wavelength_nm':cutoff,'QE_last_above_1e-5_nm':last_positive,'QE_max_fraction':float(qe[:,2].max()),'dark_effective_parameters':dm,'light_metrics':lm}
        summary[cell]['QE_last_above_1percent_nm']=edge_one_percent
        summary[cell]['QE_at_1200nm_fraction']=float(qe[-1,2])
        if cell=='cdte':
            back=definition.split('\nback contact\n')[1].split('\nlayer\n')[0]
            summary[cell]['ideal_back_hole_barrier_eV']=gb.number(absorber,'chi')+eg-gb.number(back,'Fi_m')
        for temperature in range(200,341,20):
            d=dark[dark[:,0]==temperature]; l=light[light[:,0]==temperature]
            axes[i,0].plot(d[:,1],d[:,2],label=f'{temperature}K'); axes[i,0].plot(l[:,1],l[:,2],ls='--')
        axes[i,0].set(title=cell+' dark / light dashed',xlabel='Voltage (V)',ylabel='J (A/m2)',yscale='symlog',ylim=(-1000,1e5)); axes[i,0].legend(fontsize=6)
        axes[i,1].plot(t,voc,'o'); axes[i,1].plot([0,340],[b,b+a*340],'--'); axes[i,1].axhline(eg,color='gray',ls=':'); axes[i,1].set(xlabel='Temperature (K)',ylabel='Voc (V)',title=f'0 K intercept {b:.4f} V / Eg {eg:g} eV')
        axes[i,2].plot(qe[:,1]*1e9,qe[:,2]); axes[i,2].set(xlabel='Wavelength (nm)',ylabel='EQE (1)',title=cell+' QE, dark bias 0 V, 300 K')
    fig.savefig(HERE/'analysis.png',dpi=150)
    (HERE/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))
    print('PASS: six datasets, 34 scans, SI roundtrip, exact settings/hashes, synthetic diode and JFM Python parsers.')
