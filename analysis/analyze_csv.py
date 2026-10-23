#!/usr/bin/env python3
"""Análisis reproducible de la última ventana conjunta válida de E-TWIN v2.

No imputa datos ni elimina valores por su amplitud. La ventana es cerrada en
ambos extremos: 600 s con cadencia de 20 s puede contener 31 observaciones.
Los indicadores de coherencia NO son errores frente a un patrón independiente.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
FIELDS = [f'field{i}' for i in range(1,9)]
NAMES = ['voltage_V','current_A','power_W','energy_kWh','frequency_Hz',
         'power_factor','acceleration_rms_m_s2','acceleration_peak_m_s2']
STATUS_NUM = ['ver','e','v','ea','va','alarm','ref','dp','vr','boot','seq','vf',
              'run','ot','rc','ct','vx','vy','vz','cf','fs','ns','mi','rj','rp']

def parse_status(value: object) -> dict[str, str]:
    return dict(item.split('=',1) for item in str(value).split(';') if '=' in item)

def analyze(source: Path, minutes: float=10) -> tuple[pd.DataFrame,pd.DataFrame,dict]:
    if not source.is_file():
        raise FileNotFoundError(source)
    if minutes <= 0:
        raise ValueError('La duración debe ser positiva.')
    raw = pd.read_csv(source, dtype=str, keep_default_na=False)
    required = {'created_at','entry_id','status', *FIELDS}
    if not required.issubset(raw.columns):
        raise ValueError(f'Columnas ausentes: {sorted(required-set(raw.columns))}')
    d=raw.copy()
    d['timestamp']=pd.to_datetime(d['created_at'],utc=True,errors='coerce')
    d['entry_id']=pd.to_numeric(d['entry_id'],errors='coerce')
    for c in FIELDS:
        d[c]=pd.to_numeric(d[c],errors='coerce')
    parsed=pd.DataFrame(d['status'].apply(parse_status).tolist(),index=d.index)
    for c in STATUS_NUM:
        d['s_'+c]=pd.to_numeric(parsed.get(c,pd.Series(index=d.index,dtype=str)),errors='coerce')
    d['s_state']=parsed.get('state',pd.Series(index=d.index,dtype=str))
    finite=pd.Series(np.isfinite(d[FIELDS].to_numpy(dtype=float)).all(axis=1),index=d.index)
    criteria={
      'fecha_y_id':d['timestamp'].notna() & d.entry_id.notna(),
      'ocho_campos_finitos':finite,
      'protocolo_v2':d.s_ver.eq(2),
      'nodos_validos':d.s_e.eq(1)&d.s_v.eq(1),
      'vigencia_5_s':d.s_ea.between(0,4999)&d.s_va.between(0,4999),
      'rango_electrico':d.field1.between(0,300)&d.field2.between(0,110)&
        d.field3.between(0,33000)&d.field4.ge(0)&d.field5.between(40,70)&d.field6.between(0,1.01),
      'rango_vibracion':d.field7.between(0,150,inclusive='left')&d.field8.ge(d.field7)&d.field8.lt(150),
      'calidad_imu':d.s_vf.eq(1)&d.s_fs.between(475,525)&d.s_ns.eq(500)&d.s_mi.eq(0)
    }
    d['joint_valid']=pd.DataFrame(criteria).all(axis=1)
    d['rejection_reasons']=[';'.join(k for k,v in criteria.items() if not bool(v.loc[i])) for i in d.index]
    valid=d[d.joint_valid].sort_values('timestamp')
    if valid.empty:
        raise ValueError('No hay observaciones conjuntas válidas con el contrato v2.')
    end=valid.timestamp.max(); start=end-pd.Timedelta(minutes=minutes)
    window=d[d.timestamp.between(start,end)].sort_values(['timestamp','entry_id']).copy()
    selected=window[window.joint_valid].copy()
    if selected.timestamp.duplicated().any() or selected.entry_id.duplicated().any():
        raise ValueError('La ventana contiene fechas o identificadores duplicados; revisar sin deduplicación silenciosa.')
    if len(selected)<2:
        raise ValueError('Se requieren al menos dos observaciones para integrar energía.')
    selected['elapsed_s']=(selected.timestamp-selected.timestamp.iloc[0]).dt.total_seconds()
    selected['apparent_power_VA']=selected.field1*selected.field2
    selected['power_from_VIpf_W']=selected.apparent_power_VA*selected.field6
    selected['power_internal_deviation_pct']=100*(selected.field3-selected.power_from_VIpf_W).abs()/selected.field3.replace(0,np.nan)
    selected['rms_from_axes_m_s2']=np.sqrt(selected.s_vx**2+selected.s_vy**2+selected.s_vz**2)
    selected['energy_delta_Wh']=(selected.field4-selected.field4.iloc[0])*1000
    times=selected.elapsed_s.to_numpy(); power=selected.field3.to_numpy()
    incr=np.diff(times)*(power[:-1]+power[1:])/2/3600
    selected['energy_trapezoid_Wh']=np.r_[0,np.cumsum(incr)]
    intervals=selected.timestamp.diff().dt.total_seconds().dropna()
    delta=float(selected.energy_delta_Wh.iloc[-1]); integral=float(selected.energy_trapezoid_Wh.iloc[-1])
    first_baseline=selected[selected.s_ref.eq(1)]
    summary={
      'source_file':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
      'selection':f'[ultima_fecha_conjunta_valida - {minutes*60:g} s, ultima_fecha_conjunta_valida], ambos extremos incluidos',
      'input_rows':int(len(raw)),'joint_valid_rows_entire_file':int(d.joint_valid.sum()),
      'window_start_UTC':start.isoformat(),'window_end_UTC':end.isoformat(),
      'window_start_Ecuador':start.tz_convert('America/Guayaquil').isoformat(),
      'window_end_Ecuador':end.tz_convert('America/Guayaquil').isoformat(),
      'window_requested_seconds':minutes*60,'observed_span_seconds':float(times[-1]),
      'window_rows':len(window),'window_valid_rows':len(selected),
      'window_rejected_rows':int((~window.joint_valid).sum()),
      'entry_id_first':int(selected.entry_id.iloc[0]),'entry_id_last':int(selected.entry_id.iloc[-1]),
      'interarrival_min_s':float(intervals.min()),'interarrival_median_s':float(intervals.median()),
      'interarrival_max_s':float(intervals.max()),
      'energy_counter_delta_Wh':delta,'energy_trapezoid_Wh':integral,
      'energy_internal_difference_Wh':abs(delta-integral),
      'energy_internal_difference_pct':100*abs(delta-integral)/abs(delta) if delta else None,
      'power_internal_deviation_mean_pct':float(selected.power_internal_deviation_pct.mean()),
      'power_internal_deviation_max_pct':float(selected.power_internal_deviation_pct.max()),
      'rms_axis_identity_max_abs_m_s2':float((selected.field7-selected.rms_from_axes_m_s2).abs().max()),
      'crest_identity_max_abs':float((selected.field8/selected.field7-selected.s_cf).abs().max()),
      'states':{str(k):int(v) for k,v in selected.s_state.value_counts().items()},
      'alarm_masks':{str(int(k)):int(v) for k,v in selected.s_alarm.value_counts().items()},
      'first_published_baseline_Ecuador':None if first_baseline.empty else first_baseline.timestamp.iloc[0].tz_convert('America/Guayaquil').isoformat(),
      'rows_with_baseline':int(selected.s_ref.eq(1).sum()),
      'rows_after_window':int(d.timestamp.gt(end).sum()),
      'standard_deviation':'muestral, ddof=1',
      'interpretation':'Consistencia interna, no exactitud frente a un patrón. Cadencia cloud, no latencia ni PDR de ESP-NOW.'
    }
    return d,selected,summary

def make_plots(w:pd.DataFrame, out:Path)->None:
    out.mkdir(parents=True,exist_ok=True)
    x=w.elapsed_s/60
    first=w.timestamp.iloc[0].tz_convert('America/Guayaquil')
    time_label=f'Tiempo desde {first:%H:%M:%S} (min) · {first:%d/%m/%Y} · UTC−5'
    end_minutes=float(x.iloc[-1])
    specs=[
      ('potencia_10min',[('Potencia activa',w.field3)],'Potencia activa (W)'),
      ('vibracion_10min',[('Aceleración RMS',w.field7),('Aceleración pico',w.field8)],'Aceleración (m/s²)'),
      ('energia_10min',[('Incremento del contador',w.energy_delta_Wh),('Integral trapezoidal de P',w.energy_trapezoid_Wh)],'Energía desde el inicio (Wh)')]
    for name,curves,ylabel in specs:
        fig,ax=plt.subplots(figsize=(7.5,2.5),layout='constrained')
        for j,(label,y) in enumerate(curves):
            ax.plot(x,y,label=label,marker='o' if j==0 else 's',markersize=3,
                    linewidth=1.3,linestyle='-' if j==0 else '--')
        ax.set(xlabel=time_label,ylabel=ylabel,xlim=(0,end_minutes))
        ax.set_xticks(np.linspace(0,end_minutes,6));ax.tick_params(labelsize=9)
        ax.grid(True,alpha=.25)
        if len(curves)>1: ax.legend(fontsize=8,loc='best',frameon=False)
        fig.savefig(out/(name+'.png'),dpi=300)
        fig.savefig(out/(name+'.pdf'))
        plt.close(fig)

def main()->None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--csv',type=Path,default=ROOT/'data/feeds_original.csv')
    p.add_argument('--minutes',type=float,default=10)
    p.add_argument('--out',type=Path,default=ROOT/'data')
    p.add_argument('--figures',type=Path,default=ROOT/'articulo/figuras')
    a=p.parse_args(); a.out.mkdir(parents=True,exist_ok=True)
    d,w,summary=analyze(a.csv,a.minutes)
    # Export original column values/precision unchanged for selected entry IDs.
    raw=pd.read_csv(a.csv,dtype=str,keep_default_na=False)
    raw[pd.to_numeric(raw.entry_id,errors='coerce').isin(w.entry_id)].to_csv(a.out/'ultimos_10_min_validos.csv',index=False)
    semantic=w.rename(columns=dict(zip(FIELDS,NAMES))).copy()
    semantic['timestamp_Ecuador']=semantic.timestamp.dt.tz_convert('America/Guayaquil').astype(str)
    semantic.to_csv(a.out/'ventana_analitica.csv',index=False)
    d[['created_at','entry_id','joint_valid','rejection_reasons']].to_csv(a.out/'auditoria_validez.csv',index=False)
    metrics=FIELDS+['apparent_power_VA','s_vx','s_vy','s_vz','s_cf','s_fs','s_ea','s_va']
    stats=w[metrics].agg(['count','mean','std','min','max']).T.rename(index=dict(zip(FIELDS,NAMES)))
    stats.to_csv(a.out/'estadisticas.csv',index_label='variable')
    (a.out/'resumen.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    make_plots(w,a.figures)
    labels=[('Tensión','V',2),('Corriente','A',3),('Potencia activa','W',2),('Energía acumulada','kWh',3),
            ('Frecuencia','Hz',3),('Factor de potencia','--',3),('Aceleración RMS',r'm/s$^2$',3),('Aceleración pico',r'm/s$^2$',3)]
    lines=['% Generado automáticamente por analysis/analyze_csv.py; s = desviación muestral.',r'\begin{tabular}{llrrrr}\toprule',r'Variable & Unidad & Media & $s$ & Mínimo & Máximo\\\midrule']
    for c,(label,unit,dec) in zip(FIELDS,labels):
        vals=[w[c].mean(),w[c].std(ddof=1),w[c].min(),w[c].max()]
        fmt=lambda v: f'{v:.{dec}f}'.replace('.',',')
        lines.append(label+' & '+unit+' & '+' & '.join(map(fmt,vals))+r' \\')
    article=a.figures.parent
    lines.append(r'\bottomrule\end{tabular}')
    (article/'tabla_estadisticas.tex').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))

if __name__=='__main__':
    main()
