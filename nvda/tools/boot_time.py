"""Where the Braille Lite's launch time goes: process + boot keys, then the driver's settle."""
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "src"))
from tools import repo_paths             # noqa: E402
from hosts.blazie import Blazie          # noqa: E402
from ssi263.native import SSI263C        # noqa: E402

B = repo_paths.engine_dir("blazie")
for menu in ((), ("punct_none", "numbers_toggle")):
    t0 = time.perf_counter()
    u = Blazie(os.path.join(B, "bns_live.exe"), os.path.join(B, "BL2ENG.BNS"),
               os.path.join(B, "bl2_2003_warm.state"), chip=SSI263C(), menu=menu)
    t1 = time.perf_counter()
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3)
    u.send(b"\x056V")
    u.run(0.05)
    t2 = time.perf_counter()
    u.say(["Hello."])
    first = None
    while True:
        y = u.run(0.03)
        if first is None and any(abs(v) > 0.003 for v in y):
            first = time.perf_counter()
        if not u.busy():
            break
    t3 = time.perf_counter()
    u.close()
    print("menu %-34s spawn+boot keys %.3f s, driver settle %.3f s, 'Hello' first sound %.3f s after say"
          % (menu, t1 - t0, t2 - t1, (first or t3) - t2))
