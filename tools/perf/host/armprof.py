#!/usr/bin/env python3
# armprof.py <binary> <args...>: runs the static ARM binary under qemu-arm-static with one instruction per TB,
# histograms the executed PCs, maps them to functions (objdump) and estimates Cortex-A9 issue cycles
# (crude: 1 per instruction, VDIV/VSQRT.F32 15, NEON quad-register ops 2, core<->VFP moves 2)
import subprocess, sys, re, collections
binary = sys.argv[1]
dis = subprocess.run(["arm-linux-gnueabihf-objdump", "-d", "--no-show-raw-insn", binary], capture_output=True, text=True).stdout
ins = {}; fn = None
for line in dis.splitlines():
    m = re.match(r"^([0-9a-f]+) <(.+)>:", line)
    if m: fn = m.group(2); continue
    m = re.match(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*)", line)
    if m and fn: ins[int(m.group(1), 16)] = (fn, m.group(2), m.group(3))
def cost(mn, ops):
    if mn.startswith(("vdiv", "vsqrt")): return 15
    if mn.startswith("v") and re.search(r"\bq\d+", ops): return 2
    if mn == "vmov" and re.search(r"\br\d+|\bip\b|\blr\b", ops) and re.search(r"\b[sd]\d+", ops): return 2
    return 1
p = subprocess.Popen(["qemu-arm-static", "-one-insn-per-tb", "-d", "exec,nochain", "-D", "/dev/stdout", binary] + sys.argv[2:], stdout=subprocess.PIPE, text=True)
hist = collections.Counter(); out = []
for line in p.stdout:
    if line.startswith("Trace"):
        m = line.split("[", 1)[1].split("/"); hist[int(m[1], 16)] += 1
    else: out.append(line)
p.wait()
fi = collections.Counter(); fc = collections.Counter(); kinds = collections.Counter()
for a, n in hist.items():
    f, mn, ops = ins.get(a, ("?", "?", ""))
    f = re.sub(r"\.(constprop|isra|part)\.\d+", "", f)
    fi[f] += n; c = cost(mn, ops) * n; fc[f] += c
    if mn.startswith(("vdiv", "vsqrt")): kinds["div/sqrt"] += n
    elif mn.startswith("v"): kinds["vfp/neon"] += n
    else: kinds["core"] += n
print("".join(out).strip())
print("total instructions %d, est. issue cycles %d" % (sum(fi.values()), sum(fc.values())))
print("kinds: " + ", ".join("%s %d" % kv for kv in kinds.most_common()))
for f, n in fi.most_common(14): print("  %-28s %10d ins %10d cyc" % (f, n, fc[f]))
