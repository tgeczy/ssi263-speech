# -*- coding: utf-8 -*-
"""SSI-263 speech data: the firmware types, where each one's files come from, and the Super Voice's program fetched
or imported by hand -- the shape of outSPOKEN's "MacinTalk engine" and Panthera's "Mac OS X speech data" dialogs.

Open it and you see every firmware type the add-on knows, one row each, saying whether its files are here: the ones
this add-on carries, and the one no package carries -- Speech Systems' Super Voice, whose disk the Color Computer
Archive keeps (Tomi, 2026-11-23).  For that one: download its disk (only on the button, asked first; the bytes checked
against their pinned sha256), or import a file the user has (the program, the disk image, a zip of either).  Either
way only SUPERTOK.BIN is kept, in NVDA's own configuration folder, once the library has found it the known program
(ssi263_userfw.py).  The driver looks for it every time the types are listed, so the voice appears without a restart.

Everything slow runs on a worker thread and every UI touch goes back through wx.CallAfter (outSPOKEN's lesson: NVDA
keeps speaking while the dialog works).  A close while it works is refused, and said.  Never on a secure screen: the
global plugin does not start there (no network, no file picker as SYSTEM); the driver still finds a file NVDA copied
with the user's configuration.
"""
import os
import threading

import wx
import gui
from logHandler import log

TITLE = "SSI-263 speech data"


def _engine():
    """(ssi263_userfw, the library's path, the unified driver's module) from the installed add-on"""
    from synthDrivers import ssi263
    from synthDrivers._ssi263_unified._ssi263_supervoice import ssi263_userfw, ssi263speech
    return ssi263_userfw, ssi263speech.dll_path(os.path.dirname(ssi263speech.__file__)), ssi263, ssi263speech


def _rows():
    """[(key, row text, details)] for every firmware type, in the driver's order"""
    userfw, _dll, ssi263, _b = _engine()
    out = []
    for fw, (name, _mod, _voices) in ssi263.FIRMWARE_TYPES.items():
        here = ssi263.present_languages(fw)
        if fw == "supervoice":
            state = "installed" if here else "not installed: download it or import it"
            details = ("Speech Systems' Super Voice (1984), a speech cartridge for the Tandy Color Computer, speaking "
                       "its own text-to-speech program, SUPERTOK.BIN, on an emulated 6809.\r\n\r\n"
                       "No package of this project carries the program: its disk is kept by the Color Computer "
                       "Archive. Download fetches the disk from there (or from apps.irata.online, which keeps the same "
                       "disk) and checks it against its known checksum; Import a file takes your own copy: "
                       "SUPERTOK.BIN, the disk image SUPERV.DSK, or a zip holding either. Only SUPERTOK.BIN is kept, "
                       "and only when it is the known program.\r\n\r\nIt is kept in:\r\n%s"
                       % userfw.supervoice_path())
        else:
            state = ("in this add-on" + (" (%d languages)" % len(here) if len(here) > 1 else "")) if here \
                else "not in this copy of the add-on"
            details = "%s's files come with this add-on." % name
        out.append((fw, "%s -- %s" % (name, state), details))
    return out


