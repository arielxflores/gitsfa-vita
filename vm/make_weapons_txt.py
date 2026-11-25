# weapons.txt: one line per first-person weapon (vm_<short>.gvm), '|' separated, for the engine's weapon switching.
# Sources per field are listed in the 'src' column: clip = measured from the baked clips / gvm, imfdb = in-game magazine
# size listed on imfdb.org, armory = the Early Access armory screens (ammo 30/90 M4A1 + MP-V5A, 5/15 L118A-S1B), class = class default (no game data available: the weapon tables are in the encrypted TDBs).
import json,sys
import os;HERE=os.path.dirname(os.path.abspath(__file__));DATA=os.path.join(HERE,'..','build','data');sys.path.insert(0,HERE);import preview
OUT=sys.argv[1] if len(sys.argv)>1 else os.path.join(DATA,'weapons.txt')
INFO={}
for _p in ('/tmp/claude-0/weapons/vminfo.json',os.path.join(os.environ.get('GITSFA_TMP','/tmp/gitsfa_tools'),'weapons','vminfo.json')):   # (b82: make_vm_weapon.py writes the gitspaths TMP one now)
    if os.path.exists(_p): INFO.update(json.load(open(_p)))
MAN=[l.split()[0] for l in open(os.path.join(DATA,'audio_manifest.txt')) if l.strip() and not l.startswith('#')]
def snd(p):
    assert any(n.startswith(p) for n in MAN),p
    return p
# class defaults: rpm, dmg body, dmg head, mag, reserve, hip spread (rad), ADS world fovy (deg)
# (ar / smg damage: the armory's M4A1 35 and MP-V5A 28, Early Access tutorial 1028 / 1046 s; head = the old class ratio)
CLS={'ar':(750,35,80,30,90,0.006,51.4),'smg':(900,28,63,30,150,0.008,51.4),'pdw':(900,28,63,30,150,0.008,51.4),'sr':(30,110,220,5,25,0.040,8.07),'sg':(70,12,18,7,28,0.060,51.4),
     'lmg':(750,26,55,100,200,0.010,51.4),'hg':(400,30,65,15,60,0.007,51.4),'knife':(110,55,55,0,0,0,0),'grenade':(60,150,150,1,1,0,0)}
