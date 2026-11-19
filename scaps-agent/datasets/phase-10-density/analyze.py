"""Package exact SCAPS exports and check offset, recombination and pinning physics."""
from pathlib import Path
import csv, hashlib, importlib.util, json, shutil
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('gb',HERE.parent/'phase-GB/analyze.py')
gb=importlib.util.module_from_spec(spec); spec.loader.exec_module(gb)
KB=gb.KB_EV_K
Q=gb.Q
EPS0=8.8541878128e-12

def text(path): return path.read_text(encoding='cp1252')
def csvwrite(path,header,rows):
    with path.open('w',newline='',encoding='utf8') as f:
        w=csv.writer(f); w.writerow(header); w.writerows(rows)

def pv(rows):
    v=rows[:,0]; j=rows[:,1]*10
    crossing=np.flatnonzero((j[:-1]<0)&(j[1:]>=0))
    assert len(crossing)==1,'Voc must be bracketed'
    i=crossing[0]; voc=float(np.interp(0,j[i:i+2],v[i:i+2])); jsc=float(-np.interp(0,v,j))
    p=-v*j; k=int(np.argmax(p)); near=int(np.argmin(abs(v-voc)))
    return {'Voc_V':voc,'Jsc_A_m2':jsc,'FF':float(p[k]/(voc*jsc)),'Pmax_W_m2':float(p[k]),'Vmpp_V':float(v[k]),'interface_rec_near_Voc_A_m2':float(rows[near,5]*10),'bulk_rec_near_Voc_A_m2':float(rows[near,4]*10),'nearest_Voc_sample_V':float(v[near]),'interface_rec_at_0p6V_A_m2':float(np.interp(.6,v,rows[:,5])*10)}

