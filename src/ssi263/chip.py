"""SSI-263 chip model -- engine v0.

One object is one chip.  The host writes registers (write), watches the A/R
request (request), and pulls audio (run, run_until_request).  Pronunciation is
not the chip's business: it knows phoneme codes, never words.

The vocal tract runs as a discrete-time system at the switched-capacitor clock
fc = XCK / (2 (256 - FF)).  Section coefficients depend only on capacitor
ratios, so the filter-frequency register changes the tract's sample rate and
nothing else (A: FF sets "the frequency of all vocal tract filters").  The
sample-and-hold output is held for one fc period and area-sampled to the host
rate, which is where clock images come from.

Three event types stay distinct (ISSCC; message section 26.1): host register
writes, transition-counter updates, and the 4-bit latch values the analog
sections actually see.  Their latencies are unknown; here the counters move
every fc sample and the latches follow at once.

Register map, by address and bit (document labels only in comments):
  R0 [7:6] duration (mode on CTL 1->0)  [5:0] phoneme code
  R1 [7:3] pitch target I10..I6          [2:0] glide field (A: I5..I3; ISSCC: TR2..TR0)
  R2 [7:4] speech rate R3..R0            [3] I11   [2:0] I2..I0
  R3 [7] CTL  [6:4] articulation (A: T2..T0; ISSCC: RA2..RA0)  [3:0] amplitude
  R4 [7:0] FF, filter clock
"""
import math

from . import dsp as _dsp
from .params import Params
from .rom import Rom

FIELDS = ("F1", "F2", "F3", "NAS", "VA", "FA")
MODE_NAMES = {3: "phoneme timing, transitioned inflection",
              2: "phoneme timing, immediate inflection",
              1: "frame timing, immediate inflection",
              0: "A/R disabled"}
PENDING_MAX = 256            # writes held for a stop's end (release_lookahead); more flush at once


def _open(entry):
    """A phoneme a stop may release into early: closure-clear, voiced and noiseless (vowels,
    R L W M N, KV).  Not PA, not a fricative, not a closure (params: release_lookahead)."""
    return bool(entry["closure_clear"]) and entry["VA"] > 0 and entry["FA"] == 0


class Resonator:
    """One SC low-pass formant section, unity DC gain.

    numerator 'bilinear': H = g (1 + z^-1)^2 / (1 - a1 z^-1 + a2 z^-2), a true
    low-pass with its null at fc/2 (as bilinear-designed SC sections have);
    'allpole': H = g / (1 - a1 z^-1 + a2 z^-2).
    """
    __slots__ = ("g", "a1", "a2", "y1", "y2", "x1", "x2", "zeros")

    def __init__(self, zeros=True):
        self.g = 1.0
        self.a1 = self.a2 = 0.0
        self.y1 = self.y2 = self.x1 = self.x2 = 0.0
        self.zeros = zeros

    def set_theta(self, theta, bw_ratio):
        r = math.exp(-math.pi * bw_ratio)
        self.a1 = 2.0 * r * math.cos(theta)
        self.a2 = r * r
        self.g = (1.0 - self.a1 + self.a2) / (4.0 if self.zeros else 1.0)

    def __call__(self, x):
        if self.zeros:
            y = self.g * (x + 2.0 * self.x1 + self.x2) + self.a1 * self.y1 - self.a2 * self.y2
            self.x2, self.x1 = self.x1, x
        else:
            y = self.g * x + self.a1 * self.y1 - self.a2 * self.y2
        self.y2, self.y1 = self.y1, y
        return y


