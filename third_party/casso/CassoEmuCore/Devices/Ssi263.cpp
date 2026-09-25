#include "Pch.h"

#include "Devices/Ssi263.h"

// Per-phoneme acoustic targets, indexed by phoneme code $00-$3F.
//
// Derived from the chip's own parameter ROM, read off the visual6502 die
// photographs of the SSI 263P. Each phoneme stores six 4-bit fields (F1, F2,
// F3 filter codes, vocal and fricative amplitudes kept separate, nasal
// coupling) plus voiced/fricative/closure flags; the extraction lives under
// specs. Filter codes become Hz through the SC-01A's measured capacitor
// network (the 2007 decap, as modeled by MAME): the two chips demonstrably
// share one code scale -- 22 of 46 name-matched phonemes carry identical
// formant codes -- so the predecessor's silicon-true DAC curves replace the
// earlier literature-fitted lines. The chip's two source amplitudes stay
// SEPARATE -- vocal and fricative -- because their ratio is what tells one
// sound from another: Z is frication 10 against voicing 1, while L is
// frication 0 against voicing 7, and their formants are nearly identical.
// The two closure-hold phonemes keep both levels 0 because the closure ramp
// hardware applies is not modeled here. Still replaceable wholesale via
// SetFormantTable when better-calibrated data arrives.
static constexpr Ssi263PhonemeSpec  s_kPhonemes[Ssi263::kPhonemeCount] =
{
    {    0,    0,    0, false, false, 0.00f, 0.00f },   // 00 PA   (pause)
    {  314, 2330, 2756, true,  false, 0.80f, 0.00f },   // 01 E    meet
    {  446, 2330, 2677, true,  false, 0.67f, 0.00f },   // 02 E1   bent
    {  256, 2257, 2756, true,  false, 0.73f, 0.00f },   // 03 Y    before
    {  314, 2174, 2511, true,  false, 0.40f, 0.00f },   // 04 YI   year
    {  365, 2330, 2756, true,  false, 0.80f, 0.00f },   // 05 AY   please
    {  256, 2408, 2832, true,  false, 0.60f, 0.00f },   // 06 IE   any
    {  446, 2007, 2598, true,  false, 0.53f, 0.00f },   // 07 I    six
    {  482, 2096, 2511, true,  false, 0.53f, 0.00f },   // 08 A    made
    {  482, 1922, 2425, true,  false, 0.53f, 0.00f },   // 09 AI   care
    {  577, 1823, 2511, true,  false, 0.53f, 0.00f },   // 0A EH   nest
    {  605, 1922, 2511, true,  false, 0.53f, 0.00f },   // 0B EH1  belt
    {  683, 1922, 2511, true,  false, 0.40f, 0.00f },   // 0C AE   dad
    {  731, 1730, 2511, true,  false, 0.40f, 0.00f },   // 0D AE1  after
    {  731, 1261, 2511, true,  false, 0.40f, 0.00f },   // 0E AH   got
    {  731, 1387, 2511, true,  false, 0.47f, 0.00f },   // 0F AH1  father
    {  683, 1106, 2425, true,  false, 0.40f, 0.00f },   // 10 AW   office
    {  516,  943, 2511, true,  false, 0.53f, 0.00f },   // 11 O    store
    {  446,  943, 2511, true,  false, 0.60f, 0.00f },   // 12 OU   boat
    {  546, 1106, 2425, true,  false, 0.53f, 0.00f },   // 13 OO   look
    {  365, 1620, 2425, true,  false, 0.67f, 0.00f },   // 14 IU   you
    {  406, 1387, 2425, true,  false, 0.60f, 0.00f },   // 15 IU1  could
    {  365,  943, 2244, true,  false, 0.67f, 0.00f },   // 16 U    tune
    {  256,  722, 2142, true,  false, 0.67f, 0.00f },   // 17 U1   cartoon
    {  546, 1387, 2511, true,  false, 0.67f, 0.00f },   // 18 UH   wonder
    {  605, 1261, 2511, true,  false, 0.53f, 0.00f },   // 19 UH1  love
    {  657, 1261, 2511, true,  false, 0.40f, 0.00f },   // 1A UH2  what
    {  657, 1514, 2511, true,  false, 0.47f, 0.00f },   // 1B UH3  nut
    {  482, 1387, 1696, true,  false, 0.53f, 0.00f },   // 1C ER   bird
    {  365,  943, 1424, true,  false, 0.53f, 0.00f },   // 1D R    roof
    {  314, 1261, 1822, true,  false, 0.53f, 0.00f },   // 1E R1   rug
    {  516, 1620, 2336, true,  false, 0.40f, 0.00f },   // 1F R2   mutter
    {  365, 1261, 2756, true,  false, 0.47f, 0.00f },   // 20 L    lift
    {  256, 1514, 2832, true,  false, 0.53f, 0.00f },   // 21 L1   play
    {  446,  943, 2756, true,  false, 0.60f, 0.00f },   // 22 LF   fall
    {  365,  722, 2336, true,  false, 0.53f, 0.00f },   // 23 W    water
    {  256, 1261, 2598, true,  false, 0.53f, 0.00f },   // 24 B    bag
    {  256, 1922, 2756, true,  false, 0.53f, 0.00f },   // 25 D    paid
    {  365, 2007, 2244, true,  false, 0.67f, 0.00f },   // 26 KV   tag
    {  406, 1106, 2244, false, true,  0.00f, 1.00f },   // 27 P    pen
    {  406, 1922, 2756, false, true,  0.00f, 1.00f },   // 28 T    tart
    {  365, 2007, 2244, false, true,  0.00f, 0.27f },   // 29 K    kit
    {  516, 1922, 2598, true,  false, 0.40f, 0.00f },   // 2A HV   (hold vocal)
    {  516, 1922, 2598, false, false, 0.00f, 0.00f },   // 2B HVC  (hold vocal closure)
    {  516, 1922, 2598, false, true,  0.00f, 0.53f },   // 2C HF   heart
    {  516, 1922, 2598, false, false, 0.00f, 0.00f },   // 2D HFC  (hold fric closure)
    {  516, 1922, 2598, true,  false, 0.27f, 0.00f },   // 2E HN   (hold nasal)
    {  365, 1106, 2677, true,  true,  0.07f, 0.67f },   // 2F Z    zero
    {  176, 1730, 2598, false, true,  0.00f, 1.00f },   // 30 S    same
    {  314, 2096, 2756, true,  true,  0.07f, 0.67f },   // 31 J    measure
    {  314, 2096, 2756, false, true,  0.00f, 0.40f },   // 32 SCH  ship
    {  314, 1261, 2336, true,  true,  0.20f, 0.27f },   // 33 V    very
    {  314, 1261, 2336, false, true,  0.00f, 0.27f },   // 34 F    four
    {  365, 1730, 2756, true,  true,  0.07f, 0.13f },   // 35 THV  there
    {  446, 1823, 2425, false, true,  0.00f, 0.13f },   // 36 TH   with
    {  176, 1261, 2336, true,  false, 0.67f, 0.00f },   // 37 M    more
    {  176, 1823, 2677, true,  false, 0.53f, 0.00f },   // 38 N    nine
    {  314, 2174, 2756, true,  false, 0.27f, 0.00f },   // 39 NG   rang
    {  516, 1922, 2425, true,  false, 0.53f, 0.00f },   // 3A :A   maerchen
    {  314, 1823, 2336, true,  false, 0.40f, 0.00f },   // 3B :OH  loewe
    {  256, 1730, 2336, true,  false, 0.67f, 0.00f },   // 3C :U   fuenf
    {  176, 1922, 2425, true,  false, 0.67f, 0.00f },   // 3D :UH  menu
    {  482, 1730, 2425, true,  false, 0.47f, 0.00f },   // 3E E2   bitte
    {  256,  943, 2756, true,  false, 0.53f, 0.00f },   // 3F LB   lube
};