def run():
    # Synthetic linear illuminated characteristic: Voc=1, Jsc=100, FF=1/4.
    test=np.zeros((201,11)); test[:,0]=np.linspace(0,1,201); test[:,1]=10*(test[:,0]-1)
    assert np.isclose(pv(test)['FF'],.25)
    members=json.loads((HERE/'manifest.json').read_text())
    assert len(members)==36
    summary={}; allmetrics={}
    fig,axes=plt.subplots(2,3,figsize=(14,8),constrained_layout=True)
    for group in ['offset','density','schottky']:
        out=HERE.parents[1]/'datasets'/('phase-10-'+group)
        out.mkdir(exist_ok=True)
        settings=[]; clean=[]; metrics=[]; bands=[]
        sources=['generate.py','analyze.py','manifest.json',group+'.script']
        if group!='schottky': sources+=['CdTe-base.abs','CdS-base.abs','AM1_5G-1sun.spe']
        for m in [x for x in members if x['group']==group]:
            stem=m['id']; suffix=m['measurement']
            definition=text(HERE/(stem+'.def')); act=text(HERE/(stem+'.act')); raw=text(HERE/(stem+'.'+suffix))
            layers=definition.split('\nlayer\n')[1:]
            assert len(layers)==2 and 'convergence' in definition and 'recalculate mesh' in definition
            assert gb.number(raw,'Temperature')==300 and gb.number(act,'Temperature')==300
            assert gb.number(act,'Frequency')==1e6 and gb.number(act,'Voltage')==0
            inp=text(HERE/(stem+'-input.def'))
            assert 'Series resistance : 0' in inp and 'Shunt conductance : 0' in inp
            assert 'phase-5' not in inp and 'phase-3' not in inp
            ebraw=text(HERE/(stem+'.eb')); eb=np.array(gb.numeric_rows(ebraw,23))
            assert len(eb)>30 and np.max(abs(eb[:,4:6]))<1e-6
            position=4 if group!='schottky' else 2
            pair=np.flatnonzero((abs(eb[:-1,1]-position)<1e-7)&(abs(eb[1:,1]-position)<1e-7))
            assert len(pair)==1,(stem,pair)
            k=pair[0]; metric=dict(m)
            if group!='schottky':
                offset=gb.number(layers[0],'chi')-gb.number(layers[1],'chi')
                actual=eb[k+1,3]-eb[k,3]
                assert abs(offset-m['conduction_band_offset_eV'])<1e-6 and abs(actual-offset)<2e-6
                if m['interface_density_m2']:
                    assert definition.count('interface recombination')==1
                    assert gb.number(definition.split('interface recombination')[1],'N')==m['interface_density_m2']
                else: assert 'interface recombination' not in definition
                rows=np.array(gb.numeric_rows(raw,11)); stop=m['voltage_stop_V']
                assert len(rows)==round((stop+.2)/.005)+1
                assert np.allclose(rows[[0,-1],0],[-.2,stop]) and 'extrapolated' not in raw
                assert 'Calculation under illumination' in raw and gb.number(raw,'Incident power on solar cell structure')==100
                metric.update(pv(rows)); metric.update({'offset_from_bands_eV':float(actual),'offset_over_kT':offset/(KB*300),'Boltzmann_factor_exp_minus_positive_offset_kT':float(np.exp(-max(offset,0)/(KB*300)))})
                assert 0<metric['FF']<1 and abs(metric['Voc_V']-gb.number(raw,'Voc ='))<.001
                for r in rows: clean.append([stem,300,r[0],*(r[1:]*10)])
                ax=axes[0,0 if group=='offset' else 1]
                ax.plot(rows[:,0],rows[:,1]*10,ls='-' if offset>=0 else '--',label=f'{offset:+.1f}eV' if group=='offset' else f'{m["interface_density_m2"]:.0e} @ {offset:+.1f}eV')
            else:
                contact=definition.split('\nfront contact\n')[1]
                assert gb.number(contact,'flatband')==0 and abs(gb.number(contact,'Fi_m')-m['metal_workfunction_eV'])<1e-9
                assert gb.number(layers[1],'d')==5e-10 and gb.number(layers[1],'eps')==1
                assert gb.number(layers[0],'chi')==4.05
                dit=m['interface_Dit_m2_eV']; n=definition.count('interface recombination')
                assert n==(1 if dit else 0),(stem,n)
                if dit:
                    section=definition.split('interface recombination')[1].split('\nlayer\n')[0]
                    assert section.count('type :')==2 and section.count(f'N : {dit:.3e}')==2,section
                    assert 'type : donor' in section and 'type : acceptor' in section
                    assert 'Et :  0.28' in section and 'Et :  0.84' in section
                    assert gb.number(definition,'defectdiscretisations')==51
                metric['barrier_semiconductor_eV']=float(eb[k,3]-eb[k,4])
                metric['barrier_metal_boundary_eV']=float(eb[-1,3]-eb[-1,4])
                metric['interlayer_drop_eV']=float(eb[-1,3]-eb[k+1,3])
                assert abs(metric['barrier_metal_boundary_eV']-(m['metal_workfunction_eV']-4.05))<2e-6
                rows=np.array(gb.numeric_rows(raw,12)); assert len(rows)==11
                assert np.allclose(np.sort(rows[:,0])[[0,-1]],[-.2,0]) and 'Calculation in dark' in raw
                metric['minimum_conductance_S_m2']=float(rows[:,2].min()*1e4)
                for r in rows: clean.append([stem,300,r[0],r[1]*1e-5,r[2]*1e4,r[3]*1e-6,r[4]*1e6,*(r[5:10]*10),r[10],r[11]])
            sources += [stem+'-input.def',stem+'.def',stem+'.act',stem+'.eb',stem+'.'+suffix]
            settings.append({**m,'temperature_K':300,'frequency_Hz':1e6,'workpoint_voltage_V':0,'definition_verbatim_SI_eV':definition,'actions_verbatim':act})
            metrics.append(metric)
            # Equilibrium bands are supporting simulation output, not a measured technique.
            for r in eb: bands.append([stem,r[1]*1e-6,*(r[3:7]*Q),*(r[7:12]*1e6),r[12]*100])
        if group=='schottky':
            fits=[]
            for dit in [0,1e17,1e18]:
                subset=[m for m in metrics if m['interface_Dit_m2_eV']==dit]
                x=np.array([m['metal_workfunction_eV'] for m in subset]); y=np.array([m['barrier_semiconductor_eV'] for m in subset])
                a,b=np.polyfit(x,y,1); theory=1/(1+Q*dit*5e-10/EPS0)
                ci=EPS0/5e-10; vn=KB*300*np.log(1e25/1e22)
                cd=np.sqrt(Q*EPS0*11.7*1e22/(2*(y.mean()-vn)))
                corrected=ci/(ci+Q*dit+cd)
                assert 0<a<=1.001
                fits.append({'Dit_m2_eV':dit,'S_scaps':float(a),'S_capacitance_model':theory,'S_difference':float(a-theory),'S_with_depletion_capacitance':float(corrected),'S_corrected_difference':float(a-corrected),'barrier_intercept_eV':float(b),'max_linear_residual_eV':float(max(abs(y-(a*x+b))))})
                axes[0,2].plot(x,y,'o-',label=f'Dit={dit:.0e}, S={a:.3f}')
            assert fits[0]['S_scaps']>.97 and fits[-1]['S_scaps']<.2
            assert all(abs(f['S_corrected_difference'])<.003 for f in fits)
            summary[group]={'fits':fits,'barrier_definition':'Ec-Fn on Si side of Si/interlayer interface at equilibrium; direct metal boundary remains Phi_m-chi.','model_limit':'5 angstrom classical transport surrogate, eps_r=1, Eg=3eV, chi=4.05eV; charge-free interlayer approximation, no microscopic metal-induced gap states or quantum tunneling calibration. CV is auxiliary and does not identify barrier independently.'}
            csvwrite(out/'pinning.csv',list(fits[0]),[list(m.values()) for m in fits])
        else:
            x=np.array([m['conduction_band_offset_eV'] if group=='offset' else m['interface_density_m2'] for m in metrics])
            ax=axes[1,0 if group=='offset' else 1]
            for subset in ([metrics] if group=='offset' else [[m for m in metrics if m['conduction_band_offset_eV']==o] for o in [.1,-.2]]):
                xx=[m['conduction_band_offset_eV'] if group=='offset' else m['interface_density_m2'] for m in subset]
                label='' if group=='offset' else f" {subset[0]['conduction_band_offset_eV']:+.1f}eV"
                ax.plot(xx,[m['Voc_V'] for m in subset],'o-',label='Voc'+label); ax.plot(xx,[m['FF'] for m in subset],'s-',label='FF'+label)
            ax.legend(fontsize=7)
            if group=='density':
                ax.set_xscale('symlog',linthresh=1e13)
                ax.set_xticks([0,1e13,1e14,1e15,1e16,1e17],['0','1e13','1e14','1e15','1e16','1e17'])
            summary[group]={'metrics':metrics}
        for source in sources: shutil.copyfile(HERE/source,out/source)
        header=['sample_id','temperature_K','voltage_V']+(['current_density_'+s+'_A_m-2' for s in ['total','recombination','generation','bulk','interface','minority_left','minority_right','SRH','radiative','Auger']] if group!='schottky' else ['capacitance_F_m-2','conductance_S_m-2','raw_W_m','raw_Napp_m-3','current_density_total_A_m-2','current_density_bulk_A_m-2','current_density_interface_A_m-2','current_density_opposite_n_A_m-2','current_density_opposite_p_A_m-2','epsilon_for_W_1','epsilon_for_N_1'])
        csvwrite(out/'data.csv',header,clean)
        csvwrite(out/'bands.csv',['sample_id','position_m','Ec_J','Fn_J','Fp_J','Ev_J','electron_density_m-3','hole_density_m-3','defect_charge_number_density_m-3','net_doping_m-3','net_charge_number_density_m-3','electric_field_V_m-1'],bands)
        params={'dataset_id':out.name,'scaps_version':'3.3.12','all_settings':settings,'temperature_K':300,'frequency_Hz':1e6,'voltage_range_V':'per member voltage_stop_V; start -0.2 V' if group!='schottky' else [-.2,0],'voltage_increment_V':.005 if group!='schottky' else .02,'illumination':'AM1.5G from right' if group!='schottky' else 'dark','incident_power_W_m2':1000 if group!='schottky' else 0,'external_Rs':'disabled','external_Gsh':'disabled','quality':'Exact saved definitions and actions; no mesh/step convergence study. Signed raw conductance retained, not certified as golden conductance. CSV in SI; metrics and native SCAPS parameters retain explicitly labelled eV.','sources':sources}
        if group=='schottky': params.update(summary[group])
        (out/'params.json').write_text(json.dumps(params,indent=2)+'\n')
        (out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
        hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name!='SHA256.json'}
        (out/'SHA256.json').write_text(json.dumps(hashes,indent=2)+'\n')
        for name,digest in hashes.items(): assert hashlib.sha256((out/name).read_bytes()).hexdigest()==digest
        with (out/'data.csv').open(newline='') as f:
            saved=list(csv.reader(f)); assert len(saved)==len(clean)+1 and all(len(r)==len(header) for r in saved)
        allmetrics[group]=metrics
        print(group,len(metrics),'scans',len(clean),'points')
    off=allmetrics['offset']; den=allmetrics['density']
    for offset in [.1,-.2]:
        subset=[m for m in den if m['conduction_band_offset_eV']==offset]
        assert all(b['Voc_V']<a['Voc_V'] for a,b in zip(subset,subset[1:]))
        assert all(b['interface_rec_at_0p6V_A_m2']>a['interface_rec_at_0p6V_A_m2'] for a,b in zip(subset,subset[1:]))
    p40=next(m for m in off if m['conduction_band_offset_eV']==.4)
    p60=next(m for m in off if m['conduction_band_offset_eV']==.6)
    assert p60['Jsc_A_m2']<p40['Jsc_A_m2']/2 and p60['FF']<p40['FF']-.3
    summary['physics']={'kT_eV':KB*300,'severe_blocking_offset_bracket_eV':[.4,.6],'severe_blocking_bracket_kT':[.4/(KB*300),.6/(KB*300)],'criterion':'Jsc falls by >50% and FF by >0.3 between adjacent sampled offsets; device-specific bracket, not a universal threshold.','cliff_Voc_loss_V':den[6]['Voc_V']-den[-1]['Voc_V'],'spike_Voc_loss_V':den[0]['Voc_V']-den[5]['Voc_V'],'point_count':sum(round((m.get('voltage_stop_V',0)+.2)/(.005 if m['measurement']=='iv' else .02))+1 for m in members)}
    axes[1,2].plot([m['conduction_band_offset_eV'] for m in off],[m['Jsc_A_m2'] for m in off],'o-')
    axes[1,2].set(xlabel='Conduction band offset (eV)',ylabel='Jsc (A/m2)')
    for ax in axes[0,:2]: ax.set(xlabel='Voltage (V)',ylabel='J (A/m2)',ylim=(-400,200)); ax.legend(fontsize=7)
    axes[0,2].set(xlabel='Metal work function (eV)',ylabel='Si-side barrier (eV)'); axes[0,2].legend(fontsize=7)
    axes[1,0].set(xlabel='Conduction band offset (eV)',ylabel='Voc (V), FF (1)')
    axes[1,1].set(xlabel='Interface density (m-2)',ylabel='Voc (V), FF (1)')
    fig.savefig(HERE/'analysis.png',dpi=160)
    (HERE/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('PASS: exact settings, SI, full ranges, PV interpolation, band offsets, pinning fits, hashes.')

if __name__=='__main__': run()
