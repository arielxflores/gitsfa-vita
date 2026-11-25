#!/usr/bin/env python3
# logcut.py boot.log [out]: this run's part of a boot.log written in place (b60, engine/src/alog.h): the text up to the
# end line "@@@ END OF THIS RUN'S LOG" (normal play), or up to the newlines a test run blanks the older text with; a log
# with neither is passed through.
import sys
d = open(sys.argv[1], 'rb').read(); i = d.find(b"\n@@@ END OF THIS RUN'S LOG")
d = d[:i + 1] if i >= 0 else d.rstrip(b'\n') + b'\n'
(open(sys.argv[2], 'wb') if len(sys.argv) > 2 else sys.stdout.buffer).write(d)