// Synthesis constants: excitation gains, the spectral tilt on the glottal
// source, per-stage resonator bandwidths, and the output level that keeps a
// full-amplitude vowel inside the sample range after three resonators.
// The voiced gain rides well above the noise gain because the radiation
// tilt at the output costs F1-dominant voiced energy 20+ dB while barely
// touching the fricatives' F2-region noise; these two together set the
// vowel-to-sibilant balance heard in connected speech.
static constexpr float   s_kfVoicedGain   = 966.00f;
static constexpr float   s_kfNoiseGain    = 0.165f;

// How fast the noise source is smoothed. The LFSR delivers a rail-to-rail bit
// per sample; at 0.65 that smoothing broke at 8 kHz, so the source was very
// nearly white and read as harsh static rather than as breath. The real chip's
// frication is dark -- measured against a recording, its 1600-3200 band sits
// 21 dB under the voice -- because on the die the noise goes through the same
// switched-capacitor sections as everything else.
//
// Measured on that recording, the chip's frication peaks at 1.5-2 kHz and is
// 28 dB down by 3-4 kHz. Ours peaked an octave higher and carried 16 dB too
// much up there, which is what made it read as noise laid over the voice
// rather than part of it. 0.12 breaks near 1 kHz and leaves the shaping to the
// fricative resonators.
// The die carries a dedicated noise shaping filter between the noise
// generator and the tract, which this model omitted: frication went into
// the formant cascade raw and came out bandlimited by F2 and F3. That is
// audible on the sibilants, whose stored formants sit low -- Z is
// 365/1106/2677 against L's 365/1261/2756, so with the noise dark the two
// are nearly the same sound and "Daisy" comes out closer to "Dailey".
// Tilting the noise up before it enters the tract restores the sibilance
// the formants cannot carry, and leaves the resonators to color it.
static constexpr float   s_kfNoiseLpCoef  = 0.07f;

// Output low-pass (one-pole, ~5.5 kHz at 44.1 kHz): rounds off the top
// TWO cascaded sections, not one. Above its top resonance the real chip falls
// about 28 dB in the octave from 2 kHz to 4 kHz, which no single pole can do:
// one pole gives 6 dB there, and against the radiation tilt the excess read as
// hiss laid over the voice. Two give a -12 dB/oct skirt.
//
// octave the radiation tilt would otherwise push to Nyquist -- the digital
// sheen on fricatives -- standing in for the analog output stage between
// the chip and the card's mixer.
static constexpr float   s_kfOutputLpCoef = 0.32f;

