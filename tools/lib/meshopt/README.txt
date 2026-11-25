meshoptimizer 0.23 (https://github.com/zeux/meshoptimizer, MIT licence: LICENSE.txt, from the end of meshoptimizer.h),
the two files the builder needs (simplifier.cpp, allocator.cpp), as vendored by the Rust crate meshopt 0.5.0.
tools/lib/meshopt_c.py builds / loads them as a small shared library (Windows: tools/databuilder/bin/win64/meshopt.dll) and
calls meshopt_simplify exactly as the meshoptimizer Python wrapper (0.2.30a0) the shipped models were made with did
(checked: the same indices for all 28 calls of make_w3.py and make_dummy.py).
