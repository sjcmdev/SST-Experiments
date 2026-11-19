"""Derive Core comparison inputs from saved SCAPS definitions, without editing Core."""
from pathlib import Path
import re, yaml

HERE = Path(__file__).resolve().parent

def num(text,key):
    match = re.search(r'^'+re.escape(key)+r'\s*:\s*([-+\d.eE]+)',text,re.M)
    assert match, key
    return float(match[1])

for name in ['baseline','low80','thin']:
    text = (HERE/(name+'.def')).read_text(encoding='cp1252')
    head,*parts = text.split('\nlayer\n')
    contacts = []
    for contact in [head.split('back contact')[1],parts[-1].split('front contact')[1]]:
        assert num(contact,'flatband')==1
        contacts.append(dict(label='flatband',flat_band=True,phi_m_eV=4.5,Sn_m_s=num(contact,'Sn'),Sp_m_s=num(contact,'Sp')))
    layers = []
    for part in parts:
        material = dict(T_ref_K=300)
        for key,label in [('eps_r','eps'),('Eg_eV','Eg'),('chi_eV','chi'),('Nc_ref_m3','Nc'),('Nv_ref_m3','Nv'),('mu_n_m2_V_s','mu_n'),('mu_p_m2_V_s','mu_p'),('vth_n_ref_m_s','v_th_n'),('vth_p_ref_m_s','v_th_p')]:
            material[key] = num(part,label)
        layer = dict(name=part.split('name :')[1].splitlines()[0].strip(),thickness_m=num(part,'d'),material=material,doping=dict(Nd_m3=num(part,'Nd(uniform)'),Na_m3=num(part,'Na(uniform)')),defects=[])
        if 'srhrecombination' in part:
            defect = part.split('srhrecombination')[1]
            layer['defects'] = [dict(label='midgap',energy_reference='relative_to_Ev',energy_offset_eV=num(defect,'Et'),Nt_m3=num(defect,'Nt(uniform)'),sigma_n_m2=num(defect,'sigma_n'),sigma_p_m2=num(defect,'sigma_p'),charge_type='neutral')]
        layers.append(layer)
    project = dict(schema_version=1,case_id='phase11-'+name,temperature_K=80 if name=='low80' else 300,constants_mode='scaps_compat',illumination=dict(enabled=False,uniform_generation_m3_s=0),voltage=dict(start_V=0,stop_V=.6,step_V=.02),mesh=dict(points_per_layer=401,clustering_beta=3),solver=dict(max_newton_iterations=1000,residual_tolerance=1e-10,max_line_search_backtracks=20,max_step_multiple_of_Vt=4),contacts=dict(zip(['left','right'],contacts)),layers=layers)
    assert len(layers)==2 and len(layers[0]['defects'])==1
    (HERE/(name+'-core.yaml')).write_text(yaml.safe_dump(project,sort_keys=False))
print('Three Core YAML inputs derived from SCAPS saved definitions')