// Two cascaded one-pole sections smooth the glottal impulse train: each pulse
// starts from zero rather than opening with a step, and the -12 dB/oct tail
// still excites F2 and F3.
//
// Where that tail STARTS decides the voice's whole character, and it belongs
// well below the pitch range. Measured against a recording of a real SSI-263
// speaking at 88.7 Hz, the chip's voiced spectrum falls monotonically from the
// fundamental at about -8 dB/oct -- a source rolling off at -12 plus the +6 of
// lip radiation. This was a bare 0.10, which breaks at 805 Hz at a 48 kHz
// device: flat underneath, so radiation tilted the output UPWARD across the
// whole F1 region. Against that same recording our spectrum was 21 dB short at
// 80-200 Hz and 9-10 dB heavy from 400 Hz up, which is the thin, buzzy voice.
//
// It is also derived from the device rate now. As a bare coefficient the break
// moved with whatever the host was running at, making timbre a property of the
// listener's DAC.
static constexpr double  s_kSourceBreakHz = 50.0;
static constexpr float   s_kfOutputGain   = 5.00f;
static constexpr double  s_kBandwidthHz[3] = { 60.0, 90.0, 120.0 };

// Frication resonances are much broader than voiced ones -- a hiss is not a
// pitched peak -- and the fricative branch is PARALLEL, not cascaded. Running
// frication through the vowel formants was wrong: Z's F2 sits at 1106 Hz, so
// cascading put a low-pass in front of its own hiss and buried it 17 dB below
// S's, when the chip's stored amplitudes differ by only 3.5 dB. Summed
// resonators contribute without attenuating each other.
static constexpr double  s_kFricBandwidthHz = 450.0;

// One fricative resonator, at F2. Measured on a real SSI-263, frication peaks
// at F2 -- 1.5-2 kHz for an S -- and is 12 dB down by 2.5-3 kHz. A second
// resonator at F3 sat exactly there (2598 Hz for S) and filled in the region
// the chip empties, which read as hiss laid over the voice; it bought nothing
// for telling S from SCH, whose F2s differ by 370 Hz while their F3s differ by
// 160. Removing it took the shape error from 4.3 dB to 3.2.

// Two more poles, on the fricative branch alone. Above its peak the real chip
// falls about 28 dB across the octave from 1.75 to 3.5 kHz; the parallel
// resonators' own skirts, working against the radiation tilt, manage roughly a
// third of that, which left frication reading as hiss laid over the voice. The
// output low-pass cannot supply the rest -- it is shared with the voiced path,
// which is already matched -- so the noise gets its own. 0.28 breaks near
// 2.5 kHz.
static constexpr float   s_kfFricLpCoef   = 0.28f;

// Amplitude envelope rates: a fast attack and a slightly longer release,
// expressed as time constants the per-sample coefficient is derived from.
// How fast a phoneme's two source amplitudes reach their new values. The
// formants already glide; the LEVELS used to step, so every phoneme boundary
// changed the excitation's amplitude and its character in a single sample --
// a vowel's 0.40 of voicing became a stop's 0.27 of frication instantly. That
// step is what popped, and it fired at every boundary, which is why the
// popping followed the phoneme rate. 6 ms is short enough to leave a stop its
// attack and long enough that the step is no longer a click. 6 ms cost the B
// in "mockingboard" its burst, so this is as short as it can be while still
// measuring as smooth as the real chip.
static constexpr double  s_kLevelTauSec   = 0.0025;
static constexpr double  s_kAttackTauSec  = 0.002;
static constexpr double  s_kReleaseTauSec = 0.004;





////////////////////////////////////////////////////////////////////////////////
//
//  Ssi263
//
//  Constructing the chip leaves it in its power-on state, which the datasheet
//  defines as Power Down: CTL set, no audio, no request.
//
////////////////////////////////////////////////////////////////////////////////

Ssi263::Ssi263 (double xckHz)
{
    // The tick clock is deliberately left unstated here. Until an owner calls
    // SetTickClock, GetTickClockHz answers with XCK, which is the only
    // self-consistent reading of a Tick nobody has described.
    m_xckHz = (xckHz > 0.0) ? xckHz : kDefaultXckHz;

    Reset();
}





////////////////////////////////////////////////////////////////////////////////
//
//  SetSampleRate
//
//  The host rendering rate. Deliberately does NOT participate in phoneme
//  timing -- that is driven from emulated cycles in Tick, so an utterance
//  occupies the same emulated span whatever the audio device is doing.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::SetSampleRate (uint32_t sampleRate)
{
    m_sampleRate = sampleRate;

    if (m_sampleRate > 0)
    {
        m_sourcePole = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi *
                                                           s_kSourceBreakHz / m_sampleRate));
    }
}





