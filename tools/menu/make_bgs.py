# Map loading backgrounds -> GTX1 DXT1 1024x512 (shown stretched to 960x544)
import sys, struct, numpy as np, etcpak
from PIL import Image
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
SRC = gitspaths.dirp('MENURSC', 'menu', 'ui', 'GFx_DA', 'Data', 'rsc', 'map', 'loading', 'background')
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.dirp('DATA')
MAP_BG = {'downtown': 'dem003.png', 'berutalube': 'ttm02.png', 'ecenter': 'dem007.png', 'ghost_storage': 'TDM01.png', 'headquarter': 'Ghost_001.png',
          'port': 'TDM_Port_bg.PNG', 'pss9': 'DEM001bg.png', 'ruinednight': 'tdm05.png', 'ruinedtokyo': 'ttm01bg.png', 'trainstation': 'dem006.png',
          'underground': 'Undergroundbg.png', 'urban': 'TDM03_urban.png', 'zioplant': 'dem005.png', 'shooting': 'tut01bg.png', 'tutorial': 'Tutorial1_big.png',
          'tutorial2': 'Tutorial2_big.png', 'tutorial3': 'Tutorial3_big.png', 'title': 'Tutorial0_big.png'}
def gtx(img, path, w=1024, h=512):
    im = img.convert('RGB').resize((w, h), Image.LANCZOS).convert('RGBA')
    data = etcpak.compress_to_dxt1(np.ascontiguousarray(np.asarray(im)).tobytes(), w, h)
    with open(path, 'wb') as f: f.write(b'GTX1' + struct.pack('<HHBBHI', w, h, 1, 0, 0, len(data))); f.write(data)
    return len(data)
if __name__ == '__main__':
    tot = 0
    for stem, fn in MAP_BG.items(): tot += gtx(Image.open(SRC + fn), OUT + 'mload_%s.gtx' % stem)
    print('backgrounds', len(MAP_BG), tot // 1024, 'KB')
