# Per-map colour-grading LUTs (map.dat Fix_<map>\*_CG_*.tga, 256x16 = 16 blue slices of 16x16 red x green) -> raw <stem>.glut
# then: tools/grade_fit.py <dir> <dir> appends the polynomial fit the engine grades with (grade.h GRADE_POLY)
import sys, os, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import gitspaths
SRC = (sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('CG')) if __name__ == '__main__' else None
OUT = (sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')) if __name__ == '__main__' else None
MAP = {'downtown': 'DM02_CG_20151020', 'berutalube': 'TTM02_CG_20151020', 'ecenter': 'E_Center0', 'ghost_storage': 'TDM01_CG_20151020',
       'headquarter': 'Ghost_PSS9_CG_20160224', 'port': 'TDM02_CG_20151020', 'pss9': 'DM01_CG_20151020', 'ruinednight': 'TDM05_CG_20160113',
       'ruinedtokyo': 'TTM01_CG_20170215-2', 'trainstation': 'DEM_Train_CGfinal', 'underground': 'TTM03_CG_green', 'urban': 'TDM3_CG_20151020',
       'zioplant': 'DM03_CG_20151020', 'tutorial': 'TUT_151110', 'tutorial2': 'TUT_151110',
       'shooting': '1test_16', 'tutorial3': '1test_16'}   # map.dat Fix_Shooting / Fix_Tutorial3 1test_16.tga
def lut(src, stem):
    """the map's LUT as 12288 bytes (16 blue slices of 16 x 16, RGB)"""
    a = np.asarray(Image.open(os.path.join(src, MAP[stem] + '.tga')).convert('RGB'), np.uint8)
    assert a.shape == (16, 256, 3), a.shape
    return a.tobytes()
if __name__ == '__main__':
    for stem, n in MAP.items():
        open(os.path.join(OUT, stem + '.glut'), 'wb').write(lut(SRC, stem)); print(stem, n)