////////////////////////////////////////////////////////////////////////////////
//
//  SetXckClock
//
//  The external XCK time base. Every published timing formula is relative to
//  it, so changing it rescales durations and filter frequencies together. It
//  is NOT the rate Tick counts in; see SetTickClock.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::SetXckClock (double xckHz)
{
    double   previousXck  = m_xckHz;
    double   previousTick = GetTickClockHz();



    if (xckHz > 0.0)
    {
        m_xckHz = xckHz;

        // A phoneme already sounding was loaded against the old rates, so
        // rescale the remainder to leave the same fraction of it to play. Two
        // ratios apply. Its length in SECONDS moves with XCK, and the domain
        // that length is counted in moves too whenever the tick clock is
        // still following XCK. Once an owner has stated a tick clock the
        // second ratio is 1, and only the duration moves.
        m_phonemeCycles *= (previousXck / m_xckHz) *
                           (GetTickClockHz() / previousTick);
    }
}





////////////////////////////////////////////////////////////////////////////////
//
//  SetTickClock
//
//  The rate of the cycles Tick receives -- the owning machine's clock, not the
//  chip's. It converts a duration in seconds into the countdown Tick drains,
//  and it is the only thing in the class that does: the datasheet formulas all
//  stay in XCK terms.
//
//  An Apple II Mockingboard ticks at phi2, about 1.75x slower than XCK, so the
//  distinction is worth 75% of every phoneme's length.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::SetTickClock (double tickClockHz)
{
    double   previous = GetTickClockHz();



    if (tickClockHz > 0.0)
    {
        m_tickClockHz = tickClockHz;

        // As above: an in-flight countdown is in the old domain and has to be
        // converted rather than left to drain at the wrong rate. `previous`
        // comes from GetTickClockHz so that the first call on a chip which was
        // still following XCK converts from XCK, not from an unset zero.
        m_phonemeCycles *= m_tickClockHz / previous;
    }
}





////////////////////////////////////////////////////////////////////////////////
//
//  SelectRegister
//
//  Maps the three address lines onto the five registers. RS2 high selects the
//  filter register regardless of RS1/RS0, so addresses 4..7 all alias to it.
//
////////////////////////////////////////////////////////////////////////////////

Byte Ssi263::SelectRegister (Byte address)
{
    Byte   sel = static_cast<Byte> (address & kAddressMask);



    return (sel >= kRegFilterFreq) ? kRegFilterFreq : sel;
}





////////////////////////////////////////////////////////////////////////////////
//
//  WriteRegister
//
//  Loads one attribute register. Two writes have side effects beyond storing
//  the value:
//
//    * A CTL one-to-zero transition leaves Power Down and latches the
//      operating mode from the duration bits as they stand at that instant.
//      Later changes to those bits select a duration, not a mode.
//
//    * Writing the duration/phoneme register while the chip is running starts
//      that phoneme and withdraws the outstanding request. This is what the
//      software's pacing loop does on every A/R.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::WriteRegister (Byte reg, Byte value)
{
    Byte   sel     = SelectRegister (reg);
    bool   wasDown = IsPoweredDown();



    m_reg[sel] = value;

    if (sel == kRegCtlArtAmp)
    {
        if (wasDown && !IsPoweredDown())
        {
            LatchMode();
        }
        else if (!wasDown && IsPoweredDown())
        {
            // Power Down ends the phoneme and disables A/R without disturbing
            // the register contents. The envelope is NOT zeroed: clearing it
            // here cut the waveform mid-cycle, which is the pop IsSilent's
            // release exists to avoid. Left alone it decays over the release
            // tail like the end of any other phoneme.
            m_sounding      = false;
            m_phonemeCycles = 0.0;
            m_request       = false;
        }
    }
    else if (sel == kRegDurationPhoneme && !IsPoweredDown())
    {
        BeginPhoneme();
    }
}





////////////////////////////////////////////////////////////////////////////////
//
//  Reset
//
//  Power-on state. CTL is set high on reset and on power up, which IS Power
//  Down -- so quiescence is what the hardware does, not a safety measure added
//  here. Nothing sounds and nothing requests until software drives CTL low.
//
//  That property is what lets the sound+speech card default safely: an
//  unprogrammed voice chip cannot introduce an interrupt a sound-only title
//  never armed.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::Reset()
{
    int   i = 0;



    for (i = 0; i < kRegCount; i++)
    {
        m_reg[i] = 0;
    }

    m_reg[kRegCtlArtAmp] = kCtl;

    m_mode          = kModeArDisabled;
    m_request       = false;
    m_phonemeCycles = 0.0;
    m_sounding      = false;

    for (i = 0; i < 3; i++)
    {
        m_fCur[i]  = 0.0;
        m_resY1[i] = 0.0f;
        m_resY2[i] = 0.0f;
    }

    m_envLevel     = 0.0f;
    m_radPrev      = 0.0f;
    m_outLp        = 0.0f;
    m_outLp2       = 0.0f;
    m_glottalPhase = 0.0;
    m_excLp1       = 0.0f;
    m_excLp2       = 0.0f;
    m_noiseLp      = 0.0f;
    m_vaCur        = 0.0f;
    m_faCur        = 0.0f;
    m_fricLp       = 0.0f;
    m_fricLp2      = 0.0f;
    m_fricLp3      = 0.0f;
    m_fricY1       = 0.0f;
    m_fricY2       = 0.0f;
    m_lfsr         = 0xACE1u;
}





