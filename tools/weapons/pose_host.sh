#!/bin/bash
# builds and runs the viewmodel pose host test (tools/weapons/pose_host.c) on build/data
set -e
H=$(cd "$(dirname "$0")" && pwd); O=${TMPDIR:-/tmp}/wpn_pose_host
gcc -O1 -w -I$H/host -o $O $H/pose_host.c -lm
cd $H/../.. && $O build/data/
