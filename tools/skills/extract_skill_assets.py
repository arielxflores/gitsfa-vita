# How /root/gits/raw/skills was made (run on the PC side, where the .dat files are; needs VitaPort/tools/gitsdat.py):
#   python3 extract_skill_assets.py "<Game Files>/Data" <outdir>     then convert DDS -> PNG into <outdir>/png (PIL opens DXT DDS)
import sys, os, re
sys.path.insert(0, os.path.join(os.path.dirname(sys.argv[1].rstrip('/\\')), '..', 'VitaPort', 'tools'))
import gitsdat
D = sys.argv[1].rstrip('/\\') + '/'; OUT = sys.argv[2].rstrip('/') + '/'
pats = {'model.dat': [r'model\\Npc\\CB_', r'model\\Object\\', r'model\\Objects\\CB_CC'],
        'weapon.dat': [r'weapon\\Armluncher\\', r'weapon\\CN_CW\\', r'weapon\\CW_CW\\', r'weapon\\Textures\\turret', r'weapon\\GR_STICKYBOMB'],
        'effect.dat': [r'effect\\cyberbrain\\', r'effect\\screen\\', r'effect\\explosion\\', r'effect\\missile\\', r'effect\\kfm\\', r'effect\\turret\\',
                       r'effect\\character\\(emp_ch_ele|sprint|strength|ally_eye|enemy_eye)', r'effect\\outgame_npc\\', r'effect\\camera\\cam_wd_inrush',
                       r'effect\\ghost_mode\\', r'effect\\cyber_world\\(1p_cyber_world|3p_cyber_world|cyber_sky)', r'effect\\TEST\\']}
def save(z, i):
    d = OUT + i.filename.replace('\\', '/'); os.makedirs(os.path.dirname(d), exist_ok=True); open(d, 'wb').write(z.read(i))
for dat, pl in pats.items():
    z = gitsdat.lst(D + dat)
    for i in z.infolist():
        if any(re.search(p, i.filename, re.I) for p in pl): save(z, i)
# the effect textures those NIFs reference (effect\texture\...)
names = set()
for root, ds, fs in os.walk(OUT + 'effect'):
    for f in fs:
        if f.lower().endswith('.nif'):
            for m in re.findall(rb'[\w\-\. ]+\.dds', open(os.path.join(root, f), 'rb').read(), re.I): names.add(os.path.basename(m.decode('latin1')).lower())
z = gitsdat.lst(D + 'effect.dat')
for i in z.infolist():
    if i.filename.split('\\')[-1].lower() in names and 'texture' in i.filename.lower(): save(z, i)
# skill videos + skill chip icons from ui.dat
z = gitsdat.lst(D + 'ui.dat')
for i in z.infolist():
    if 'video\\skill' in i.filename or 'Skill_Chip' in i.filename: save(z, i)