////////////////////////////////////////////////////////////////////////////////
//
//  Tick
//
//  Advances the current phoneme by `cycles` of emulated machine time. When the
//  phoneme's duration is exhausted the chip stops sounding and -- in the modes
//  where A/R is enabled -- raises the request that asks software for the next
//  one.
//
//  A chip that is powered down, or not sounding, has nothing to advance. That
//  early exit is also what makes an idle chip free.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::Tick (uint32_t cycles)
{
    if (!m_sounding || IsPoweredDown())
    {
        return;
    }

    m_phonemeCycles -= static_cast<double> (cycles);

    if (m_phonemeCycles <= 0.0)
    {
        m_phonemeCycles = 0.0;
        m_sounding      = false;

        if (m_mode != kModeArDisabled)
        {
            m_request = true;
        }
    }
}





////////////////////////////////////////////////////////////////////////////////
//
//  IsSilent
//
//  True when the chip contributes nothing to the mix, so the audio path can
//  skip synthesis outright. Powered down, not sounding, or at zero amplitude
//  all qualify.
//
////////////////////////////////////////////////////////////////////////////////

bool Ssi263::IsSilent() const
{
    // Nothing here is silent until the release tail has decayed: the envelope
    // ramps every boundary instead of gating it, so the chip stays audible for
    // the few milliseconds the ramp needs. Idle and unprogrammed chips have a
    // zero envelope and stay free.
    //
    // Power Down used to short-circuit this and return true at once, which cut
    // the waveform to zero mid-cycle -- the demo ends on a Power Down and that
    // was an audible pop every time. It releases like everything else now.
    bool   quiet = IsPoweredDown() || (GetAmplitude() == 0) || !m_sounding;



    return quiet && (m_envLevel < 0.001f);
}





////////////////////////////////////////////////////////////////////////////////
//
//  GetInflectionValue
//
//  Reassembles the 12-bit inflection value, which the register map scatters
//  non-contiguously: I11 sits alone at register 2 bit 3, I10-I3 fill register
//  1, and I2-I0 are register 2 bits 2-0.
//
////////////////////////////////////////////////////////////////////////////////

uint16_t Ssi263::GetInflectionValue() const
{
    uint16_t   high = static_cast<uint16_t> ((m_reg[kRegRateInflection] & kInflect11) != 0 ? 0x800 : 0);
    uint16_t   mid  = static_cast<uint16_t> (static_cast<uint16_t> (m_reg[kRegInflection]) << 3);
    uint16_t   low  = static_cast<uint16_t> (m_reg[kRegRateInflection] & kInflectLowMask);



    return static_cast<uint16_t> (high | mid | low);
}





////////////////////////////////////////////////////////////////////////////////
//
//  GetFrameDurationSec
//
//  Datasheet: Frame Duration = 4096 * (16 - R) / XCK.
//
////////////////////////////////////////////////////////////////////////////////

double Ssi263::GetFrameDurationSec() const
{
    return (4096.0 * (16.0 - static_cast<double> (GetRateSel()))) / m_xckHz;
}





////////////////////////////////////////////////////////////////////////////////
//
//  GetPhonemeDurationSec
//
//  Datasheet: Phoneme Duration = Frame Duration * (4 - D).
//
//  In the frame-timing mode the duration bits select the mode rather than
//  scaling the frame, so the frame duration stands unmodified.
//
////////////////////////////////////////////////////////////////////////////////

double Ssi263::GetPhonemeDurationSec() const
{
    double   frame = GetFrameDurationSec();



    if (m_mode == kModeFrameImmediate)
    {
        return frame;
    }

    return frame * (4.0 - static_cast<double> (GetDurationSel()));
}





////////////////////////////////////////////////////////////////////////////////
//
//  GetFilterFrequencyHz
//
//  Datasheet: Filter Frequency = XCK / (2 * (256 - F)).
//
////////////////////////////////////////////////////////////////////////////////

double Ssi263::GetFilterFrequencyHz() const
{
    double   divisor = 2.0 * (256.0 - static_cast<double> (m_reg[kRegFilterFreq]));



    return (divisor > 0.0) ? (m_xckHz / divisor) : 0.0;
}





////////////////////////////////////////////////////////////////////////////////
//
//  GetInflectionFrequencyHz
//
//  Datasheet: Inflection Frequency = XCK / (8 * (4096 - I)).
//
////////////////////////////////////////////////////////////////////////////////

double Ssi263::GetInflectionFrequencyHz() const
{
    double   divisor = 8.0 * (4096.0 - static_cast<double> (GetInflectionValue()));



    return (divisor > 0.0) ? (m_xckHz / divisor) : 0.0;
}





////////////////////////////////////////////////////////////////////////////////
//
//  GenerateSample
//
//  One mono sample of formant synthesis: an excitation source shaped by
//  three resonators whose centers glide toward the active phoneme's targets.
//  The filter-frequency register scales the whole tract relative to its
//  nominal clock, which is the datasheet's "voice type" adjustment; the
//  amplitude register scales the output.
//
//  Silent states pay nothing here -- the IsSilent early-out is the idle
//  fast-path that keeps an unprogrammed chip free.
//
////////////////////////////////////////////////////////////////////////////////

