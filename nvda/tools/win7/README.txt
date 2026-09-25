Old-NVDA test environment (Windows 7 era: NVDA 2021.1 and 2023.3 are 32-bit Python 3.7.9).

py37\         python-3.7.9-embed-win32.zip from python.org, unzipped
nvda2021app\  nvda_2021.1.exe from download.nvaccess.org/releases/2021.1/, opened with
              7-Zip ("7z x nvda_2021.1.exe"), the $PLUGINSDIR\app folder renamed
nvda2023app\  the same from nvda_2023.3.4.exe (the last NVDA for Windows 7)

run37.py runs a script against one NVDA's own library.zip and .pyd files only (no
site-packages), which is how an add-on's code is imported there:

    set NVDA_APP=nvda2021app
    tools\win7\py37\python.exe tools\win7\run37.py tools\driver_sim.py blazie <abs path to nvda2021app> tag
    tools\win7\py37\python.exe tools\win7\run37.py ..\src\tools\check_native_core.py

The NVDA folders are NV Access's binaries: for local testing only, never into a repository.
