# GITS:FA Vita data builder (Windows package: python\sitecustomize.py, written by make_package.sh / repackage.sh)
import sys, os, builtins, io
# As a normal Python does, put the running script's folder first on sys.path (the embeddable package's python313._pth
# leaves it out, and the port's tools import their neighbours)
if sys.argv and sys.argv[0].lower().endswith('.py') and os.path.isfile(sys.argv[0]):
    sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[0])))
# b122 (the user's first Windows run): text files are written with '\n' line ends, as on Linux - Windows' text mode
# made them '\r\n' (smf.txt, wobj_*.txt, wpn_inspect.txt, ...: other bytes than the reference build's, read by the
# Vita's own parsers). Only writing is changed; reading keeps Python's universal newlines.
_open = builtins.open
def _lf_open(file, mode='r', buffering=-1, encoding=None, errors=None, newline=None, closefd=True, opener=None):
    if newline is None and 'b' not in mode and any(c in mode for c in 'wax'):
        newline = '\n'
    return _open(file, mode, buffering, encoding, errors, newline, closefd, opener)
builtins.open = _lf_open
io.open = _lf_open