float Ssi263::GenerateSample()
{
    float    sample = 0.0f;
    float    diffed = 0.0f;
    float    target = 0.0f;
    double   tau    = 0.0;
    double   coef   = 0.0;
    double   scale  = 1.0;
    int      stage  = 0;



    if (IsSilent() || m_sampleRate == 0)
    {
        return 0.0f;
    }

    GlideFormants();
    GlideLevels();

    scale = GetFilterFrequencyHz() / kNominalFilterHz;
    scale = std::clamp (scale, 0.5, 2.0);

    // Voiced excitation drives the whole tract from the glottis; frication
    // is front-cavity excitation and enters AFTER the first formant stage.
    // Injecting noise at the glottis instead sent it through the F1 low-pass,
    // which crushed sibilants into a quiet sub-1 kHz rumble.
    sample = Resonate (0, GenerateExcitation(), m_fCur[0] * scale);

    for (stage = 1; stage < 3; stage++)
    {
        sample = Resonate (stage, sample, m_fCur[stage] * scale);
    }

    // Frication runs beside the tract, not through it: the noise is shaped by
    // its own broad resonator at F2 and summed in.
    // Driven by the glided level, not the spec's, so frication fades in and
    // out rather than switching on. The branch stays live while that level is
    // still decaying, which is why it tests m_faCur and not spec.fricative.
    if (GetActiveSpec().fricative || m_faCur > 0.0001f)
    {
        float   noise = GenerateNoiseSample();

        float   fric = ResonateFricative (noise, m_fCur[1] * scale);

        m_fricLp  += s_kfFricLpCoef * (fric - m_fricLp);
        m_fricLp2 += s_kfFricLpCoef * (m_fricLp - m_fricLp2);
        m_fricLp3 += s_kfFricLpCoef * (m_fricLp2 - m_fricLp3);

        sample += m_fricLp3 * s_kfNoiseGain * m_faCur;
    }

    // Radiation characteristic: the lips differentiate the volume flow,
    // a +6 dB/oct tilt. Without it the -12 dB/oct glottal source leaves
    // the cascade too dark -- F2/F3 sit far below F1 and every vowel
    // smears toward an "aw". The first difference is normalized by the
    // sample rate so its gain at a given frequency is host-rate-invariant.
    diffed    = (sample - m_radPrev) *
                (static_cast<float> (m_sampleRate) / 44100.0f);
    m_radPrev = sample;
    sample    = diffed;

    m_outLp  += s_kfOutputLpCoef * (sample - m_outLp);
    m_outLp2 += s_kfOutputLpCoef * (m_outLp - m_outLp2);
    sample    = m_outLp2;

    // The amplitude envelope: eases toward the active level while sounding
    // and toward zero once the phoneme has finished, so every boundary is a
    // short ramp over the resonators' natural tail rather than a gate --
    // the linear amplitude transitioning the datasheet describes. Truncating
    // it instead put an audible click at every phoneme edge.
    target = m_sounding
                 ? (HasAnySource (GetActiveSpec()) ? 1.0f : 0.0f) *
                       (static_cast<float> (GetAmplitude()) / 15.0f)
                 : 0.0f;
    tau    = (target > m_envLevel) ? s_kAttackTauSec : s_kReleaseTauSec;
    coef   = 1.0 - std::exp (-1.0 / (tau * static_cast<double> (m_sampleRate)));

    m_envLevel += static_cast<float> (coef) * (target - m_envLevel);

    sample *= m_envLevel * s_kfOutputGain;

    return std::clamp (sample, -1.0f, 1.0f);
}





////////////////////////////////////////////////////////////////////////////////
//
//  SetFormantTable
//
//  Swaps the per-phoneme acoustic table. The synthesis reads targets through
//  this seam only, so better data -- measured from hardware or extracted from
//  the chip's ROM -- replaces the built-in approximation with no other change.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::SetFormantTable (const Ssi263PhonemeSpec * table)
{
    m_formants = table;
}





////////////////////////////////////////////////////////////////////////////////
//
//  GetActiveSpec
//
////////////////////////////////////////////////////////////////////////////////

const Ssi263PhonemeSpec & Ssi263::GetActiveSpec() const
{
    const Ssi263PhonemeSpec *   table = (m_formants != nullptr) ? m_formants : s_kPhonemes;



    return table[GetPhoneme()];
}





