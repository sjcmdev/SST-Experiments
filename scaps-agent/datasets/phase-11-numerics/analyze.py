"""Package robustness oracles with quality flags; never equate completion with accuracy."""
from pathlib import Path
import csv, hashlib, importlib.util, json, re, shutil, subprocess, sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
spec = importlib.util.spec_from_file_location('gb',HERE.parent/'phase-GB/analyze.py')
gb = importlib.util.module_from_spec(spec); spec.loader.exec_module(gb)
OUT = ROOT/'datasets/phase-11-numerics'

def text(path): return path.read_text(encoding='cp1252')
def rows(path,count): return np.asarray(gb.numeric_rows(text(path),count))
def number(t,key):
    match = re.search(r'^'+re.escape(key)+r'\s*:?\s*([-+\d.eE]+)',t,re.M)
    assert match, key
    return float(match[1])
def write_csv(path,header,data):
    with path.open('w',newline='',encoding='utf8') as f:
        w=csv.writer(f); w.writerow(header); w.writerows(data)

def budget_flags(a):
    error = abs(a[:,1]-a[:,2])*10
    # shortcut: mixed absolute/relative oracle gate, calibrate against Core error budgets before acceptance use.
    return error, error > (1e-8+.01*abs(a[:,1]*10))

def run():
    sample=np.zeros((2,11));sample[:,1]=[0,1];sample[:,2]=[0,1.02]
    assert budget_flags(sample)[1].tolist()==[False,True]
    cases=json.loads((HERE/'manifest.json').read_text())
    OUT.mkdir(exist_ok=True)
    summary={}; total=0
    fig,axes=plt.subplots(2,2,figsize=(12,8),constrained_layout=True)
    for c in cases:
        name=c['id']; folder=OUT/name; folder.mkdir(exist_ok=True)
        stems=[name] if name!='forward' else ['forward-logged','forward-safe','forward-fine','forward-clamp','forward-fixed']
        if name in ('doping','low80'): stems += [name+'-tight']
        members=[]
        for stem in stems:
            a=rows(HERE/(stem+'.iv'),11); e=rows(HERE/(stem+'-final.eb'),23)
            assert len(a)>2 and np.isfinite(a).all() and np.isfinite(e).all()
            definition=text(HERE/(stem+'.def')); finaldef=text(HERE/(stem+'-final.def'))
            act=text(HERE/(stem+'.act')); ebtext=text(HERE/(stem+'-final.eb'))
            assert number(act,'Temperature')==c['temperature_K']
            assert 'Calculation in dark' in text(HERE/(stem+'.iv'))
            assert number(act,'IV_startV')==0 and abs(a[0,0])<1e-12
            step=number(act,'IV_increment'); stop=number(act,'IV_stopV')
            assert np.allclose(np.diff(a[:,0]),step,atol=1e-6)
            assert 'convergence' in definition and 'Series resistance : 0' in text(HERE/(stem+'-input.def'))
            layers=definition.split('\nlayer\n')[1:]
            assert len(layers)==2
            scale=c.get('thickness_scale',1)
            assert abs(number(layers[0],'d')-5e-6*scale)<1e-12
            assert number(layers[0],'Na(uniform)')==c.get('doping_m3',1e22)
            assert number(layers[1],'Nd(uniform)')==c.get('doping_m3',1e24)
            assert number(layers[0],'Nt(uniform)')==c.get('trap_density_m3',1e20)
            error,bad=budget_flags(a)
            actual=number(ebtext,'Band diagramme at actual bias voltage')
            spatial=float(np.ptp(e[:,17])*10)
            metric=dict(id=stem,points=len(a),requested_stop_V=stop,last_exported_voltage_V=float(a[-1,0]),profile_voltage_V=actual,profile_is_last_IV_point=bool(abs(actual-a[-1,0])<1e-6),scan_complete=bool(abs(a[-1,0]-stop)<1e-6),J_final_A_m2=float(a[-1,1]*10),budget_error_final_A_m2=float(error[-1]),budget_relative_final=float(error[-1]/max(abs(a[-1,1]*10),1e-300)),first_budget_bad_voltage_V=float(a[bad,0][0]) if bad.any() else None,profile_current_spread_A_m2=spatial,profile_current_spread_relative_to_last_IV=float(spatial/max(abs(a[-1,1]*10),1e-300)),mesh_nodes=len(e),flags=[])
            if not metric['scan_complete']: metric['flags'].append('SCAN_TRUNCATED')
            if bad.any(): metric['flags'].append('CURRENT_BUDGET_UNRELIABLE')
            if not metric['profile_is_last_IV_point']: metric['flags'].append('PROFILE_AT_FAILED_BIAS')
            if spatial>1e-8+.01*abs(a[-1,1]*10): metric['flags'].append('PROFILE_CURRENT_NONUNIFORM')
            if c['temperature_K']<100: metric['flags'].append('LOW_T_TERMINAL_CURRENT_UNRESOLVED')
            if name=='doping': metric['flags'].append('DEGENERACY_AND_TUNNELING_NOT_VALIDATED')
            clean=np.column_stack((a[:,0],a[:,1:]*10,error,bad.astype(int)))
            write_csv(folder/(stem+'.csv'),['voltage_V','J_A_m2','J_total_rec_A_m2','J_total_gen_A_m2','J_bulk_A_m2','J_interface_A_m2','J_minor_left_A_m2','J_minor_right_A_m2','J_SRH_A_m2','J_radiative_A_m2','J_Auger_A_m2','current_budget_error_A_m2','current_budget_bad'],clean)
            bands=np.column_stack((e[:,1]*1e-6,e[:,3:7]*gb.Q,e[:,7:12]*1e6,e[:,12]*100,e[:,13:18]*10))
            write_csv(folder/(stem+'-profile.csv'),['x_m','Ec_J','Fn_J','Fp_J','Ev_J','n_m3','p_m3','rho_defect_m3','net_doping_m3','rho_m3','electric_field_V_m','Jn_A_m2','Jp_A_m2','Jn_tunnel_A_m2','Jp_tunnel_A_m2','Jtotal_A_m2'],bands)
            sources=[stem+suffix for suffix in ['-input.def','.def','-final.def','.act','.script','.iv','-final.eb']]
            if (HERE/(name+'-equilibrium.eb')).exists(): sources += [name+'-equilibrium.eb']
            for f in sources: shutil.copyfile(HERE/f,folder/f)
            members.append(dict(**metric,temperature_K=c['temperature_K'],frequency_Hz=number(act,'Frequency'),voltage_step_V=step,device_and_numerics_definition_native=text(HERE/(stem+'-input.def')),saved_definition_native=definition,post_calculation_definition_native=finaldef,action_list_native=act,definition_parameters=[dict(label=line.split(':',1)[0].strip(),value_native=line.split(':',1)[1].strip()) for line in definition.splitlines() if ':' in line],sha256={f:hashlib.sha256((HERE/f).read_bytes()).hexdigest() for f in sources}))
            summary[stem]=metric; total+=len(a)
            if stem in [name,'forward-safe']:
                axes[0,0].semilogy(a[:,0],np.maximum(abs(a[:,1]*10),1e-80),label=stem)
                axes[0,1].semilogy(a[:,0],np.maximum(error,1e-80),label=stem)
            if name in ['baseline','doping','low80'] and stem==name:
                axes[1,0].plot(e[:,1],(e[:,17]*10-a[-1,1]*10)/max(abs(a[-1,1]*10),1e-8),label=stem)
        (folder/'params.json').write_text(json.dumps(dict(schema_version=1,scaps_version='3.3.12',case=c,members=members,quality_gate='abs(J-Jrec) > 1e-8 A/m2 + 1% abs(J); spatial current uses same gate; these are robustness probes, not universal acceptance thresholds',profile_warning='After a failed IV scan the profile is at the attempted failed bias. Read profile_voltage_V.'),indent=2,allow_nan=False)+'\n')
        shutil.copyfile(HERE/'Si.abs',folder/'Si.abs')
    # Non-degenerate constant-gap homojunction check, using the SCAPS compatibility k/q.
    vt=1.38e-23/1.602e-19*300
    ni2=2.8e25*1.04e25*np.exp(-1.12/vt)
    expected=vt*np.log(1e22*1e24/ni2)
    eb=rows(HERE/'baseline-equilibrium.eb',23)
    actual=float(eb[0,3]-eb[-1,3])
    assert abs(expected-actual)<.002
    summary['physics']=dict(Vbi_theory_V=float(expected),Vbi_SCAPS_V=actual,Vbi_diff_V=actual-float(expected),baseline_Eg_eV=float(np.median(eb[:,3]-eb[:,6])))
    a=rows(HERE/'baseline.iv',11); use=(a[:,0]>=.3)&(a[:,0]<=.5)
    slope=np.polyfit(a[use,0],np.log(a[use,1]),1)[0]
    summary['physics']['ideality_0p3_to_0p5V']=float(1/(slope*vt))
    assert .9<summary['physics']['ideality_0p3_to_0p5V']<1.2
    assert abs(summary['physics']['baseline_Eg_eV']-1.12)<1e-6
    for name in ['baseline','thin','low80']:
        core=HERE/('core-'+name)/'iv.csv'
        if not core.exists(): continue
        tools=Path('D:/SJCM/01_SemiconductorStudio/scaps_core/scaps_solver_validation_tools')
        subprocess.run([sys.executable,str(tools/'normalize_solver_output.py'),'--input',str(core.parent),'--output',str(HERE/(name+'-core.npz'))],check=True)
        with np.load(HERE/(name+'-core.npz')) as z: data={k:z[k] for k in z.files}
        scaps=rows(HERE/(name+'.iv'),11)
        assert np.allclose(data['voltage_V'],scaps[:,0])
        ok=data['converged']
        assert ok.dtype==np.bool_ and ok.all(), 'Core convergence must be checked explicitly'
        mask=ok & (scaps[:,0]>=.1)
        j=data['current_density_A_m2']; ref=scaps[:,1]*10
        metric=dict(converged_points=int(ok.sum()),points=len(ok),max_abs_difference_A_m2=float(np.max(abs(j[mask]-ref[mask]))) if mask.any() else None,J_core_at_0p6V_A_m2=float(j[-1]) if ok[-1] else None,J_SCAPS_at_0p6V_A_m2=float(ref[-1]),relative_difference_at_0p6V=float((j[-1]-ref[-1])/ref[-1]) if ok[-1] else None)
        metric['J_SCAPS_recombination_at_0p6V_A_m2']=float(scaps[-1,2]*10)
        metric['Core_relative_difference_to_SCAPS_recombination_at_0p6V']=float((j[-1]-scaps[-1,2]*10)/(scaps[-1,2]*10))
        summary['core-'+name]=metric
        with np.load(HERE/(name+'-core.npz')) as z:
            candidate={k:(z[k][-1:] if z[k].ndim==1 else z[k][:,-1:]) for k in z.files}
        np.savez_compressed(HERE/(name+'-core-endpoint.npz'),**candidate)
        e=rows(HERE/(name+'-final.eb'),23)
        reference=dict(voltage_V=np.array([.6]),current_density_A_m2=np.array([ref[-1]]),x_m=e[:,1:2]*1e-6)
        for key,col,factor in [('Ec_eV',3,1),('Ev_eV',6,1),('Fn_eV',4,1),('Fp_eV',5,1),('n_m3',7,1e6),('p_m3',8,1e6),('electric_field_V_m',12,100),('jn_A_m2',13,10),('jp_A_m2',14,10)]:
            reference[key]=e[:,col:col+1]*factor
        np.savez_compressed(HERE/(name+'-scaps-endpoint.npz'),**reference)
        subprocess.run([sys.executable,str(tools/'compare_scaps_solver.py'),'--reference',str(HERE/(name+'-scaps-endpoint.npz')),'--candidate',str(HERE/(name+'-core-endpoint.npz')),'--output-dir',str(HERE/('comparison-'+name))],check=True,capture_output=True)
        metric['existing_comparator_endpoint_report']=json.loads((HERE/('comparison-'+name)/'summary.json').read_text())
        for filename in [name+'-core.yaml',name+'-core.npz',name+'-core-endpoint.npz',name+'-scaps-endpoint.npz']:
            shutil.copyfile(HERE/filename,OUT/name/filename)
        shutil.copytree(core.parent,OUT/name/'core-output',dirs_exist_ok=True)
        shutil.copytree(HERE/('comparison-'+name),OUT/name/'comparison-endpoint',dirs_exist_ok=True)
        axes[1,1].semilogy(scaps[:,0],np.maximum(abs(ref),1e-80),label=name+' SCAPS')
        axes[1,1].semilogy(data['voltage_V'],np.maximum(abs(j),1e-80),'--',label=name+' Core')
    axes[1,0].set_yscale('symlog',linthresh=1e-5)
    for ax,title in zip(axes.flat,['Terminal current','Current budget error |J-Jrec|','Final profile: deviation from terminal current','Core comparison']):
        ax.set_title(title);ax.grid(alpha=.25)
        if ax.get_legend_handles_labels()[0]: ax.legend(fontsize=7)
    axes[0,0].set(xlabel='Voltage (V)',ylabel='|J| (A/m²)')
    axes[0,1].set(xlabel='Voltage (V)',ylabel='Error (A/m²)')
    axes[1,0].set(xlabel='Position (µm)',ylabel='(J(x)−Jterm) / max(|Jterm|, 1e−8 A/m²)')
    axes[1,1].set(xlabel='Voltage (V)',ylabel='|J| (A/m²)')
    fig.savefig(HERE/'analysis.png',dpi=160);plt.close(fig)
    summary['total_IV_points']=total
    (HERE/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    for f in ['manifest.json','acquisition.json','generate.py','prepare_core.py','analyze.py','SCAPSErrorLogFile.log','equilibrium.script','core-binary.json','core-runs.json']:
        shutil.copyfile(HERE/f,OUT/f)
    print(json.dumps(summary,indent=2,allow_nan=False))

if __name__=='__main__': run()