class SpeechDataDialog(wx.Dialog):
    _instance = None

    @classmethod
    def show(cls, parent=None):
        if cls._instance:
            cls._instance.Raise()
            return
        d = cls(parent or gui.mainFrame)
        cls._instance = d
        d.Show()

    def __init__(self, parent):
        super().__init__(parent, title=TITLE)
        self._busy = False
        main = wx.BoxSizer(wx.VERTICAL)
        main.Add(wx.StaticText(self, label="&Firmware types:"), 0, wx.ALL, 5)
        self.list = wx.ListBox(self, size=(560, 180))
        main.Add(self.list, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 5)
        main.Add(wx.StaticText(self, label="&Details:"), 0, wx.ALL, 5)
        self.details = wx.TextCtrl(self, style=wx.TE_MULTILINE | wx.TE_READONLY, size=(560, 140))
        main.Add(self.details, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 5)
        self.status = wx.StaticText(self, label="")
        main.Add(self.status, 0, wx.ALL, 5)
        buttons = wx.BoxSizer(wx.HORIZONTAL)
        self.download = wx.Button(self, label="&Download the Super Voice...")
        self.importer = wx.Button(self, label="&Import a file...")
        self.folder = wx.Button(self, label="Open the f&older")
        close = wx.Button(self, wx.ID_CLOSE, label="&Close")
        for b in (self.download, self.importer, self.folder, close):
            buttons.Add(b, 0, wx.ALL, 5)
        main.Add(buttons)
        self.SetSizerAndFit(main)
        self.list.Bind(wx.EVT_LISTBOX, lambda evt: self._showDetails())
        self.download.Bind(wx.EVT_BUTTON, self.onDownload)
        self.importer.Bind(wx.EVT_BUTTON, self.onImport)
        self.folder.Bind(wx.EVT_BUTTON, self.onFolder)
        close.Bind(wx.EVT_BUTTON, lambda evt: self.Close())
        self.Bind(wx.EVT_CLOSE, self.onClose)
        self.SetEscapeId(wx.ID_CLOSE)
        self._rows = []
        self.refresh("supervoice")
        self.list.SetFocus()

    def refresh(self, select=None):
        try:
            self._rows = _rows()
        except Exception:
            log.error("SSI-263 speech data: the firmware types could not be listed", exc_info=True)
            self._rows = []
        keep = select or (self._rows[self.list.GetSelection()][0] if 0 <= self.list.GetSelection() < len(self._rows)
                          else None)
        self.list.Set([r[1] for r in self._rows])
        at = next((k for k, r in enumerate(self._rows) if r[0] == keep), 0)
        if self._rows:
            self.list.SetSelection(at)
        self._showDetails()

    def _showDetails(self):
        k = self.list.GetSelection()
        self.details.SetValue(self._rows[k][2] if 0 <= k < len(self._rows) else "")

    def _say(self, text):
        self.status.SetLabel(text)
        try:
            import ui
            ui.message(text)
        except Exception:
            pass

    def _setBusy(self, busy):
        self._busy = busy
        for b in (self.download, self.importer):
            b.Enable(not busy)

    # ---- the work ---------------------------------------------------------------------------------------------------
    def _work(self, what, job):
        """run job() on a worker thread; its result (or error) back here"""
        self._setBusy(True)
        self._say(what)

        def run():
            try:
                result, err = job(), None
            except Exception as e:
                result, err = None, e
                if not e.__class__.__name__ == "FirmwareError":
                    log.error("SSI-263 speech data: %s failed" % what, exc_info=True)
            wx.CallAfter(self._done, result, err)
        threading.Thread(target=run, name="ssi263-speech-data", daemon=True).start()

    def _done(self, result, err):
        self._setBusy(False)
        self.refresh("supervoice")
        if err is not None:
            self._say("Nothing was installed.")
            gui.messageBox("Nothing was installed. %s" % err, TITLE, wx.OK | wx.ICON_ERROR, self)
            return
        state, where = result
        if state == "same":
            text = "The Super Voice's program is already installed, unchanged: nothing was changed."
        else:
            text = ("The Super Voice's program is installed%s. Choose Speech Systems Super Voice as the firmware in "
                    "the SSI-263 synthesizer's voice settings." % (" (from %s)" % where if where else ""))
        self._say(text)
        gui.messageBox(text, TITLE, wx.OK | wx.ICON_INFORMATION, self)

    def onDownload(self, evt):
        userfw, dll, _ssi263, binding = _engine()
        if gui.messageBox("Download Speech Systems' Super Voice disk from the Color Computer Archive (about 28 KB)? "
                          "It is checked against its known checksum, and only its program, SUPERTOK.BIN, is kept, in "
                          "%s. Nothing about you is sent with the request." % userfw.firmware_dir(),
                          TITLE, wx.YES_NO | wx.NO_DEFAULT | wx.ICON_QUESTION, self) != wx.YES:
            return

        def job():
            disk, where = userfw.download_disk()
            return userfw.install_supervoice(disk, lambda d: binding.supervoice_firmware(dll, d)), where
        self._work("Downloading the Super Voice's disk...", job)

    def onImport(self, evt):
        userfw, dll, _ssi263, binding = _engine()
        with wx.FileDialog(self, "Choose the Super Voice's program, its disk image, or a zip holding either",
                           wildcard="Super Voice files (*.bin;*.dsk;*.zip)|*.bin;*.dsk;*.zip|All files (*.*)|*.*",
                           style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as fd:
            if fd.ShowModal() != wx.ID_OK:
                return
            path = fd.GetPath()

        def job():
            data = userfw.read_file(path)
            return userfw.install_supervoice(data, lambda d: binding.supervoice_firmware(dll, d)), \
                os.path.basename(path)
        self._work("Reading %s..." % os.path.basename(path), job)

    def onFolder(self, evt):
        userfw = _engine()[0]
        folder = os.path.dirname(userfw.supervoice_path())
        try:
            os.makedirs(folder, exist_ok=True)
            os.startfile(folder)
        except Exception as e:
            gui.messageBox("The folder could not be opened: %s" % e, TITLE, wx.OK | wx.ICON_ERROR, self)

    def onClose(self, evt):
        if self._busy and evt.CanVeto():
            self._say("Still working; the dialog closes when it is done.")
            evt.Veto()
            return
        SpeechDataDialog._instance = None
        self.Destroy()


def show(parent=None):
    try:
        SpeechDataDialog.show(parent)
    except Exception:
        log.error("SSI-263 speech data: the dialog could not be opened", exc_info=True)
        gui.messageBox("The SSI-263 speech data dialog could not be opened; see NVDA's log.", TITLE,
                       wx.OK | wx.ICON_ERROR)