////////////////////////////////////////////////////////////////////////////////
//
//  GlideFormants
//
//  Eases the current formant centers toward the active phoneme's targets at
//  the articulation rate -- the linear transition to a new set of
//  characteristics the datasheet describes, with T2-T0 setting how fast.
//  A tract starting from silence snaps rather than sweeping up from zero.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::GlideFormants()
{
    const Ssi263PhonemeSpec &   spec = GetActiveSpec();



    double   target[3] = { 0.0, 0.0, 0.0 };
    double   tauSec    = 0.0;
    double   coef      = 0.0;
    int      i         = 0;



    // A pause or closure has no formant targets of its own: the tract HOLDS
    // its position through it rather than sinking toward zero -- otherwise
    // the phoneme after every pause would sweep up from the basement and
    // every utterance would open on a spurious glide.
    if (spec.f1 == 0)
    {
        return;
    }

    target[0] = spec.f1;
    target[1] = spec.f2;
    target[2] = spec.f3;

    if (m_fCur[0] <= 0.0)
    {
        for (i = 0; i < 3; i++)
        {
            m_fCur[i] = target[i];
        }

        return;
    }

    // Articulation 0 is the slowest transition, 7 the fastest.
    tauSec = (8.0 - static_cast<double> (GetArticulation())) * 0.010;
    coef   = 1.0 - std::exp (-1.0 / (tauSec * static_cast<double> (m_sampleRate)));

    for (i = 0; i < 3; i++)
    {
        m_fCur[i] += coef * (target[i] - m_fCur[i]);
    }
}





////////////////////////////////////////////////////////////////////////////////
//
//  GlideLevels
//
//  Eases the two source amplitudes toward the active phoneme's, so a boundary
//  changes the excitation over a few milliseconds rather than in one sample.
//  Runs every sample, next to the formant glide, and unlike that one it does
//  NOT hold through a pause: a pause really does want both sources at zero.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::GlideLevels()
{
    const Ssi263PhonemeSpec &   spec = GetActiveSpec();



    double   coef = 1.0 - std::exp (-1.0 / (s_kLevelTauSec *
                                            static_cast<double> (m_sampleRate)));

    m_vaCur += static_cast<float> (coef) * (spec.voicedLevel - m_vaCur);
    m_faCur += static_cast<float> (coef) * (spec.fricLevel   - m_faCur);
}





////////////////////////////////////////////////////////////////////////////////
//
//  GenerateExcitation
//
//  The glottal source: a tilted pulse train at the inflection frequency for
//  voiced phonemes. Frication is generated separately by GenerateNoiseSample and
//  injected past the first formant stage.
//
////////////////////////////////////////////////////////////////////////////////

float Ssi263::GenerateExcitation()
{
    float    src     = 0.0f;
    float    impulse = 0.0f;
    double   pitch   = 0.0;



    // The oscillator runs unconditionally and the GLIDED level decides how
    // much of it is heard. It used to be gated on the phoneme's `voiced`
    // flag, which is read from the incoming spec the instant it changes, so
    // handing a vowel to a fricative stopped the glottal source dead in one
    // sample while m_vaCur was still easing down from 0.67 -- a step, and an
    // audible pop at the transition. The flag is redundant anyway: across all
    // 64 ROM rows, voiced == false always carries voicedLevel == 0, so the
    // level alone says everything the flag did, and says it smoothly.
    //
    // Running the phase through unvoiced stretches also means a returning
    // vowel resumes mid-cycle instead of restarting, and the two source poles
    // decay to nothing rather than holding stale energy across the gap.
    pitch = std::clamp (GetInflectionFrequencyHz(), 30.0, 400.0);

    m_glottalPhase += pitch / static_cast<double> (m_sampleRate);

    if (m_glottalPhase >= 1.0)
    {
        m_glottalPhase -= 1.0;
        impulse         = 1.0f;
    }

    // Two cascaded one-pole sections: the pulse rises from zero rather
    // than opening with a step, and its tail keeps enough energy at the
    // upper formants to excite them.
    m_excLp1 += m_sourcePole * (impulse - m_excLp1);
    m_excLp2 += m_sourcePole * (m_excLp1 - m_excLp2);

    src = m_excLp2 * s_kfVoicedGain * m_vaCur;

    // The radiation tilt at the output taxes low-F1 vowels roughly with
    // the square of F1; compensating one power of it here keeps close
    // vowels (OU, :OH) audible next to open ones (AH, AE) while leaving
    // the natural open-vowels-are-louder tendency in place. Glided F1 is
    // used so the correction moves smoothly through transitions.
    src *= static_cast<float> (731.0 / std::max (m_fCur[0], 170.0));

    return src;
}





////////////////////////////////////////////////////////////////////////////////
//
//  GenerateNoiseSample
//
//  Deterministic LFSR noise for frication, one-pole smoothed: raw LFSR
//  output swings rail to rail between adjacent samples, which is a stream
//  of clicks, not a hiss. The coefficient sets the noise brightness.
//
////////////////////////////////////////////////////////////////////////////////

float Ssi263::GenerateNoiseSample()
{
    Byte   bit = static_cast<Byte> (m_lfsr & 1u);



    m_lfsr = m_lfsr >> 1;

    if (bit != 0)
    {
        m_lfsr ^= 0xB400u;
    }

    // Smoothing the rail-to-rail step both removes what would read as clicks
    // and band-limits the source; the fricative branch's own resonators supply
    // the shape from there.
    m_noiseLp += s_kfNoiseLpCoef * (((bit != 0) ? 1.0f : -1.0f) - m_noiseLp);

    return m_noiseLp;
}





