"""Run a script the way NVDA 2023.3 (the last for Windows 7) would import it: 32-bit
Python 3.7.9, NVDA's own library.zip and .pyd files only, no site-packages.
usage: py37\\python.exe run37.py target.py [args]"""
import os
import sys

NV = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.environ.get("NVDA_APP", "nvda2023app"))
sys.path[:] = [os.path.join(NV, "library.zip"), NV]
target = os.path.abspath(sys.argv[1])
sys.argv = sys.argv[1:]
sys.path.insert(0, os.path.dirname(target))
g = {"__name__": "__main__", "__file__": target}
exec(compile(open(target, encoding="utf-8").read(), target, "exec"), g)