class Bandpass:
    """Two-pole band-pass, zeros at DC and fc/2, unit gain at the peak (noise shaper)."""
    __slots__ = ("g", "a1", "a2", "y1", "y2", "x1", "x2")

    def __init__(self, ratio, bw_ratio):
        r = math.exp(-math.pi * bw_ratio)
        th = 2 * math.pi * ratio
        self.a1 = 2.0 * r * math.cos(th)
        self.a2 = r * r
        z = complex(math.cos(th), math.sin(th))
        h = (1 - z ** -2) / (1 - self.a1 * z ** -1 + self.a2 * z ** -2)
        self.g = 1.0 / abs(h)
        self.y1 = self.y2 = self.x1 = self.x2 = 0.0

    def __call__(self, x):
        y = self.g * (x - self.x2) + self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1 = self.x1, x
        self.y2, self.y1 = self.y1, y
        return y


class SSI263:
    def __init__(self, params=None, rom=None, out_rate=44100, dsp=None):
        self.p = params if isinstance(params, Params) else Params(params)
        self.rom = rom or Rom.load()
        self.out_rate = float(out_rate)
        self.dsp = _dsp.get(dsp)     # output stage: "numpy" (reference) or "c" (the add-ons)
        self.xck = float(self.p["xck_hz"])
        self.regs = [0x00, 0x00, 0x00, 0x80, 0x00]   # A: CTL set at power-up
        self.mode = None
        self.timer_done = False      # internal phoneme/frame timer completion
        self.phoneme = 0
        self.elapsed = 0.0
        self.duration = 0.0
        self.target = {f: 0 for f in FIELDS}
        self.cur = {f: 0.0 for f in FIELDS}
        self.latch = {f: -1 for f in FIELDS}
        self.amp_target = 0
        self.amp_cur = 0.0
        self.closing = False
        self.releases = False
        self.released = False        # this closure has released before its end
        self.onto_silence = False    # this phoneme loaded onto a silent tract (closure_onto_silence)
        self.early_req = False       # A/R raised ahead of a releasing stop's end (release_lookahead)
        self.pending = []            # (addr, value) held while early_req, applied at the stop's end
        self.apply_due = False       # the stop's duration is up: apply `pending` at the next tick
        self.clo = 1.0               # closure gain on VOL
        self.w2, self.w5 = self.p["noise_into_f2"], self.p["noise_into_f5"]
        self.g2 = self.g5 = 0.0      # per-path noise levels (noise_route_mode 'per_path')
        self.imm = 0                 # I11 and I2..I0: always immediate
        self.trans = 0.0             # I10..I3 part, transitioned in mode 3
        self.trans_target = 0
        self.snap_pitch = False      # host request: the next CHANGE of pitch target lands at once
        self.phase = 0.0
        self.pending_pulse = 0.0
        self.lfsr = 1
        self.noise_phase = 0.0
        self.noise_val = 1.0
        self.tick = 0                # tract samples since power-up
        zeros = self.p["section_numerator"] == "bilinear"
        self.sec = [Resonator(zeros) for _ in range(5)]
        self.shaper = Resonator(zeros)
        shp = self.p["noise_shaper"]
        self.shaper_on = shp != "flat"
        if self.shaper_on and shp[0] == "bp":
            self.shaper = Bandpass(shp[1], shp[2])
        elif self.shaper_on:
            self.shaper.set_theta(2 * math.pi * shp[0], shp[1])
        self.hp_x1 = self.hp_y1 = 0.0
        self.out_pos = 0.0
        self.out_acc = 0.0
        self.pend_v = 0.0            # held value not yet fully emitted (host block boundary)
        self.pend_rem = 0.0
        # host-rate output: area-sample the held S/H value at os x the host rate,
        # then low-pass and decimate, as a DAC/ADC chain would (no image folding)
        self.os = int(self.p["output_oversample"])
        self.fir = self.dsp.firwin(48 * self.os + 1, self.p["output_lowpass_hz"], fs=self.out_rate * self.os)
        self.dec = self.dsp.decimator(self.fir, self.os)
        self.log = []                # (time_s, event) for sidecars
        self._time = 0.0
        self._set_fixed_sections()
        self._latch_all(force=True)

    # ---- host side -------------------------------------------------------------
    @property
    def powered(self):
        return not (self.regs[3] & 0x80)

    @property
    def fc(self):
        return self.xck / (2.0 * (256 - self.regs[4]))

    @property
    def request(self):
        """A/R asserted: the chip requests data.  The pin is active-low (A/R*), so True
        here means the physical pin is LOW.  Only when powered and in a mode with A/R
        enabled (mode 0 is 'A/R disabled'); the timer itself runs regardless.  Under
        release_lookahead it also rises ahead of a releasing stop's end, and drops once the
        next phoneme is held."""
        if not (self.powered and self.mode):
            return False
        if self.apply_due or any(a == 0 for a, _ in self.pending):
            return False
        return self.timer_done or self.early_req

    @property
    def time(self):
        """Chip time aligned with the audio already returned to the host."""
        return self._time - self.pend_rem

    def write(self, addr, value):
        """addr = RS2..RS0 as on the bus: 0-3 select R0-R3; RS2 = 1 (4-7) selects R4."""
        addr &= 7
        if addr >= 4:
            addr = 4
        value &= 0xFF
        if self.apply_due:
            self._apply_pending()
        if self.early_req and not self.timer_done:
            if addr == 3 and (value & 0x80):
                self._apply_pending()          # power down: nothing waits for the stop's end
            elif len(self.pending) < PENDING_MAX:
                self.pending.append((addr, value))
                self.log.append((round(self.time, 6), "latch w%d=%02X" % (addr, value)))
                return
            else:
                self._apply_pending()
        old = self.regs[addr]
        self.regs[addr] = value
        t = self.time
        self.log.append((round(t, 6), "w%d=%02X" % (addr, value)))
        if addr == 0:
            if self.powered and self.mode is not None:
                self._load_phoneme()
        elif addr == 1:
            self._update_inflection()
        elif addr == 2:
            self._update_inflection()
        elif addr == 3:
            if (old & 0x80) and not (value & 0x80):
                self.mode = self.regs[0] >> 6
                self.log.append((round(t, 6), "mode %d: %s" % (self.mode, MODE_NAMES[self.mode])))
                self._update_inflection()
            self.amp_target = value & 0x0F

    def _apply_pending(self):
        """The writes held for a releasing stop's end land now, in order."""
        pend, self.pending = self.pending, []
        self.early_req = False
        self.apply_due = False
        for a, v in pend:
            self.write(a, v)

    def _pending_entry(self):
        for a, v in reversed(self.pending):
            if a == 0:
                return self.rom.entry(v & 0x3F)
        return None

    def _pending_open(self):
        for a, v in reversed(self.pending):
            if a == 0:
                return _open(self.rom.entry(v & 0x3F))
        return False

    def _load_phoneme(self):
        held = (self.p["release_lookahead"] and self.p["late_release_burst"]
                and self.releases and not self.released and self.clo <= 0.5)
        prev_fa, pw2, pw5 = self.target["FA"], self.w2, self.w5
        # nothing sounding as this phoneme loads: a PA with the amplitude down, or voice and
        # noise both faded out (params: closure_onto_silence)
        self.onto_silence = (self.amp_cur < 0.5
                             or (self.latch["VA"] == 0 and self.latch["FA"] == 0 and self.g2 + self.g5 < 0.5))
        self.phoneme = self.regs[0] & 0x3F
        e = self.rom.entry(self.phoneme)
        for f in FIELDS:
            self.target[f] = e[f]
        self.closing = bool(self.p["closure_enable"]) and not e["closure_clear"]
        self.releases = self.closing and (e["class1"] == 1 or not self.p["closure_release_b01"])
        if self.p["noise_route"] == "b02":
            self.w2, self.w5 = self.p["noise_route_b02"][e["class2"]]
        if held and not self.closing:
            # the stop was held to its end: it releases here, its own noise the burst
            self.g2 = max(self.g2, prev_fa * pw2)
            self.g5 = max(self.g5, prev_fa * pw5)
        self.released = False
        self.early_req = False
        self.apply_due = False
        self.pending = []
        self.elapsed = 0.0
        self.timer_done = False
        rate = self.regs[2] >> 4
        frame = self.p["frame_xck_cycles"] * (16 - rate) / self.xck
        if self.mode == 1:
            self.duration = frame
        else:
            self.duration = frame * (4 - (self.regs[0] >> 6))

    def _update_inflection(self):
        r1, r2 = self.regs[1], self.regs[2]
        self.imm = ((r2 >> 3) & 1) * 2048 + (r2 & 7)
        if self.mode == 3:
            new = (r1 >> 3) * 64
            if self.snap_pitch and new != self.trans_target:
                # A host feature, not chip behaviour: NVDA's capital-letter pitch must be
                # heard on the letter, and the chip's glide would take most of it.
                self.trans = float(new)
                self.snap_pitch = False
            self.trans_target = new
        else:
            self.trans_target = r1 * 8
            self.trans = float(self.trans_target)

    # ---- running ---------------------------------------------------------------
    def run(self, seconds):
        n = int(round(seconds * self.out_rate))
        return self._decimate(self._run(max_out=n * self.os))

    def skip(self, seconds):
        """Advance time with no sound, for a host that is discarding the output anyway
        (fast-forwarding a flush).  The timer, A/R, transition counters, amplitude and
        glide move as they would have; filter ringing is let die.  Without this the
        next utterance opened with the tail of the skipped phoneme."""
        if seconds <= 0:
            return
        if self.apply_due:
            self._apply_pending()
        p = self.p
        end_now = False
        if self.powered and self.mode is not None:
            left = self.duration - self.elapsed
            early_left = None
            if (p["release_lookahead"] and self.releases and self.mode
                    and not self.early_req and not self.timer_done):
                fr = p["frame_xck_cycles"] * (16 - (self.regs[2] >> 4)) / self.xck
                sc = (self.duration / (4.0 * fr)) if p["closure_timing"] == "fraction" and fr > 0 else 1.0
                early_left = (self.duration - p["closure_release_frames"] * fr * sc
                              - p["lookahead_lead_frames"] * fr * sc - self.elapsed)
            if early_left is not None and seconds >= early_left:
                seconds = max(early_left, 0.0)
                self.early_req = True
                self.log.append((round(self._time + seconds, 6), "request"))
            elif not self.timer_done and seconds >= left:
                seconds = max(left, 0.0)
                self.timer_done = True
                if not self.early_req:
                    self.log.append((round(self._time + seconds, 6), "request" if self.mode else "timer"))
                end_now = bool(self.pending)
            self.elapsed += seconds
            frame = p["frame_xck_cycles"] * (16 - (self.regs[2] >> 4)) / self.xck
            step = p["art_codes_per_frame"][(self.regs[3] >> 4) & 7] * seconds / frame
            for f in self.cur:
                st = step * p["art_amp_mult"] if f in ("VA", "FA") else step
                d = self.target[f] - self.cur[f]
                self.cur[f] = self.target[f] if abs(d) <= st else self.cur[f] + math.copysign(st, d)
            self.g2 = self.target["FA"] * self.w2
            self.g5 = self.target["FA"] * self.w5
            if self.duration > 0:
                astep = 15.0 * p["amp_slew_per_phoneme"] * seconds / self.duration
                d = self.amp_target - self.amp_cur
                self.amp_cur = self.amp_target if abs(d) <= astep else self.amp_cur + math.copysign(astep, d)
            self.clo = 0.0 if self.closing else 1.0
            d = self.trans_target - self.trans
            if d:
                rate = self.regs[2] >> 4
                g = (p["glide_field_mult"][self.regs[1] & 7] * self.xck
                     / (p["glide_xck_cycles_per_count"] * (16 - rate))) * seconds
                self.trans = self.trans_target if abs(d) <= g else self.trans + math.copysign(g, d)
            self._latch_all()
        if True:                          # skipped output is discarded: no ringing or held samples survive it
            for sec in list(self.sec) + [self.shaper]:
                for attr in ("y1", "y2", "x1", "x2"):
                    if hasattr(sec, attr):
                        setattr(sec, attr, 0.0)
            self.hp_x1 = self.hp_y1 = 0.0
            self.pending_pulse = 0.0
            # the host-rate stage still holds audio from before the skip: drop it too
            self.dec.reset()
            self.pend_v = self.pend_rem = 0.0
            self.out_acc = self.out_pos = 0.0
        self._time += seconds
        if end_now:
            self._apply_pending()

    def run_until_request(self, max_seconds=5.0):
        return self._decimate(self._run(max_out=int(max_seconds * self.out_rate) * self.os,
                                        stop_on_request=True))

    def _decimate(self, fine):
        if not len(fine):
            return fine
        return self.dec.process(fine)

    def _set_fixed_sections(self):
        bw = self.p["bw_ratio"]
        self.sec[3].set_theta(2 * math.pi * self.p["f4_ratio"], bw[3])
        self.sec[4].set_theta(2 * math.pi * self.p["f5_ratio"], bw[4])
        self._coef = {}

    def _theta(self, name, code):
        K = self.p[name.lower() + "_K"]
        C0 = self.p[name.lower() + "_C0"]
        w = self.p[name.lower() + "_w"]
        cap = C0 + sum(wi for i, wi in enumerate(w) if (code >> i) & 1)
        s = min(0.999, math.sqrt(max(K * cap, 0.0)) / 2.0)
        return 2.0 * math.asin(s)

    def _latch_all(self, force=False):
        q = round if self.p["latch_quantize"] == "round" else math.floor
        bw = self.p["bw_ratio"]
        changed = False
        for f in FIELDS:
            c = int(q(self.cur[f]))
            c = 0 if c < 0 else 15 if c > 15 else c
            if c != self.latch[f] or force:
                self.latch[f] = c
                changed = True
        if changed:
            self.sec[0].set_theta(self._theta("F1", self.latch["F1"]), bw[0])
            b2 = bw[1] * (1.0 + self.p["nas_f2_bw_gain"] * self.latch["NAS"])
            self.sec[1].set_theta(self._theta("F2", self.latch["F2"]), b2)
            self.sec[2].set_theta(self._theta("F3", self.latch["F3"]), bw[2])

    def _run(self, max_out, stop_on_request=False):
        p = self.p
        out = []
        to = 1.0 / (self.out_rate * self.os)
        art = p["art_codes_per_frame"]
        mult = p["glide_field_mult"]
        amp_mult = p["art_amp_mult"]
        per_path = p["noise_route_mode"] == "per_path"
        hold_noise = p["closure_noise_at_release"]
        f5_post = p["noise_f5_point"] == "output"
        burst_hold = p["burst_hold_frames"]
        frac_timing = p["closure_timing"] == "fraction"
        clo_floor = 0.0 if p["closure_floor_db"] is None else 10 ** (p["closure_floor_db"] / 20.0)
        fractional = p["pulse_place"] == "fractional"
        ng = p["noise_gain"]
        hp_r = 1.0 - 2.0 * math.pi * p["hp_ratio"]
        # carrier level is relative to a loud vowel, which output_gain puts at RMS 0.1
        c8 = 0.1 * 10 ** (p["carrier_rel_db"] / 20.0)
        c4 = c8 * 10 ** (p["carrier_h2_db"] / 20.0)
        carrier_off = p["carrier_when_powered_down"]
        gain = p["output_gain"]
        clo_va = p["closure_target"] == "va"
        look = p["release_lookahead"]
        lead = p["lookahead_lead_frames"]
        precharge = p["fricative_precharge"]
        s1, s2, s3, s4, s5 = self.sec
        sh = self.shaper
        lfsr_top = p["lfsr_bits"] - 1
        lfsr_mask = (1 << p["lfsr_bits"]) - 1
        t = self._time
        # finish the held sample a previous call stopped inside
        rem, v = self.pend_rem, self.pend_v
        while rem > 0.0 and len(out) < max_out:
            take = min(rem, to - self.out_pos)
            self.out_acc += v * take
            self.out_pos += take
            rem -= take
            if self.out_pos >= to - 1e-15:
                out.append(self.out_acc / to)
                self.out_acc = 0.0
                self.out_pos = 0.0
        self.pend_rem = rem
        while len(out) < max_out:
            if self.apply_due:
                self._apply_pending()          # as a host writing at this tick boundary would
            if stop_on_request and self.request:
                break
            fc = self.xck / (2.0 * (256 - self.regs[4]))
            dt = 1.0 / fc
            powered = not (self.regs[3] & 0x80)
            v = 0.0
            if powered:
                # -- controller: timer, transition counters, glide --------------
                if self.mode is not None:
                    self.elapsed += dt
                    if not self.timer_done and self.elapsed >= self.duration:
                        self.timer_done = True
                        if not self.early_req:
                            self.log.append((round(t, 6), "request" if self.mode else "timer"))
                        if self.pending:
                            self.apply_due = True
                frame = p["frame_xck_cycles"] * (16 - (self.regs[2] >> 4)) / self.xck
                step = art[(self.regs[3] >> 4) & 7] * dt / frame
                for f in FIELDS:
                    d = self.target[f] - self.cur[f]
                    if d:
                        st = step * amp_mult if f in ("VA", "FA") else step
                        self.cur[f] = self.target[f] if abs(d) <= st else self.cur[f] + math.copysign(st, d)
                if self.duration > 0:
                    astep = 15.0 * p["amp_slew_per_phoneme"] * dt / self.duration
                    d = self.amp_target - self.amp_cur
                    if d:
                        self.amp_cur = self.amp_target if abs(d) <= astep else self.amp_cur + math.copysign(astep, d)
                d = self.trans_target - self.trans
                if d:
                    rate = self.regs[2] >> 4
                    gstep = (mult[self.regs[1] & 7] * self.xck
                             / (p["glide_xck_cycles_per_count"] * (16 - rate))) * dt
                    self.trans = self.trans_target if abs(d) <= gstep else self.trans + math.copysign(gstep, d)
                cstep = dt * 1000.0 / p["closure_ramp_ms"]
                scale = (self.duration / (4.0 * frame)) if frac_timing and frame > 0 else 1.0
                rel = p["closure_release_frames"] * frame * scale if self.releases else 0.0
                cdel = p["closure_delay_frames"] if self.releases else p["closure_hold_delay_frames"]
                if self.onto_silence and p["closure_onto_silence"]:
                    cdel = 0.0
                rel_now = rel > 0 and self.elapsed >= self.duration - rel
                if look and self.releases:
                    # ask for the next phoneme ahead of the release point; release only into an
                    # open one (params: release_lookahead)
                    if (self.mode and not self.early_req and not self.timer_done
                            and self.elapsed >= self.duration - rel - lead * frame * scale):
                        self.early_req = True
                        self.log.append((round(t, 6), "request"))
                    if rel_now and not self.released:
                        self.released = self._pending_open()
                    rel_now = rel_now and self.released
                if (self.closing and self.elapsed >= cdel * frame * scale and not rel_now):
                    self.clo = max(clo_floor, self.clo - cstep)
                elif (p["closure_reopen"] or not self.closing or rel_now) and self.clo < 1.0:
                    self.clo = min(1.0, self.clo + cstep)
                self._latch_all()
                la = self.latch
                # -- sources -------------------------------------------------------
                I = self.imm + self.trans
                period = p["pitch_xck_div"] * (4096.0 - I) / self.xck
                inc = dt / period
                pulse = self.pending_pulse
                self.pending_pulse = 0.0
                ph = self.phase + inc
                n = int(ph)
                if n:
                    for k in range(1, n + 1):
                        if fractional:
                            a = (k - self.phase) / inc     # where in this tick crossing k falls
                            pulse += 1.0 - a
                            self.pending_pulse += a
                        else:
                            pulse += 1.0
                    ph -= n
                self.phase = ph
                x = pulse * la["VA"] / 15.0
                if clo_va:
                    x *= self.clo
                self.noise_phase += p["noise_clock_ratio"]
                while self.noise_phase >= 1.0:
                    self.noise_phase -= 1.0
                    bit = ((self.lfsr >> lfsr_top) ^ (self.lfsr >> (lfsr_top - 1))) & 1
                    self.lfsr = ((self.lfsr << 1) | bit) & lfsr_mask
                    self.noise_val = 1.0 if bit else -1.0
                if per_path:
                    astp = step * amp_mult
                    t2, t5 = self.target["FA"] * self.w2, self.target["FA"] * self.w5
                    rel_noise = self.elapsed >= self.duration - p["closure_release_frames"] * frame * scale
                    if look and self.releases:
                        rel_noise = rel_noise and self.released
                    if hold_noise and self.closing and (not self.releases or not rel_noise):
                        t2 = t5 = 0.0
                        if (look and precharge and self.releases and not self.released
                                and self.elapsed >= self.duration - p["closure_release_frames"] * frame * scale):
                            # the next phoneme is a fricative: its noise builds behind the closed gate
                            pe = self._pending_entry()
                            if pe is not None and pe["closure_clear"] and pe["FA"] > 0:
                                w = p["noise_route_b02"][pe["class2"]] if p["noise_route"] == "b02" else (self.w2, self.w5)
                                t2, t5 = pe["FA"] * w[0], pe["FA"] * w[1]
                    elif (burst_hold is not None and self.closing and self.releases
                          and self.elapsed >= self.duration - (p["closure_release_frames"] - burst_hold) * frame * scale):
                        t2, t5 = t2 * p["burst_tail_level"], t5 * p["burst_tail_level"]
                    d = t2 - self.g2
                    self.g2 = t2 if abs(d) <= astp else self.g2 + math.copysign(astp, d)
                    d = t5 - self.g5
                    self.g5 = t5 if abs(d) <= astp else self.g5 + math.copysign(astp, d)
                    nz = self.noise_val * ng / 15.0
                    n2, n5 = self.g2, self.g5
                else:
                    nz = self.noise_val * ng * la["FA"] / 15.0
                    n2, n5 = self.w2, self.w5
                if self.shaper_on:
                    nz = sh(nz)
                # -- the cascade: F1 -> F2 (+noise) -> F3 -> F4 -> F5 (+noise) -------
                if f5_post:
                    x = s5(s4(s3(s2(s1(x) + n2 * nz)))) + n5 * nz
                else:
                    x = s5(s4(s3(s2(s1(x) + n2 * nz))) + n5 * nz)
                # -- high-pass with volume, then S/H ----------------------------------
                hp = x - self.hp_x1 + hp_r * self.hp_y1
                self.hp_x1, self.hp_y1 = x, hp
                amp = round(self.amp_cur) / 15.0
                v = gain * hp * amp * (1.0 if clo_va else self.clo)
            if powered or carrier_off:
                k = self.tick & 7
                v += c8 * math.sin(2 * math.pi * k / 8.0) + c4 * math.sin(2 * math.pi * k / 4.0)
            self.tick += 1
            # -- area-sample the held value to the host rate ------------------------
            rem = dt
            while rem > 0.0:
                if len(out) >= max_out:
                    self.pend_v, self.pend_rem = v, rem
                    break
                take = min(rem, to - self.out_pos)
                self.out_acc += v * take
                self.out_pos += take
                rem -= take
                if self.out_pos >= to - 1e-15:
                    out.append(self.out_acc / to)
                    self.out_acc = 0.0
                    self.out_pos = 0.0
            t += dt
            self._time = t
        return self.dsp.fine(out)
