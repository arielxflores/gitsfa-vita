# b105 (user: "KRIS-SV still seems to be missing the tip", "the UMP-45S1 ... does not seem to render properly when you inspect
# it"): each gun's barrel and muzzle piece as the PC shows the gun.
#
# Until b102 a gun had a barrel only when weapon\Parts\Barrel\<its folder> exists, and no muzzle piece at all; b103 put
# the muzzle piece (Parts\Silencer\<folder>\<GUN>_MU.nif) on the barrel's 'we_mu' node. Checked against the PC's own
# pictures of every gun's default look (ui.dat <GUN>_BASIC_01.png, 384 x 150; tools/weapons/front_fit/fit.py lays the port's
# side view on the picture - scale and place from the rear three quarters of the gun - and reads where the PC gun's tip
# is in the gun's own space; M4A1 +0.2 cm, SC-AR +0.5, MP-V5A 0.0: the method and b103's rule hold for those):
#   - 12 guns had no barrel and no muzzle at all (their folders have a muzzle piece but no barrel: the PC gives them
#     one of the shared barrels - Parts\Barrel\AR_BARREL_01, L_BARREL, S_BARREL, SM_BARREL_01, MG_BARREL_01);
#   - on the KRISS, the EVO and the MP5K the barrel's 'we_mu' lies INSIDE the body: the muzzle piece was hidden;
#   - the AK47's tip was 2 cm, the MG36-A's 7 cm, the SEO90's 3 cm off the picture, the ACR's barrel 6 cm too long.
# Where the muzzle piece's own pivot ('Editable Poly' node) has exactly the body's barrel height it is the place the
# piece was modelled at on that gun (KRISS 32.59, EVO 31.51, UMP45 34.02, AK47 57.56: each within 0.8 cm of the
# picture): 'mu_x'. Otherwise the shared barrel / node that brings the tip nearest the picture (the error in the note).
#   barrel: 'folder/stem' of a shared barrel that replaces the gun's own (or gives it one)
#   node:   the body node it sits on (default 'wea_ba')
#   mu_x:   the muzzle piece's place along the barrel, body space (cm); the gun's OWN barrel is moved so its 'we_mu' is there
#   mu_end: the barrel has no 'we_mu' node - the muzzle piece on the barrel's end
FRONT = {
 'kriss':    dict(mu_x=32.59),                                           # picture tip 36.9, model 36.8
 'evo':      dict(mu_x=31.51),                                           # 37.3 / 36.5
 'ak47':     dict(mu_x=57.56),                                           # 59.8 / 59.7 (was 61.8)
 'ump45':    dict(barrel='SM_BARREL_01/SM_BA_01', mu_x=34.02),           # (no barrel, no muzzle before)
 'kacpdw':   dict(barrel='SM_BARREL_01/SM_BA_01'),                       # 47.5 / 47.6
 'p90':      dict(barrel='SM_BARREL_01/SM_BA_01'),                       # 21.6 / 21.7
 'pp2000':   dict(barrel='SM_BARREL_01/SM_BA_01'),                       # 21.8 / 22.3
 'mp5k':     dict(barrel='SM_BARREL_01/SM_BA_01'),                       # 32.0 / 33.1 (its own barrel: 25.3, behind the body's front at 29.0)
 'arx160':   dict(barrel='AR_BARREL_01/AR_BA_01'),                       # 60.1 / 59.2
 'c30':      dict(barrel='L_BARREL/L_BARREL_BA_01'),                     # 43.7 / 43.3
 'famas':    dict(barrel='L_BARREL/L_BARREL_BA_01'),                     # 41.2 / 41.6
 'k2':       dict(barrel='L_BARREL/L_BARREL_BA_01', node='wea_ba_l'),    # 60.3 / 60.0
 'hk417':    dict(barrel='S_BARREL/S_BARREL_BA_01', node='wea_ba_l'),    # 52.3 / 52.9
 'f2000':    dict(barrel='AR_BARREL_01/AR_BA_01'),                       # 39.2 / 38.7 (in the Grip's space: this body's Grip is 5 cm before its origin)
 'acr':      dict(barrel='S_BARREL/S_BARREL_BA_01'),                     # 57.1 / 57.6 (its own barrel: 63.2)
 'mg36':     dict(barrel='MG_BARREL_01/MG_BA_01'),                       # 67.0 / 68.3 (its own, a rifle's barrel: 59.9)
 'l86':      dict(barrel='MG_BARREL_01/MG_BA_01'),                       # 55.7 / 55.6
 'seburomg': dict(barrel='MG_BARREL_01/MG_BA_01'),                       # 50.1 / 49.9
 'seo90':    dict(node='wea_ba_l'),                                      # 81.5 / 81.2 (on 'wea_ba': 78.5)
 'x95r':     dict(mu_end=True),                                          # 41.2 / 41.6 (no muzzle piece before: 36.5)
}
def parts_for(short, parts, third=False):
    """the gun's part list [(nif, node, ...)] with this table's barrel: -> (parts, FRONT entry)"""
    f = FRONT.get(short, {}); out = list(parts)
    if f.get('barrel'):
        d, s = f['barrel'].split('/'); nif = 'Barrel/%s/%s/%s%s.nif' % (d, '3rd' if third else '1st', s, '_3rd' if third else '')
        ex = [p for p in out if p[1] == 'wea_ba']; new = (nif, f.get('node', 'wea_ba')) + tuple(ex[0][2:] if ex else (() if third else ('Grip',)))
        out = [p for p in out if p[1] != 'wea_ba'] + [new]
    elif f.get('node'): out = [((p[0], f['node']) + tuple(p[2:])) if p[1] == 'wea_ba' else p for p in out]
    return out, f
