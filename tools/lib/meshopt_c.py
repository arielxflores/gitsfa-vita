# meshopt_simplify through ctypes (meshoptimizer 0.23, tools/lib/meshopt/): a drop-in for the meshoptimizer Python
# wrapper's simplify() that make_w3.py / make_dummy.py use. Linux / macOS: built here with the C++ compiler on first use;
# Windows: tools/databuilder/bin/win64/meshopt.dll (or GITSFA_MESHOPT_LIB).
import os, ctypes, subprocess, numpy as np
_H = os.path.dirname(os.path.abspath(__file__)); _L = None
def _lib():
    global _L
    if _L is None:
        p = os.environ.get('GITSFA_MESHOPT_LIB')
        if not p:
            if os.name == 'nt': p = os.path.join(_H, '..', 'databuilder', 'bin', 'win64', 'meshopt.dll')
            else:
                d = os.path.join(os.environ.get('GITSFA_TMP') or os.environ.get('TMPDIR') or '/tmp', 'ctools'); os.makedirs(d, exist_ok=True); p = os.path.join(d, 'libmeshopt023.so')
                src = [os.path.join(_H, 'meshopt', f) for f in ('simplifier.cpp', 'allocator.cpp')]
                if not os.path.exists(p) or any(os.path.getmtime(s) > os.path.getmtime(p) for s in src):
                    t = p + '.%d' % os.getpid()   # (steps run side by side: built under its own name, then renamed)
                    subprocess.run(['c++', '-O2', '-shared', '-fPIC', '-o', t] + src, check=True); os.replace(t, p)
        _L = ctypes.CDLL(p)
        _L.meshopt_simplify.restype = ctypes.c_size_t
        _L.meshopt_simplify.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t,
                                        ctypes.c_size_t, ctypes.c_float, ctypes.c_uint, ctypes.c_void_p]
        _L.meshopt_simplifyWithAttributes.restype = ctypes.c_size_t
        _L.meshopt_simplifyWithAttributes.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t,
                                                      ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                                      ctypes.c_size_t, ctypes.c_float, ctypes.c_uint, ctypes.c_void_p]
    return _L
LOCK_BORDER, SPARSE, ERROR_ABSOLUTE = 1, 2, 4
def simplify_locked(indices, vertex_positions, lock, target_index_count, target_error, options=0):
    """b60: meshopt_simplifyWithAttributes with no attributes and a per-vertex lock (1 = the vertex stays) -> the new indices"""
    I = np.ascontiguousarray(indices, dtype=np.uint32); P = np.ascontiguousarray(vertex_positions, dtype=np.float32); L = np.ascontiguousarray(lock, dtype=np.uint8)
    assert P.ndim == 2 and P.shape[1] == 3 and len(L) == len(P)
    dst = np.zeros(len(I), np.uint32)
    n = _lib().meshopt_simplifyWithAttributes(dst.ctypes.data, I.ctypes.data, len(I), P.ctypes.data, len(P), 12, None, 0, None, 0, L.ctypes.data,
                                              target_index_count, target_error, options, None)
    return dst[:n].copy()
def simplify(destination, indices, vertex_positions, index_count=None, vertex_count=None, vertex_positions_stride=None,
             target_index_count=None, target_error=0.01, options=0, result_error=None):
    indices = np.ascontiguousarray(indices, dtype=np.uint32); vp = np.ascontiguousarray(vertex_positions, dtype=np.float32)
    if index_count is None: index_count = len(indices)
    if vertex_count is None: vertex_count = len(vp)
    if vertex_positions_stride is None: vertex_positions_stride = vp.itemsize * vp.shape[1] if vp.ndim > 1 else vp.itemsize
    if target_index_count is None: target_index_count = index_count // 4
    assert destination.dtype == np.uint32 and destination.flags['C_CONTIGUOUS']
    err = ctypes.c_float(0.0) if result_error is not None else None
    n = _lib().meshopt_simplify(destination.ctypes.data, indices.ctypes.data, index_count, vp.ctypes.data, vertex_count, vertex_positions_stride,
                                target_index_count, target_error, options, ctypes.byref(err) if err is not None else None)
    if result_error is not None: result_error[0] = err.value
    return n