# recoil (rad of view kick per shot at the hip, the engine scales it by 1 - 0.57 ads): M4A1 0.0064 measured on the
# footage; MP-V5A / L118A from the armory STABILITY (42 / 60 / 15: kick ~ 100 - stability); the rest keep the old
# damage-based guess 0.0065 sqrt(old dmg x pellets / 24)
# b89 (the user's "gits armory" videos: every gun's v2.0 armory panel, the numbers printed beside the bars): DAMAGE, ACCURACY,
# STABILITY, FIRE RATE, RANGE m, magazine / reserve. The panel's DAMAGE is the body damage (M4A1 35, MP-V5A 28: the
# tutorial's), FIRE RATE x 10 the rounds per minute (M4A1 69: 690, as tuned on the footage); hip spread scales with
# 1 / ACCURACY from each class's reference gun, the view kick with 100 - STABILITY (M4A1 45: 0.0064 rad); the head damage
# keeps each class's head / body ratio. (870-ICS: DAMAGE = its 8 pellets together.)
V2={'m4a1':(35,69,45,69,20,30,90),'acr':(34,70,52,63,22,30,90),'ak47':(41,78,39,58,25,30,90),'f2000':(34,74,40,80,24,30,90),
    'famas':(33,64,50,88,20,25,75),'k2':(36,76,47,68,22,30,90),'hk417':(40,76,42,60,24,30,90),'arx160':(38,65,55,75,22,30,90),
    'x95r':(33,66,51,85,22,30,90),'scar':(40,73,45,62,24,30,90),'c30':(37,67,50,71,23,30,90),
    'kriss':(25,53,62,88,12,28,84),'mp5':(28,48,60,83,15,30,90),'l118':(156,100,15,4,42,5,15),'870':(84,35,33,6,8,8,24),
    'm249':(43,30,35,75,25,100,200),
    # b90: the SMGs of the user's SMG video (the PP-2200's panel says PERSONAL DEFENSE WEAPON)
    'c26a':(26,54,62,85,15,30,90),'evo':(29,56,61,88,18,30,90),'kacpdw':(35,57,58,70,20,30,90),'mp5k':(26,57,61,85,15,30,90),
    'mpx':(30,53,60,83,15,30,90),'p90':(26,45,62,88,15,50,150),'pp2000':(27,54,63,85,12,20,60),'ump45':(33,62,59,60,20,25,75),'mateba':(48,38,25,21,15,6,18),'seburom5':(25,51,26,60,10,20,60),
    # b91: the sniper rifles of the user's SR video (all x8 / x12 RIFLESCOPE, aim 0.25 s); the WS2088 (the beta's WA2000) from its
    # 2015 armory panel at the end of the video (ACCURACY 113 on that older scale; the menu bar stops at 100)
    'cm2010':(180,100,24,3,45,5,15),'mm200':(176,100,26,3,38,5,15),'m99d':(184,100,17,3,45,10,30),'sa50':(135,85,21,7,43,5,15),
    'seburosr':(178,100,21,3,48,5,15),'seo90':(79,58,29,33,40,10,30),'srv97':(154,100,36,4,40,10,30),'ws2088':(85,113,31,12,40,6,18),
    # b92: the pistols of the user's handgun video (the Mateba 2008M - removed, no panel anywhere - given the MATEBA 2006SM's) and
    # the light machine guns of the user's LMG video
    'mag44':(51,44,23,20,16,6,18),'b93se':(25,38,32,85,8,20,60),'kcz85':(27,53,24,50,12,17,51),'sn57':(25,47,27,60,10,20,60),
    'p226':(28,47,25,50,12,12,36),'m2008':(48,38,25,21,15,6,18),
    'l86':(42,40,36,66,25,50,150),'mg33':(42,30,31,85,25,100,200),'mg36':(45,31,35,73,28,50,150),'seburomg':(46,36,36,68,28,50,150),
    # b97: the shotguns of the user's shotgun video (KSG-G1 0:14, STA-12S2 1:46, USS 12 3:16)
    'ksg':(88,48,27,7,12,14,42),'sta12':(107,38,25,7,12,15,45),'uss12':(58,35,31,20,7,6,24),
    # b98: the melee weapons of the user's equipment video (DAMAGE, FIRE RATE, RANGE m; no accuracy / stability / ammunition).
    # The engine keeps its melee hit a kill (weapon.h) and takes the reach from RANGE (main.c mel_rng)
    'knife':(80,0,0,45,1.6,0,0),'axe':(90,0,0,55,1.5,0,0)}
V2['m4a1rd']=V2['m4a1']
SPREF={'ar':(69,0.006),'smg':(48,0.010),'pdw':(48,0.010),'lmg':(30,0.010),'hg':(51,0.007),'sr':(100,0.040)}   # (b91: sr - the L118A's 100 keeps its 0.040)   # class: (reference ACCURACY, its hip spread rad)
HEADR={'ar':80/35.0,'smg':63/28.0,'pdw':63/28.0,'lmg':55/26.0,'hg':65/30.0,'sr':2.0,'sg':1.5}
def v2(s,cl,**kw):
    d,a,st,r,rg,mg,rs=V2[s];o=dict(mag=mg,reserve=rs,v2=1)
    pel=8 if cl=='sg' else 1;o['dmg']=(round(d/float(pel),2),round(d*HEADR[cl]/pel,2)) if pel>1 else (d,int(round(d*HEADR[cl])))
    if cl not in ('sr','sg') or kw.get('action')=='semi': o['rpm']=r*10   # (b91: a semi-auto sniper rifle fires at its FIRE RATE too)
    if cl in SPREF: o['spread']=round(SPREF[cl][1]*SPREF[cl][0]/float(a),4)
    o.update(kw);return o
