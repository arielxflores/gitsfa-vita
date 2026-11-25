# w3_<short>.gvm: the third-person weapons the bots carry (engine/src/chr.h chr_draw_rigid on the Grip bone of the
# character's pose). Built from the game's weapon\<DIR>\3rd\*.nif bodies plus their 3rd-person parts (magazine on
# wea_ma, barrel on wea_ba, bipod on wea_bi, the sight on SC_DUMMY or the iron sights on E1/E2) with the same part
# choice as the first-person weapons (vm/make_vm_weapon.py). Everything is rigid in Grip space (one palette entry);
# attachment 'muzzle' = the barrel tip in Grip space; 'lhand' (b118) = the gun's own left-hand place (its LHand_IK node).
# Sources (extracted from weapon.dat into SRC, outside git): <DIR>/3rd/*.nif|dds, Parts/<kind>/<DIR or sight>/3rd/*, Textures/
# Usage: python3 make_w3.py [outdir] [short|all] [src]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gvmlib import *
from vmw import find_tex, skin_tex, skinrun_fixed, mesh_parents
import meshopt_c as meshoptimizer   # databuilder: meshoptimizer 0.23 through ctypes (tools/lib), same results as the pip wrapper
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'anim'))
from nmb import Network
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
WHICH = sys.argv[2] if len(sys.argv) > 2 else 'all'
SRC = sys.argv[3] if len(sys.argv) > 3 else gitspaths.dirp('W3')
P = SRC + 'Parts/'
TEXD = [SRC + 'Textures/']
N = Network(gitspaths.path('ANIMBIN', 'GIS_base_3rd_runtimeBinary', 'GIS_base_3rd.nmb')); RIG = N.rigs[0]
CFG = {
 'm4a1':   dict(dir='AR_M4A1', body='M4A1_body_3rd.nif', parts=[('Magazine/AR_M4A1/3rd/M4A1_MA_3rd.nif', 'wea_ma'), ('Barrel/AR_M4A1/3rd/M4A1_BA_3rd.nif', 'wea_ba')], sight=('iron', None)),
 'c30':    dict(dir='AR_SEBURO_C30', body='SEBURO_C30_body_3rd.nif', parts=[('Magazine/AR_SEBURO_C30/3rd/SEBURO_C30_MA_3rd.nif', 'wea_ma')], sight=None),
 'scar':   dict(dir='AR_SCAR', body='SCAR_BODY_3rd.nif', parts=[('Magazine/AR_SCAR/3rd/SCAR_MA_3rd.nif', 'wea_ma'), ('Barrel/AR_SCAR/3rd/SCAR_BA_3rd.nif', 'wea_ba')], sight=('scope', 'TRICO_GOA')),
 'acr':    dict(dir='AR_ACR', body='ACR_Body_3rd.nif', parts=[('Magazine/AR_ACR/3rd/ACR_MA_3rd.nif', 'wea_ma'), ('Barrel/AR_ACR/3rd/ACR_BA_3rd.nif', 'wea_ba')], sight=('scope', 'GRAFT_N3')),
 'mp5':    dict(dir='SM_MP5', body='MP5_Body_3rd.nif', parts=[('Magazine/SM_MP5/3rd/MP5_MA_3rd.nif', 'wea_ma'), ('Barrel/SM_MP5/3rd/MP5_BA_3rd.nif', 'wea_ba')], sight=('irons', 'IS_MP5', 'IS_MP5_E1_U_3rd.nif', 'IS_MP5_E2_U_3rd.nif')),
 'kriss':  dict(dir='SM_KRISS', body='KRISS_Body_3rd.nif', parts=[('Magazine/SM_KRISS/3rd/KRISS_MA_3rd.nif', 'wea_ma'), ('Barrel/SM_KRISS/3rd/KRISS_BA_3rd.nif', 'wea_ba')], sight=('scope', 'DIOTECH660')),
 'l118':   dict(dir='SR_L118', body='L118A_Body_3rd.nif', parts=[('Magazine/SR_L118/3rd/L118A_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_L118/3rd/L118A_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 # b91: the sniper rifles (as the first-person models: magazine, barrel, the CM2010's and SA50's bipods, the 8x RIFLESCOPE)
 'cm2010': dict(dir='SR_XM2010', body='XM2010_body_3rd.nif', parts=[('Magazine/SR_XM2010/3rd/XM2010_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_XM2010/3rd/XM2010_BA_3rd.nif', 'wea_ba'), ('Bipod/SR_XM2010/3rd/XM2010_BI_3rd.nif', 'wea_bi')], sight=('scope', 'RIFLESCOPE')),
 'mm200':  dict(dir='SR_M200', body='M200_Body_3rd.nif', parts=[('Magazine/SR_M200/3rd/M200_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_M200/3rd/M200_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 'm99d':   dict(dir='SR_M82', body='M82_Body_3rd.nif', parts=[('Magazine/SR_M82/3rd/M82_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_M82/3rd/M82_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 'sa50':   dict(dir='SR_AS50', body='AS50_Body_3rd.nif', parts=[('Magazine/SR_AS50/3rd/AS50_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_AS50/3rd/AS50_BA_3rd.nif', 'wea_ba'), ('Bipod/SR_AS50/3rd/AS50_BI_3rd.nif', 'wea_bi')], sight=('scope', 'RIFLESCOPE')),
 'seburosr': dict(dir='SR_SEBURO_SR', body='SEBURO_SR_Body_3rd.nif', parts=[('Magazine/SR_SEBURO_SR/3rd/SEBURO_SR_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_SEBURO_SR/3rd/SEBURO_SR_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 'seo90':  dict(dir='SR_MSG90', body='MSG90_Body_3rd.nif', parts=[('Magazine/SR_MSG90/3rd/MSG90_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_MSG90/3rd/MSG90_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 'srv97':  dict(dir='SR_SV98', body='SV98_Body_3rd.nif', parts=[('Magazine/SR_SV98/3rd/SV98_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_SV98/3rd/SV98_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 'ws2088': dict(dir='SR_WA2000', body='WA2000_Body.nif', body_1st=1, parts=[('Magazine/SR_WA2000/3rd/WA2000_MA_3rd.nif', 'wea_ma'), ('Barrel/SR_WA2000/3rd/WA2000_BA_3rd.nif', 'wea_ba')], sight=('scope', 'RIFLESCOPE')),
 '870':    dict(dir='SG_870MCS', body='870MCS_Body_3rd.nif', parts=[], sight=('irons', 'IS_SF_COM_01', 'IS_SF_COM_01_E1_U_3rd.nif', 'IS_SF_COM_01_E2_U_3rd.nif'), muzzle='Fire'),
 'm249':   dict(dir='MG_M249', body='M249_Body_3rd.nif', parts=[('Barrel/MG_M249/3rd/M249_BA_3rd.nif', 'wea_ba'), ('Bipod/MG_M249/3rd/M249_BI_3rd.nif', 'wea_bi')], sight=('irons', 'IS_M249', 'IS_M249_E1_U_3rd.nif', 'IS_M249_E2_U_3rd.nif')),
 'seburom5': dict(dir='PL_SEBUROM5', body='SEBUROM5_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'mateba': dict(dir='PL_MATEBA', body='MATEBA_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 # b92: the pistols and light machine guns (as the first-person models)
 'mag44':  dict(dir='PL_44MG', body='44MG_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'm2008':  dict(dir='PL_2008M', body='2008M_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'b93se':  dict(dir='PL_93R', body='93R_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'kcz85':  dict(dir='PL_BULStorm', body='BULStorm_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'sn57':   dict(dir='PL_FN57', body='FN57_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'p226':   dict(dir='PL_P226', body='P226_3rd.nif', parts=[], sight=None, muzzle='Fire'),
 'mg33':   dict(dir='MG_MG3', body='MG3_Body_3rd.nif', parts=[('Barrel/MG_MG3/3rd/MG3_BA_3rd.nif', 'wea_ba'), ('Bipod/MG_MG3/3rd/MG3_BI_3rd.nif', 'wea_bi')], sight=None),
 'l86':    dict(dir='MG_L86LSW', body='L86LSW_body_3rd.nif', parts=[('Magazine/MG_L86LSW/3rd/L86LSW_2X_MA_3rd.nif', 'wea_ma'), ('Bipod/MG_L86LSW/3rd/L86LSW_BI_3rd.nif', 'wea_bi')], sight=('irons', 'IS_L86LSW', 'IS_L86LSW_E1_U_3rd.nif', 'IS_L86LSW_E2_U_3rd.nif')),
 'mg36':   dict(dir='MG_MG36', body='MG36_Body_3rd.nif', parts=[('Magazine/MG_MG36/3rd/MG36_2X_MA_3rd.nif', 'wea_ma'), ('Barrel/MG_MG36/3rd/MG36_BA_3rd.nif', 'wea_ba'), ('Bipod/MG_MG36/3rd/MG36_BI_3rd.nif', 'wea_bi')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'seburomg': dict(dir='MG_SEBURO_MG', body='SEBURO_MG_body_3rd.nif', parts=[('Magazine/MG_SEBURO_MG/3rd/SEBURO_MG_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_SEBURO_MG', 'IS_SEBURO_MG_E1_U_3rd.nif', 'IS_SEBURO_MG_E2_U_3rd.nif')),
 # b97: the shotguns (as the first-person models: the IS_M4 ring and post)
 'ksg':    dict(dir='SG_KSG', body='KSG_Body_3rd.nif', parts=[], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif'), muzzle='Fire'),
 'sta12':  dict(dir='SG_UTS15', body='UTS15_Body_3rd.nif', parts=[], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif'), muzzle='Fire'),
 'uss12':  dict(dir='SG_USAS12', body='USAS12_Body_3rd.nif', parts=[('Magazine/SG_USAS12/3rd/USAS_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif'), muzzle='Fire'),
 # b89: the rifles added from the user's AR video (their 3rd-person bodies + magazine / barrel, iron sights as in 1st person)
 'ak47':   dict(dir='AR_AK47', body='AK47_Body_3rd.nif', parts=[('Magazine/AR_AK47/3rd/AK47_MA_3rd.nif', 'wea_ma'), ('Barrel/AR_AK47/3rd/AK47_BA_3rd.nif', 'wea_ba')], sight=('irons', 'IS_AK47', 'IS_AK47_E1_U_3rd.nif', 'IS_AK47_E2_U_3rd.nif')),
 'k2':     dict(dir='AR_K2', body='K2_body_3rd.nif', parts=[('Magazine/AR_K2/3rd/K2_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_K2', 'IS_K2_E1_U_3rd.nif', 'IS_K2_E2_U_3rd.nif')),
 'arx160': dict(dir='AR_ARX_160', body='ARX_160_Body_3rd.nif', parts=[('Magazine/AR_ARX_160/3rd/ARX_160_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'hk417':  dict(dir='AR_HK417', body='HK417_Body_3rd.nif', parts=[('Magazine/AR_HK417/3rd/HK417_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'f2000':  dict(dir='AR_F2000', body='F2000_Body_3rd.nif', parts=[('Magazine/AR_F2000/3rd/F2000_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_F2000', 'IS_F2000_E1_U_3rd.nif', 'IS_F2000_E2_U_3rd.nif')),
 # b90: the SMGs (+ the PP-2200 PDW); sights as their 1st-person models
 'c26a':   dict(dir='SM_SEBURO_C26A', body='SEBURO_C26A_Body_3rd.nif', parts=[('Magazine/SM_SEBURO_C26A/3rd/SEBURO_C26A_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_SEBURO_C26A', 'IS_SEBURO_C26A_E1_U_3rd.nif', '-')),
 'evo':    dict(dir='SM_EVO', body='EVO_Body_3rd.nif', parts=[('Magazine/SM_EVO/3rd/EVO_MA_3rd.nif', 'wea_ma'), ('Barrel/SM_EVO/3rd/EVO_BA_3rd.nif', 'wea_ba')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'kacpdw': dict(dir='SM_KAC_PDW', body='KAC_PDW_Body_3rd.nif', parts=[('Magazine/SM_KAC_PDW/3rd/KAC_PDW_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'mp5k':   dict(dir='SM_MP5K', body='MP5K_Body_3rd.nif', parts=[('Magazine/SM_MP5K/3rd/MP5K_MA_3rd.nif', 'wea_ma'), ('Barrel/SM_MP5K/3rd/MP5K_BA_3rd.nif', 'wea_ba')], sight=('irons', 'IS_MP5', 'IS_MP5_E1_U_3rd.nif', 'IS_MP5_E2_U_3rd.nif')),
 'mpx':    dict(dir='SM_MPX', body='MPX_BODY_3rd.nif', parts=[('Magazine/SM_MPX/3rd/MPX_MA_3rd.nif', 'wea_ma'), ('Barrel/SM_MPX/3rd/MP5K_BA_3rd.nif', 'wea_ba')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'p90':    dict(dir='SM_P90', body='P90_body_3rd.nif', parts=[], sight=('scope', 'QuickPoint_S1')),
 'pp2000': dict(dir='SM_PP2000', body='PP2000_Body_3rd.nif', parts=[('Magazine/SM_PP2000/3rd/PP2000_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'ump45':  dict(dir='SM_UMP45', body='UMP45_body_3rd.nif', parts=[('Magazine/SM_UMP45/3rd/UMP45_MA_3rd.nif', 'wea_ma')], sight=('irons', 'IS_UMP45', 'IS_UMP45_E1_U_3rd.nif', 'IS_UMP45_E2_U_3rd.nif')),
 'famas':  dict(dir='AR_FAMAS', body='FAMAS_Body_3rd.nif', parts=[('Magazine/AR_FAMAS/3rd/FAMAS_MA_3rd.nif', 'wea_ma')], sight=None),
 'x95r':   dict(dir='AR_X95_R', body='X95_R_Body_3rd.nif', parts=[('Magazine/AR_X95R/3rd/X95R_MA_3rd.nif', 'wea_ma'), ('Barrel/AR_X95_R/3rd/X95_R_BA_3rd.nif', 'wea_ba')], sight=('irons', 'IS_M4', 'IS_M4_E1_U_3rd.nif', 'IS_M4_E2_U_3rd.nif')),
 'knife':  dict(dir='CN_CW', body='CW_3rd.nif', parts=[], sight=None, nomuzzle=1),
 # b70: the Ghost Assault cyborg's arm blade (AXE_3rd.nif keeps the 1st-person mesh 'v02' too: only its own 'v01_3rd')
 'axe':    dict(dir='CN_AXE', body='AXE_3rd.nif', parts=[], sight=None, nomuzzle=1, only='v01_3rd'),
 'm67':    dict(dir='GR_M67', body='M67_3rd.nif', parts=[], sight=None, nomuzzle=1),
}
ADD = lambda t: 2 if t and any(k in t.lower() for k in ('dot', 'lens', 'cross', 'glow', 'blur')) else 0

def bind_points(m, Wn):
    """skinned mesh at its bind pose, in nif space"""
    BIr = (m['BI'][0].view('<u2').reshape(-1, 4) if m['BI'][0].shape[1] == 8 else m['BI'][0]).astype(int)
    BW = m['BW'][0].view('<f4').reshape(len(BIr), -1)
    BW = np.c_[BW, 1 - BW.sum(1)] if BW.shape[1] == 3 else BW / np.maximum(BW.sum(1, keepdims=True), 1e-6)
    palr = m['PAL'][0].view('<u2').ravel()
    mats = [bw @ s2b for bw, s2b in zip(m['bone_world'], m['sm']['s2b'])]
    Pout = np.zeros_like(m['P'], dtype=float); Nout = np.zeros_like(m['P'], dtype=float)
    for (vs, vc), (ps, pc), (is_, ic) in zip(m['vregs'], m['PAL'][2], m['idxregs']):
        Pv = np.c_[m['P'][vs:vs + vc], np.ones(vc)]
        for j in range(BIr.shape[1]):
            M = np.array([mats[palr[ps + k]] for k in BIr[vs:vs + vc, j]])
            Pout[vs:vs + vc] += BW[vs:vs + vc, j:j + 1] * np.einsum('nij,nj->ni', M, Pv)[:, :3]
    I = np.concatenate([m['I'][is_:is_ + ic] + vs for (vs, vc), (is_, ic) in zip(m['vregs'], m['idxregs'])])
    tri = I.reshape(-1, 3); fn = np.cross(Pout[tri[:, 1]] - Pout[tri[:, 0]], Pout[tri[:, 2]] - Pout[tri[:, 0]])
    for k in range(3): np.add.at(Nout, tri[:, k], fn)
    Nout /= np.linalg.norm(Nout, axis=1, keepdims=True) + 1e-9
    return Pout, Nout, I

def build(short):
    cfg = CFG[short]; WD = SRC + cfg['dir'] + ('/1st/' if cfg.get('body_1st') else '/3rd/'); body = WD + cfg['body']   # (b91 body_1st: the WS2088's 3rd-person body mesh is not skinned: the first-person one, reduced below)
    gun, Wg, ng, _ = skinrun_fixed(body); gnode = {v: k for k, v in ng.items()}
    gW = lambda n: Wg[gnode[n]]
    iGi = np.linalg.inv(gW('Grip'))
    b = Builder(RIG); I4 = np.eye(4)
    mpar = mesh_parents(body) if cfg.get('only') else {}
    for m in gun:
        if cfg.get('only') and mpar.get(m['block']) != cfg['only']: continue
        Pn, Nn, I = bind_points(m, Wg)
        t = skin_tex(body, m['block']); tp = find_tex(t, [WD] + TEXD) or find_tex(cfg['dir'].split('_', 1)[-1] + '_3rd', [WD])
        if tp is None: tp = [WD + f for f in os.listdir(WD) if f.lower().endswith('_3rd.dds')][0]
        part = dict(P=Pn, N=Nn, UV=m['UV'], I=I, tex=tp)
        b.add_rigid([part], iGi, 'Grip', I4, '', lambda x: ADD(os.path.basename(x)), 256, texmap=lambda x: x)
    if not gun:   # b90: a body with no skinned meshes (SEBURO C-26A 3rd): its plain meshes, already in the body's space
        for m in static_meshes(body):
            if m['P'] is None or m['I'] is None or m['UV'] is None or len(m['I']) < 9: continue   # (no UVs / a stray triangle: skip)
            tp = find_tex(m['tex'], [WD] + TEXD) if m['tex'] else None
            if tp is None: tp = [WD + f for f in os.listdir(WD) if f.lower().endswith('_3rd.dds')][0]
            f32 = lambda x: np.ascontiguousarray(x, dtype=np.float32)   # (the skinned path's arrays are float32: static_meshes gives float64)
            part = dict(P=f32(m['P']), N=f32(m['N']), UV=f32(m['UV']), I=np.ascontiguousarray(m['I'].ravel(), dtype=np.int64), tex=tp)
            b.add_rigid([part], iGi, 'Grip', I4, '', lambda x: ADD(os.path.basename(x)), 256, texmap=lambda x: x)
    def place(nif, node, Wn=None):
        ms = static_meshes(nif); pd = os.path.dirname(nif) + '/'
        tm = lambda t, pd=pd: find_tex(t, [pd] + TEXD) or [pd + f for f in os.listdir(pd) if f.lower().endswith('.dds')][0]
        op = [m for m in ms if not ADD(m['tex'])]; ad = [m for m in ms if ADD(m['tex'])]
        Wn = gW(node) if Wn is None else Wn
        if op: b.add_rigid(op, iGi @ Wn, 'Grip', I4, '', lambda t: 0, 256, tm)
        if ad: b.add_rigid(ad, iGi @ Wn, 'Grip', I4, '', ADD, 128, tm)
        return ms
    tip = None
    import barrels; parts_, fr_ = barrels.parts_for(short, cfg['parts'], third=True)   # b105: the barrel and muzzle piece the PC shows the gun with (vm/barrels.py)
    for nif, node in parts_:
        if node not in gnode: print('  (no node %s in %s)' % (node, cfg['body'])); continue
        isba_ = nif.startswith('Barrel/') and bool(fr_); Wn_ = gW(node)
        if isba_:
            bw = node_worlds(P + nif)
            if 'mu_x' in fr_ and not fr_.get('barrel') and 'we_mu' in bw: Wn_ = Wn_.copy(); Wn_[:3, 3] += Wn_[:3, 0] / np.linalg.norm(Wn_[:3, 0]) * (fr_['mu_x'] - (Wn_ @ bw['we_mu'])[0, 3])
            ms = place(P + nif, node, Wn_); x = max(m['P'][:, 0].max() for m in ms); tip = (iGi @ Wn_ @ np.array([x, 0, 0, 1.0]))[:3]
            mud = P + 'Silencer/' + cfg['dir'] + '/3rd/'; mus = [f for f in (os.listdir(mud) if os.path.isdir(mud) else []) if f.lower().endswith('_mu_3rd.nif')]
            Wm = None
            if mus and 'mu_x' in fr_: Wm = gW('wea_ba').copy(); Wm[0, 3] = fr_['mu_x']
            elif mus and 'we_mu' in bw: Wm = Wn_ @ bw['we_mu']
            elif mus and fr_.get('mu_end'): Wm = Wn_.copy(); Wm[:3, 3] += Wm[:3, 0] / np.linalg.norm(Wm[:3, 0]) * x
            if Wm is not None:
                mms = place(mud + mus[0], node, Wm); mw = node_worlds(mud + mus[0])
                tip = (iGi @ Wm @ mw['Fire'])[:3, 3] if 'Fire' in mw else (iGi @ Wm @ np.array([max(m['P'][:, 0].max() for m in mms), 0, 0, 1.0]))[:3]
                print('  muzzle piece', mus[0])
            continue
        ms = place(P + nif, node)
        if node == 'wea_ba': x = max(m['P'][:, 0].max() for m in ms); tip = (iGi @ gW('wea_ba') @ np.array([x, 0, 0, 1.0]))[:3]
        # b103: the gun's own muzzle piece (Parts\Silencer\<dir>\3rd\<GUN>_MU_3rd.nif) on the barrel's 'we_mu' node, as make_vm_weapon.py
        if node == 'wea_ba':
            bw = node_worlds(P + nif); mud = os.path.dirname(P + nif).replace('/Barrel/', '/Silencer/') + '/'
            mus = [f for f in (os.listdir(mud) if os.path.isdir(mud) else []) if f.lower().endswith('_mu_3rd.nif')]
            if 'we_mu' in bw and mus:
                Wm = gW(node) @ bw['we_mu']; place(mud + mus[0], node, Wm); mw = node_worlds(mud + mus[0])
                if 'Fire' in mw: tip = (iGi @ Wm @ mw['Fire'])[:3, 3]
                print('  muzzle piece', mus[0])
    s = cfg['sight']
    if s and s[0] == 'scope' and 'SC_DUMMY' in gnode:
        sd = P + 'ScopeSight/%s/3rd/' % s[1]; place(sd + [f for f in os.listdir(sd) if f.endswith('.nif')][0], 'SC_DUMMY')
    elif s and s[0] == 'irons':
        sd = P + 'IronSight/%s/3rd/' % s[1]
        for f, node in ((s[2], 'E1'), (s[3], 'E2')):
            if node in gnode and os.path.exists(sd + f): place(sd + f, node)
    if cfg.get('muzzle') and cfg['muzzle'] in gnode: tip = (iGi @ gW(cfg['muzzle']))[:3, 3]
    if tip is None and not cfg.get('nomuzzle'):
        Pa = np.concatenate([(np.c_[m['P'], np.ones(len(m['P']))] @ I4.T)[:, :3] for m in b.meshes]); tip = np.array([Pa[:, 0].max(), 0, np.median(Pa[:, 2])])
    # third person: ~1600 triangles a weapon (meshoptimizer edge collapses; positions / UVs of kept vertices untouched)
    tot = sum(len(m['I']) // 3 for m in b.meshes); budget = 1600
    if tot > budget:
        for M in b.meshes:
            T = len(M['I']) // 3; tgt = max(24, int(T * budget / tot))
            if T <= tgt or T < 60: continue
            I = M['I'].astype(np.uint32); Pf = np.ascontiguousarray(M['P'], np.float32); dst = np.zeros(len(I), np.uint32)
            n = meshoptimizer.simplify(dst, I, Pf, target_index_count=tgt * 3, target_error=0.02)
            I2 = dst[:n].astype(np.int64); used = np.unique(I2); remap = np.full(len(Pf), -1, np.int64); remap[used] = np.arange(len(used))
            for k in ('P', 'N', 'UV', 'BI', 'BW'): M[k] = M[k][used]
            M['I'] = remap[I2]
    b.att = {'muzzle': tip} if tip is not None else {}
    # b118: the gun's own place for the left hand (node LHand_IK of the 3rd-person body: the eight guns that borrow another
    # family's animation set have one - F2000, X95R, L86, SEBURO MG, MP5K, PP-2200, UMP-45, SR-V97): attachment 'lhand', the
    # wrist in Grip space. The engine bends the left arm to it where a pose holds the gun in both hands (main.c og_lhand).
    if 'LHand_IK' in gnode:   # ('lhand_x' / 'lhand_y': the node's x and y axes as points 10 cm from it - the hand bone's turn there)
        M_ = iGi @ gW('LHand_IK'); b.att['lhand'] = M_[:3, 3]; b.att['lhand_x'] = M_[:3, 3] + 10 * M_[:3, 0] / np.linalg.norm(M_[:3, 0]); b.att['lhand_y'] = M_[:3, 3] + 10 * M_[:3, 1] / np.linalg.norm(M_[:3, 1])
        print('  left hand at', np.round(b.att['lhand'], 1), 'x axis', np.round(M_[:3, 0], 2), 'y axis', np.round(M_[:3, 1], 2), '(%d attachments)' % len(b.att))
    path = OUT + '/w3_%s.gvm' % short
    print('w3', short, b.write(path), 'bytes; meshes', [(len(x['P']), len(x['I']) // 3, x['blend']) for x in b.meshes], 'muzzle', None if tip is None else np.round(tip, 1))

for sh in (CFG if WHICH == 'all' else [WHICH]): build(sh)