////////////////////////////////////////////////////////////////////////////////
//
//  Resonate
//
//  One two-pole resonator stage, normalized for unit gain at DC so a
//  cascade passes energy below each center instead of attenuating it --
//  the classic formant-synthesis arrangement, and what makes three stages
//  in series workable. The stages are the "cascaded programmable low pass
//  filter sections" the datasheet describes, programmed here by the glided
//  formant centers.
//
////////////////////////////////////////////////////////////////////////////////

float Ssi263::Resonate (int stage, float input, double centerHz)
{
    double   fs = static_cast<double> (m_sampleRate);
    double   fc = std::clamp (centerHz, 50.0, fs * 0.45);
    double   r  = std::exp (-std::numbers::pi * s_kBandwidthHz[stage] / fs);
    double   b  = 2.0 * r * std::cos (2.0 * std::numbers::pi * fc / fs);
    double   c  = -(r * r);
    float    y  = 0.0f;



    y = static_cast<float> ((1.0 - b - c) * input +
                            b * m_resY1[stage] +
                            c * m_resY2[stage]);

    m_resY2[stage] = m_resY1[stage];
    m_resY1[stage] = y;

    return y;
}





////////////////////////////////////////////////////////////////////////////////
//
//  ResonateFricative
//
//  The PARALLEL fricative branch's resonator. Same two-pole form as the tract
//  sections but much broader, and beside the tract rather than in it, so a low
//  F2 colors the hiss instead of low-passing it away.
//
////////////////////////////////////////////////////////////////////////////////

float Ssi263::ResonateFricative (float input, double centerHz)
{
    double   fs = static_cast<double> (m_sampleRate);
    double   fc = std::clamp (centerHz, 50.0, fs * 0.45);
    double   r  = std::exp (-std::numbers::pi * s_kFricBandwidthHz / fs);
    double   b  = 2.0 * r * std::cos (2.0 * std::numbers::pi * fc / fs);
    double   c  = -(r * r);
    float    y  = 0.0f;



    y = static_cast<float> ((1.0 - b - c) * input +
                            b * m_fricY1 +
                            c * m_fricY2);

    m_fricY2 = m_fricY1;
    m_fricY1 = y;

    return y;
}





////////////////////////////////////////////////////////////////////////////////
//
//  LatchMode
//
//  Captures the operating mode on the CTL one-to-zero transition, from the
//  duration bits as they stand at that instant.
//
//  In the three modes the chart marks "A/R active" the transition also STARTS
//  the phoneme already sitting in P5-P0: excitation and analog come back on,
//  the chip generates that phoneme, and A/R falls when it is done. Only the
//  DR1=DR0=LO row leaves the request line alone.
//
//  That is the only event the datasheet offers that can produce a FIRST A/R --
//  it has software load the attribute registers after power up and then take
//  CTL low -- so a driver written in that order, and detection code that spins
//  on the interrupt, get nothing at all without this.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::LatchMode()
{
    m_mode = GetDurationSel();

    if (m_mode == kModeArDisabled)
    {
        m_request       = false;
        m_sounding      = false;
        m_phonemeCycles = 0.0;

        return;
    }

    BeginPhoneme();
}





////////////////////////////////////////////////////////////////////////////////
//
//  BeginPhoneme
//
//  Starts the phoneme now in P5-P0 and withdraws any outstanding request. The
//  pacing loop reaches here by writing the next phoneme, and that write IS the
//  answer to the previous request; the CTL one-to-zero transition reaches here
//  with whatever was loaded while the chip was powered down.
//
//  A phoneme after a pause GLIDES IN from wherever the tract was left, and
//  does not snap to its own targets. Snapping was tried first, by zeroing the
//  formants at the pause so the next phoneme took the cold-start path, and it
//  traded one artifact for a worse one: see the note in the body.
//
////////////////////////////////////////////////////////////////////////////////

void Ssi263::BeginPhoneme()
{
    double   seconds = GetPhonemeDurationSec();



    // The tract is NOT reset here. Zeroing the current formants at a pause
    // defeated the hold GlideFormants performs for a phoneme with no targets
    // of its own: the zero made the next real phoneme take the cold-start
    // path and SNAP its resonators to their targets, stepping three filter
    // centers at once while those filters were carrying state. That step is a
    // pop, and the demo puts a pause inside consonant clusters as well as
    // between words, which is why the popping tracked phoneme edges. Holding
    // the position instead means the phoneme after a pause glides in from
    // wherever the tract was, which is also what the mouth does.

    // The datasheet formula gives seconds against XCK; the countdown Tick
    // drains is in the machine's cycles, so the tick clock is what converts.
    m_phonemeCycles = seconds * GetTickClockHz();
    m_sounding      = (m_phonemeCycles > 0.0);
    m_request       = false;
}





////////////////////////////////////////////////////////////////////////////////
//
//  HasAnySource
//
//  True when a phoneme drives either source. The pause and the two closure
//  holds drive neither, which is how silence is spelled on this chip.
//
////////////////////////////////////////////////////////////////////////////////

bool Ssi263::HasAnySource (const Ssi263PhonemeSpec & spec)
{
    return (spec.voicedLevel > 0.0f) || (spec.fricLevel > 0.0f);
}