# b91: a bolt-action rifle's rounds per minute from its panel FIRE RATE as the L118A's (4 = 30 rpm, its fire + bolt clips; the
# CM2010's clips give 23 for its 3): x 7.5. The engine plays a bolt clip longer than the shot interval faster (main.c)
def SR_RPM(s): return V2[s][3]*7.5
def v2_recoil(s): return round(0.0064*(100-V2[s][2])/55.0,4)
RECOIL={'m4a1':0.0064,'m4a1rd':0.0064,'scar':0.0065,'acr':0.0065,'mp5':0.0044,'kriss':0.0059,'l118':0.0094,'870':0.0130,'m249':0.0068,'seburom5':0.0073,'mateba':0.0103}
ZOOM_ACOG=31.7   # 2x of the 59 deg world fovy
for _s in V2: RECOIL[_s]=v2_recoil(_s)   # (b89: every gun from its v2.0 STABILITY)
RECOIL['knife']=RECOIL['axe']=0   # (b98: blades)
# L118A scope: 8x (tutorial footage 261.5 s: a dummy 15 px tall at hip is 123 px tall in the scope, one frame later)
# hip offset: fitted on the tutorial footage (257-261 s, the scope's eyepiece at (1010,330) of 1280x720)
# pistols (P99 clips): the Early Access tutorial "Secondary Weapons Fire" (309-319 s, SEBURO M5) has the hip pistol at the
# right with the slide top at 53 % of the height and the hand + forearm filling the bottom right, and in ADS the slide
# 0.068 of the width with the hands below it: hip offset (2, 5.6, -6) cm and an ADS viewmodel fovy of 26 deg (not 11.5)
HG_HIP='2.00,5.60,-6.0';HG_ADS_VM=26.0
W=[  # short, display, class, icon, fire1p, fire3p, reload sound prefix, equip, overrides, notes
 ('m4a1','M4A1','ar',2,'wp_m4a1_fire_fp_outdoor_','wp_m4a1_fire_tp_distance_','wp_ar_m4a1_reload_','wp_ar_equip_on_1',v2('m4a1','ar'),'IS_M4 carry-handle rear sight + front post (tutorial 186-214 s, armory: no sight fitted); rpm tuned on footage'),
 ('m4a1rd','M4A1','ar',2,'wp_m4a1_fire_fp_outdoor_','wp_m4a1_fire_tp_distance_','wp_ar_m4a1_reload_','wp_ar_equip_on_1',v2('m4a1rd','ar',ads_vm=13.5),'b82: the M4A1 with the UH3 tube red dot (the Batou footage); ADS viewmodel fovy 13.5: the sight 47 % of the screen height aimed, as the footage (0:24)'),
 ('scar','SC-AR 45','ar',18,'wp_scar_fire_fp_outdoor_','wp_scar_fire_tp_distance_','wp_ar_scar_reload_','wp_ar_equip_on_1',v2('scar','ar',ads_fovy=ZOOM_ACOG),'TRICO GOA (ACOG) sight: 2x; b89 v2.0 armory bars'),
 ('acr','ACR1-G2','ar',38,'wp_acr_fire_fp_outdoor_','wp_acr_fire_distance_','wp_ar_acr_reload_','wp_ar_equip_on_1',v2('acr','ar'),'GRAFT N3 red dot; b89 v2.0 armory bars'),
 # b89: the rifles added from the user's AR video (v2.0: no sight fitted = iron sights; the F-MAS G1's 25 / 75)
 ('ak47','AK47','ar',15,'wp_ak47_fire_fp_outdoor_','wp_ak47_fire_tp_distance_','wp_ar_ak47_reload_','wp_ar_equip_on_1',v2('ak47','ar'),'IS_AK47 iron sights; AN94 clips (its body is skinned to the AN94 bones)'),
 ('k2','K22','ar',14,'wp_k2_fire_fp_outdoor_','wp_k2_fire_tp_distance_','wp_k2_reload_','wp_ar_equip_on_1',v2('k2','ar'),'IS_K2 iron sights; AN94 clips'),
 ('arx160','KHX-160','ar',79,'wp_arx160_fire_fp_outdoor_','wp_arx160_fire_tp_distance_','wp_ar_acr_reload_','wp_ar_equip_on_1',v2('arx160','ar'),'IS_M4 iron sights; AN94 clips; no reload sound of its own (the ACR\'s)'),
 ('hk417','KH-417','ar',39,'wp_hk417_fire_fp_outdoor_','wp_hk417_fire_tp_distance_','wp_ar_hk417_reload_','wp_ar_equip_on_1',v2('hk417','ar'),'IS_M4 iron sights; M4A1 clips'),
 ('f2000','F2020','ar',13,'wp_f2000_fire_fp_outdoor_','wp_f2000_fire_tp_distance_','wp_ar_f2000_reload_','wp_ar_equip_on_1',v2('f2000','ar'),'IS_F2000 iron sights; its own F2000 clips'),
 ('famas','F-MAS G1','ar',17,'wp_famas_fire_fp_outdoor_','wp_famas_fire_tp_distance_','wp_ar_famas_reload_','wp_ar_equip_on_1',v2('famas','ar'),'the carry handle\'s own sights (aim fitted to the video 6:33); its own FAMAS clips'),
 ('c30','SEBURO C-30','ar',46,'wp_c30_fire_fp_outdoor_','wp_c30_fire_tp_distance_','wp_ar_seburoc30_reload','wp_ar_equip_on_1',v2('c30','ar'),'IS_SEBURO_C30 one-piece sight with its red dot; its own AR_C30 clips (fire, reloads, draw, sprint)'),
 ('x95r','S25-R','ar',40,'wp_x95-r_fire_fp_outdoor_','wp_x95-r_fire_tp_distance_','wp_ar_x95-r_reload_','wp_ar_equip_on_1',v2('x95r','ar'),'IS_M4 iron sights; its own X95R clips (sprint too)'),
 ('mp5','MP-V5A','smg',1,'wp_ump45_fire_fp_outdoor_','wp_ump45_fire_distance_','wp_smg_mp5_reload_','wp_smg_equip_on_1',v2('mp5','smg'),'armory: FIRE RATE 83, ACCURACY 49 (M4A1 69 / 70); IS_MP5 iron sights; no MP5 fire sound in the banks (UMP45 used)'),
 ('kriss','KRIS-SV','smg',4,'wp_sig-mpx_fire_fp_outdoor_','wp_sig-mpx_fire_tp_distance_','wp_smg_kriss_reload_','wp_smg_equip_on_1',v2('kriss','smg',hip_off='-2.37,4.00,0'),'DIOTECH 660 holo; hip view 4 cm up (the Kriss clips hold it as low as their 6.5 cm ADS correction says; the class footage); no KRISS fire sound in the banks (SIG MPX used)'),
 # b90: the SMGs (sounds: each one's FMOD group; prefixes here as in the packs)
 ('c26a','SEBURO C-26A','smg',57,'wp_smg_seburoc26a_fire_fp_outdoor_','wp_smg_seburoc26a_fire_fp_outdoor_','wp_smg_seburoc26a_reload','wp_smg_equip_on_1',v2('c26a','smg'),'IS_SEBURO_C26A one-piece sight (front dot); its own SM_C26A clips'),
 ('evo','DEATHSTALKER EVO','smg',30,'wp_mp7_fire_fp_outdoor_','wp_mp7_fire_fp_outdoor_','wp_smg_scorpion_evo_reload_','wp_smg_equip_on_1',v2('evo','smg'),'IS_M4 iron sights; Scolpion clips; the game plays the MP7 shot for it'),
 ('kacpdw','KA-P5W','smg',29,'wp_kac_fire_fp_outdoor_','wp_kac_fire_tp_distance_','wp_smg_sig-mpx_reload_','wp_smg_equip_on_1',v2('kacpdw','smg'),'IS_M4 iron sights; PDW clips'),
 ('mp5k','MP5K-S3','smg',37,'wp_mp5_fire_fp_outdoor_loop_end','wp_mp5_fire_fp_outdoor_loop_end','wp_smg_mp5k_reload_','wp_smg_equip_on_1',v2('mp5k','smg'),'IS_MP5 iron sights; its own MP5K clips'),
 ('mpx','SEO MPX','smg',28,'wp_sig-mpx_fire_fp_outdoor_','wp_sig-mpx_fire_tp_distance_','wp_smg_sig-mpx_reload_','wp_smg_equip_on_1',v2('mpx','smg'),'IS_M4 iron sights; MPX clips'),
 ('p90','P-90S1','smg',6,'wp_p90_fire_fp_outdoor_loop_end','wp_p90_fire_distance_loop_end','wp_smg_p90_reload_','wp_smg_equip_on_1',v2('p90','smg'),'v2.0 ring sight not in the install: QuickPoint S1 reflex on its rail; P90 clips'),
 ('pp2000','PP-2200','pdw',71,'wp_pp2000_fire_fp_outdoor_','wp_pp2000_fire_fp_outdoor_','wp_smg_pp2000_reload_','wp_smg_equip_on_1',v2('pp2000','pdw'),'PERSONAL DEFENSE WEAPON; v2.0 ring sight not in the install: IS_M4 on its rail; its own PP2000 clips'),
 ('ump45','UMP-45S1','smg',8,'wp_ump45_fire_fp_outdoor_','wp_ump45_fire_distance_','wp_smg_ump45_reload_','wp_smg_equip_on_1',v2('ump45','smg'),'IS_UMP45 iron sights; its own UMP45 clips'),
 ('l118','L118A-S1b','sr',21,'wp_l118a_fire_fp_outdoor_','wp_l118a_fire_tp_distance_','wp_sr_l118a_reload_','wp_sr_equip_on_1',v2('l118','sr',rpm='clip',hip_off='0.70,2.80,-3.0'),
  'RIFLESCOPE 8x, baked scope view (gvm attachment scope = texture, half size, 2); bolt clip after every shot, bolt sound wp_sr_l118a_shot_reload_'),
 # b91: the sniper rifles (bolt: FIRE RATE x 7.5, SR_RPM; semi: FIRE RATE x 10, column 31 'semi'). Hip view: the L118A's, moved so
 # each scope sits where the L118A's does (height always; nearer only by moving the gun away: the SR video holds them all alike)
 ('cm2010','CM2010','sr',53,'wp_sv98_fire_fp_outdoor_','wp_sv98_fire_fp_outdoor_','wp_sr_xm2010_reload_','wp_sr_equip_on_1',v2('cm2010','sr',rpm=SR_RPM('cm2010'),hip_off='0.70,2.20,-3.00'),
  'RIFLESCOPE 8x; bipod; XM2010 reloads / bolt, M200 fire clip; the game plays the SV98 shot for it'),
 ('mm200','MM200','sr',24,'wp_m200_fire_fp_outdoor_','wp_m200_fire_tp_distance_','wp_sr_m200_reload_','wp_sr_equip_on_1',v2('mm200','sr',rpm=SR_RPM('mm200'),hip_off='0.70,2.57,-3.00'),
  'RIFLESCOPE 8x; its own M200 clips (fire, bolt, reloads, idle)'),
 ('m99d','M99D','sr',23,'wp_m98b_fire_fp_outdoor_','wp_m98b_fire_fp_outdoor_','wp_sr_m98b_reload_','wp_sr_equip_on_1',v2('m99d','sr',rpm=SR_RPM('m99d'),hip_off='0.70,1.98,-7.98'),
  'RIFLESCOPE 8x; M82 set reloads / bolt (M98B bones), M200 fire clip'),
 ('sa50','SA50','sr',50,'wp_as50_fire_fp_outdoor_','wp_as50_fire_tp_distance_','wp_sr_as50_reload_','wp_sr_equip_on_1',v2('sa50','sr',action='semi',hip_off='0.70,-1.50,-4.38'),
  'semi-auto; RIFLESCOPE 8x; bipod; its own AS50 clips'),
 ('seburosr','SEBURO SR','sr',59,'wp_c30_fire_fp_outdoor_','wp_c30_fire_tp_distance_','wp_sr_seburosr_reload','wp_sr_equip_on_1',v2('seburosr','sr',rpm=SR_RPM('seburosr'),hip_off='0.70,-1.71,-14.43'),
  'RIFLESCOPE 8x; SR_C30 set reloads / bolt, M200 fire clip; the game plays the C-30 shot for it'),
 ('seo90','SEO90','sr',78,'wp_msg90_fire_fp_outdoor_','wp_msg90_fire_fp_outdoor_','wp_sr_msg90_reload_','wp_sr_equip_on_1',v2('seo90','sr',action='semi',hip_off='0.70,0.93,-5.59'),
  'semi-auto; RIFLESCOPE 8x; its own MSG90 clips'),
 ('srv97','SR-V97','sr',25,'wp_sv98_fire_outdoor_','wp_sv98_fire_distance_','wp_sr_sv98_reload_','wp_sr_sv98_equip_on_1',v2('srv97','sr',rpm=SR_RPM('srv97'),hip_off='0.70,3.78,-3.68'),
  'RIFLESCOPE 8x; its own SV98 clips (fire, bolt, reloads)'),
 ('ws2088','WS2088','sr',77,'wp_sr_wa2000_fire_fp_outdoor','wp_sr_wa2000_fire_tp_distance_','wp_sr_wa2000_reload_','wp_sr_equip_on_1',v2('ws2088','sr',action='semi',hip_off='0.70,0.06,-7.27'),
  'semi-auto bullpup (removed after the 2015 beta); RIFLESCOPE 8x; WA2000 reloads / draw, SVU-AS fire clips'),
 # b92: the pistols (semi-auto; the 93-SE three-round bursts, column 31 'burst'; hip / aim view as the SEBURO M5) and the light
 # machine guns (hip / aim view as the M249)
 ('mag44','44 MAGNUM','hg',33,'wp_44magnum_fire_fp_outdoor_','wp_44magnum_fire_tp_distance_','wp_hg_44magnum_reload_','wp_hg_equip_on_1',v2('mag44','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),
  'revolver; 44MG set fire / draw, the Mateba reloads moved onto its own cylinder parts'),
 ('b93se','93-SE','hg',34,'wp_93r_fire_fp_outdoor_','wp_93r_fire_tp_distance_','wp_hg_93r_reload_','wp_hg_equip_on_1',v2('b93se','hg',action='burst',hip_off=HG_HIP,ads_vm=HG_ADS_VM),
  'burst-fire pistol (3 a pull at its FIRE RATE); its own 93R clips'),
 ('kcz85','KCZ-85','hg',70,'wp_hg_bulstorm_fire_fp_outdoor_','wp_hg_bulstorm_fire_fp_outdoor_','wp_hg_bulstorm_reload_','wp_hg_equip_on_1',v2('kcz85','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),
  'P99 set (P99 bones)'),
 ('sn57','SN 5-7','hg',75,'wp_hg_fn57_fire_fp_outdoor_','wp_hg_fn57_fire_fp_outdoor_','wp_hg_fn57_reload_','wp_hg_equip_on_1',v2('sn57','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),
  'P99 set (P99 bones)'),
 ('p226','SEO-P226','hg',49,'wp_p226_fire_fp_outdoor_','wp_p226_fire_tp_distance_','wp_hg_sig_p226_reload_','wp_hg_equip_on_1',v2('p226','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),
  'P99 set (P99 bones)'),
 ('m2008','MATEBA 2008M','hg',73,'wp_mateba_fire_fp_outdoor_','wp_mateba_fire_tp_distance_','wp_mateba_reload_','wp_hg_equip_on_1',v2('m2008','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),
  'revolver removed after the 2015 beta (44MG bones); the MATEBA 2006SM numbers (no panel of its own)'),
 ('l86','L86-SEO','lmg',56,'wp_l86_fire_fp_outdoor_','wp_l86_fire_fp_outdoor_','wp_l86_reload_','wp_mg_equip_on_1',v2('l86','lmg'),
  'IS_L86LSW iron sights, drum magazine, bipod; LS86LSW reloads / draw / aimed fire'),
 ('mg33','MG33-A','lmg',81,'wp_m249_fire_fp_outdoor_','wp_mg36_fire_tp_outdoor_','wp_mg_m249_reload_','wp_mg_equip_on_1',v2('mg33','lmg'),
  'M249 bones and set, its own MG3 reload; M249-style iron sights; the game plays the M249 sounds for it'),
 ('mg36','MG36-A','lmg',48,'wp_mg36_fire_fp_outdoor_','wp_mg36_fire_tp_outdoor_','wp_mg_mg36_reload_','wp_mg_equip_on_1',v2('mg36','lmg'),
  'ring rear sight + post (IS_M4), drum magazine, bipod; MG36 reloads / draw'),
 ('seburomg','SEBURO MG','lmg',69,'wp_mg_seburomg_fire_fp_outdoor_','wp_mg_seburomg_fire_fp_outdoor_','wp_mg_seburomg_reload','wp_mg_equip_on_1',v2('seburomg','lmg'),
  'its own SeburoLMG clips (fire, reloads, sprint, draw); holo iron sight'),
 # b97: the shotguns (8 pellets as the 870-ICS). KSG-G1 and STA-12S2 pump and load shell by shell (the game's events: the KSG
 # loads with the 870's shell sound and is heard from afar as the 870, the STA from afar as the USS 12); the USS 12 is semi-automatic
 # with a 6-round box magazine (column 31 'semi': fires at its FIRE RATE x 10)
 ('ksg','KSG-G1','sg',47,'wp_sg_ksg_fire_fp_outdoor_','wp_870mcs_fire_tp_distance_','wp_sg_870cms_reload_shell_','wp_sg_equip_on_1',v2('ksg','sg',rpm='clip'),
  'bullpup pump; KSG fire / ReloadStart / Reload / ReloadEnd / draw, the rest the M4A1 set; IS_M4 ring and post (the shotgun video)'),
 ('sta12','STA-12S2','sg',76,'wp_uts15_fire_fp_outdoor_','wp_usas-12_fire_tp_distance_','wp_sg_uts15_reload2_left_','wp_sg_equip_on_1',v2('sta12','sg',rpm='clip'),
  'UTS-15: the KSG bones + two port covers; its own fire / aimed fire / shell reload / draw; IS_M4 ring and post'),
 ('uss12','USS 12','sg',45,'wp_usas-12_fire_fp_outdoor_','wp_usas-12_fire_tp_distance_','wp_sg_usas-12_reload_','wp_sg_equip_on_1',v2('uss12','sg',action='semi'),
  'USAS-12: semi-automatic, box magazine (USAS_MA); its own fire / aimed fire / reloads / draw; IS_M4 ring and post'),
 ('870','870-ICS','sg',5,'wp_870mcs_fire_fp_outdoor_','wp_870mcs_fire_tp_distance_','wp_sg_870cms_reload_shell_','wp_sg_equip_on_1',v2('870','sg',rpm='clip'),
  '8 pellets; shell-by-shell reload: reload_start, reload_loop per shell (wp_sg_870cms_reload_shell_), reload_end (pump wp_sg_870cms_shot_reload_); IS_SF_COM_01 ghost ring'),
 ('m249','M249-G4','lmg',27,'wp_m249_fire_fp_outdoor_','wp_mg36_fire_tp_outdoor_','wp_mg_m249_reload_','wp_mg_equip_on_1',v2('m249','lmg'),'IS_M249 iron sights + bipod (folded); no M249 3rd-person fire sound (MG36 used)'),
 ('seburom5','SEBURO M5','hg',31,'wp_m-5_fire_fp_outdoor_','wp_m-5_fire_tp_distance_','wp_hg_p99_reload_','wp_hg_equip_on_1',v2('seburom5','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),'semi-auto; built-in sights; hip/ADS view fitted on the tutorial (EA 309-319 s); reload sound: P99 set (same clip family)'),
 ('mateba','MATEBA 2006SM','hg',32,'wp_mateba_fire_fp_outdoor_','wp_mateba_fire_tp_distance_','wp_mateba_reload_','wp_hg_equip_on_1',v2('mateba','hg',hip_off=HG_HIP,ads_vm=HG_ADS_VM),'revolver, semi-auto; built-in sights; hip/ADS view as the SEBURO M5 (same P99 clips)'),
 ('knife','MEGATECH KNIFE','knife',51,'wp_knife_swing_0','wp_knife_swing_0','-','wp_knife_equip_on',{},'fire = slash (hit sound hit_knife2_body_); no ADS/reload; no hud1'),
 ('axe','DOUBLE KATAR','knife',55,'wp_doubleblade_swing','wp_doubleblade_swing','-','wp_doubleblade_equip_on',{},
  'b98: weapon\\CN_AXE (the twin folding blades on the forearm), the Axe set: two slashes, executions from the front and from behind'),
 ('m67','M030 GRENADE','grenade',41,'wp_grenade_pin_pull_','wp_grenade_throw_','-','wp_grenade_equip_on',{},
  'cook (pin) then fire = throw: release at frame 4 (0.13 s) -> hide meshes >= 2 and spawn the projectile; explosion grenade_exp_'),
]
G={s:preview.read(os.path.join(DATA,'vm_%s.gvm'%s)) for s,*_ in W}
def clen(s,c):
    cl=G[s]['clips'].get(c);return round((cl[2].shape[0]-1)/cl[1],2) if cl else 0
lines=['# weapons.txt - first-person weapon table (vm/make_weapons_txt.py). Columns, | separated:',
 '# short|display|class|wpicon (WpIconMC iconN)|snd fire 1P prefix|snd fire 3P prefix|snd reload prefix|snd equip prefix|rpm|dmg body|dmg head|mag|reserve|'
 'reload s (tactical)|reload s (empty)|hip spread rad|ADS world fovy deg (0 = no ADS)|hip vm fovy|hip vm offset x,y,z|ADS vm fovy|ADS vm offset x,y,z|sprint|hud1|src|notes|recoil rad per shot|'
 'v2.0 armory DAMAGE|ACCURACY|STABILITY|FIRE RATE|RANGE m (b89; the engine reads up to recoil)',
 '# Engine view: world fovy 59 hip; the viewmodel is drawn with K translation + vm fovy lerped by ads (as vm_m4a1). '
 'Reload seconds = clip length (engine uses length - 0.1). Shotgun: reload/reload_empty = one shell (start+loop+end).']
for s,disp,cl,icon,f1,f3,rl,eq,ov,notes in W:
    rpm,db,dh,mag,res,spr,zf=CLS[cl];src=[]
    for x in (f1,f3,eq): snd(x)
    if rl!='-': snd(rl)
    if 'mag' in ov: mag=ov['mag'];src.append('mag=imfdb')
    if 'reserve' in ov: res=ov['reserve']
    if 'dmg' in ov: db,dh=ov['dmg']
    if 'ads_fovy' in ov: zf=ov['ads_fovy']
    if 'spread' in ov: spr=ov['spread']
    if ov.get('v2'): src.append('rpm/dmg/spread/recoil=v2.0 armory video bars')
    elif s in ('m4a1','m4a1rd','mp5','l118'): src.append('dmg=armory')
    elif cl in ('ar','smg'): src.append('dmg=armory class')
    if ov.get('rpm')=='clip':
        t=clen(s,'fire')+clen(s,'bolt');rpm=int(round(60/t));src.append('rpm=clip(fire%s %.2fs)'%('+bolt' if clen(s,'bolt') else '',t))
    elif 'rpm' in ov: rpm=ov['rpm']
    if not ov.get('v2'): src.append('rpm/dmg/reserve/spread=class' if 'rpm=clip' not in ' '.join(src) else 'dmg/reserve/spread=class')
    src.append('reload=clip')
    if cl=='sg' and not ov.get('action'): r1=r2=clen(s,'reload')   # (b97: the semi-automatic USS 12 has a magazine: its two reloads)
    else: r1,r2=clen(s,'reload'),clen(s,'reload_empty')
    info=INFO[s];ads=info.get('ads_offset')
    adsv='%.2f,%.2f,%.1f'%tuple(ads) if ads else '-'
    lines.append('|'.join(map(str,[s,disp,cl,icon,f1,f3,rl,eq,rpm,db,dh,mag,res,r1,r2,spr,zf if ads else 0,49.75,ov.get('hip_off','-2.37,0.08,0'),ov.get('ads_vm',11.5) if ads else 0,adsv,
                                     1 if 'sprint' in G[s]['clips'] else 0,1 if 'hud1' in G[s]['att'] else 0,';'.join(src),notes,RECOIL.get(s,0)]+
                                    (['%g|%d|%d|%d|%g'%(V2[s][0],V2[s][1],V2[s][2],V2[s][3],V2[s][4])] if s in V2 else [])+([ov['action']] if ov.get('action') else []))))
open(OUT,'w').write('\n'.join(lines)+'\n');print('\n'.join(lines))
