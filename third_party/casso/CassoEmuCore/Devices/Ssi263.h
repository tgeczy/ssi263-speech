#pragma once

#include "Pch.h"





////////////////////////////////////////////////////////////////////////////////
//
//  Ssi263PhonemeSpec
//
//  Acoustic targets for one phoneme: three formant center frequencies, the
//  excitation type, and an intrinsic level. The chip's own per-phoneme
//  parameters live in an internal ROM that was never published, so these
//  values are derived from the public acoustic-phonetics literature -- a
//  deliberate approximation, disclosed as such, and replaceable as a whole
//  table without touching the synthesis (see SetFormantTable).
//
////////////////////////////////////////////////////////////////////////////////

struct Ssi263PhonemeSpec
{
    uint16_t   f1;          // formant center frequencies, Hz
    uint16_t   f2;
    uint16_t   f3;
    bool       voiced;      // glottal excitation
    bool       fricative;   // noise excitation (both set = voiced fricative)

    //  The chip stores the two source amplitudes SEPARATELY -- vocal amplitude
    //  and fricative amplitude -- and the ratio between them is what tells one
    //  sound from another. Z is frication 10, voicing 1; L is frication 0,
    //  voicing 7. Collapsing them to one number made Z and L the same sound,
    //  since their formants are nearly identical (365/1106/2677 against
    //  365/1261/2756) and only the source balance separates them.
    float      voicedLevel; // VA / 15
    float      fricLevel;   // FA / 15
};





////////////////////////////////////////////////////////////////////////////////
//
//  Ssi263
//
//  Clean-room Silicon Systems SSI 263A phoneme speech synthesizer, written
//  from the datasheet register map. The same part was marketed by Votrax as
//  the SC-02, and it is the voice chip on the Mockingboard C. Generic and
//  reusable: the class knows nothing about the Mockingboard -- it exposes
//  five attribute registers, a status read, and a mono sample stream.
//
//  Register file, selected by the three address lines RS2-RS0:
//
//    0  DUR/PHON   D7:6 = DR1,DR0 duration    D5:0 = P5-P0 phoneme
//    1  INFLECT    D7:0 = I10-I3
//    2  RATE/INFL  D7:4 = R3-R0 rate          D3 = I11   D2:0 = I2-I0
//    3  CTL/ART/A  D7 = CTL   D6:4 = T2-T0    D3:0 = A3-A0 amplitude
//    4  FILTER     D7:0 = F7-F0
//
//  RS2 high selects the filter register regardless of the low two lines, so
//  addresses 4..7 all alias to it -- five registers in eight slots.
//
//  The 12-bit inflection value is scattered non-contiguously across two
//  registers and is reassembled by GetInflectionValue().
//
//  Timing follows the datasheet formulas, all relative to the external clock:
//
//    Frame Duration       = 4096 * (16 - R) / XCK
//    Phoneme Duration     = Frame Duration * (4 - D)
//    Inflection Frequency = XCK / (8 * (4096 - I))
//    Filter Frequency     = XCK / (2 * (256 - F))
//
//  TWO CLOCK DOMAINS meet in this class and must not be interchanged:
//
//    * XCK is the chip's own external time base, generated on the card and
//      independent of the host processor. Every formula above divides by it,
//      and each one yields SECONDS or Hz.
//
//    * The TICK clock is the rate of the cycles Tick receives, which is
//      whatever the owning machine counts -- phi2 on an Apple II, roughly
//      1.75x slower than XCK.
//
//  A duration in seconds therefore has to be multiplied by the TICK clock to
//  become a countdown Tick can drain. Multiplying by XCK instead is what once
//  made every phoneme sound 1.75x too long. SetXckClock and SetTickClock keep
//  the two rates separate.
//
//  Until SetTickClock states a rate, the tick clock FOLLOWS XCK, whichever way
//  XCK was set -- through the constructor or through SetXckClock. The two must
//  agree: a fallback that tracked the constructor but froze at the default
//  afterward would make Ssi263 (rate) and SetXckClock (rate) time phonemes
//  differently, which is a trap of exactly the kind this class already sprung
//  once.
//
//  A/R (Acknowledge/Request Not) goes from high to low once a phoneme has
//  been generated, and the owning card wires it to a VIA control line as an
//  interrupt. Reading the chip returns the INVERTED A/R state in D7 and
//  ignores the register address entirely.
//
//  Quiescence is the chip's own documented behavior rather than a policy
//  imposed here: CTL is set on power-up and reset, which is Power Down --
//  excitation and analog off, registers retained, A/R disabled. Software
//  brings CTL low to select an operating mode from the DR bits, and the
//  DR1=DR0=0 mode disables A/R outright.
//
////////////////////////////////////////////////////////////////////////////////

class Ssi263
{
public:
    static constexpr Byte    kRegCount    = 5;
    static constexpr Byte    kAddressMask = 0x07;

    static constexpr Byte    kRegDurationPhoneme = 0;
    static constexpr Byte    kRegInflection      = 1;
    static constexpr Byte    kRegRateInflection  = 2;
    static constexpr Byte    kRegCtlArtAmp       = 3;
    static constexpr Byte    kRegFilterFreq      = 4;

    static constexpr Byte    kPhonemeMask   = 0x3F;
    static constexpr Byte    kPhonemeCount  = 64;
    static constexpr Byte    kDurationShift = 6;

    static constexpr Byte    kCtl           = 0x80;
    static constexpr Byte    kArticMask     = 0x70;
    static constexpr Byte    kArticShift    = 4;
    static constexpr Byte    kAmplitudeMask = 0x0F;

    static constexpr Byte    kRateMask       = 0xF0;
    static constexpr Byte    kRateShift      = 4;
    static constexpr Byte    kInflect11      = 0x08;
    static constexpr Byte    kInflectLowMask = 0x07;

    // Duration-bit mode encodings, latched on a CTL one-to-zero transition.
    static constexpr Byte    kModePhonemeTransitioned = 3;
    static constexpr Byte    kModePhonemeImmediate    = 2;
    static constexpr Byte    kModeFrameImmediate      = 1;
    static constexpr Byte    kModeArDisabled          = 0;

    // The datasheet's nominal XCK time base: a colorburst crystal divided by
    // two. This is the chip's own clock, NOT the rate Tick counts in.
    static constexpr double  kDefaultXckHz = 1789772.5;

    explicit Ssi263 (double xckHz = kDefaultXckHz);

    void    SetSampleRate (uint32_t sampleRate);
    void    SetXckClock   (double xckHz);
    void    SetTickClock  (double tickClockHz);

    void    WriteRegister (Byte reg, Byte value);

    void    Reset ();

    // Advance the chip by `cycles` of the TICK clock -- emulated machine time,
    // not XCK. Phoneme duration and the A/R request are driven from here, never
    // from the audio path, so an utterance occupies the same emulated span at
    // any host sample rate.
    void    Tick (uint32_t cycles);

    // Render one mono sample at the host rate. Zero whenever IsSilent().
    float   GenerateSample ();

    // Replace the per-phoneme acoustic table (64 entries). The default is a
    // built-in table from the published phonetics literature; a caller with
    // better data -- a measured or extracted set -- swaps it here with no
    // other change. Pass nullptr to restore the built-in table.
    void    SetFormantTable (const Ssi263PhonemeSpec * table);

    bool    IsRequesting () const { return m_request; }
    bool    IsPoweredDown() const { return (m_reg[kRegCtlArtAmp] & kCtl) != 0; }
    bool    IsSilent     () const;

    Byte    GetPhoneme     () const { return static_cast<Byte> (m_reg[kRegDurationPhoneme] & kPhonemeMask); }
    Byte    GetDurationSel () const { return static_cast<Byte> (m_reg[kRegDurationPhoneme] >> kDurationShift); }
    Byte    GetRateSel     () const { return static_cast<Byte> ((m_reg[kRegRateInflection] & kRateMask) >> kRateShift); }
    Byte    GetAmplitude   () const { return static_cast<Byte> (m_reg[kRegCtlArtAmp] & kAmplitudeMask); }
    Byte    GetArticulation() const { return static_cast<Byte> ((m_reg[kRegCtlArtAmp] & kArticMask) >> kArticShift); }
    Byte    GetActiveMode  () const { return m_mode; }

    // Current center frequency of one resonator stage (0..2) -- the glided
    // tract position, exposed so tests can assert transitions directly.
    double   GetFormantCenter (int stage) const { return m_fCur[stage]; }

    uint16_t GetInflectionValue  () const;
    double   GetFrameDurationSec () const;
    double   GetPhonemeDurationSec () const;
    double   GetFilterFrequencyHz () const;
    double   GetInflectionFrequencyHz () const;

    static Byte  SelectRegister (Byte address);

    // Nominal vocal-tract filter clock; the filter register scales formants
    // relative to it, changing "voice type" as the datasheet describes.
    static constexpr double  kNominalFilterHz = 20000.0;

private:
    double  GetTickClockHz      () const { return (m_tickClockHz > 0.0) ? m_tickClockHz : m_xckHz; }

    void    LatchMode           ();
    void    BeginPhoneme        ();
    void    GlideFormants       ();
    void    GlideLevels         ();
    float   GenerateExcitation  ();
    float   GenerateNoiseSample ();
    float   Resonate            (int stage, float input, double centerHz);
    float   ResonateFricative   (float input, double centerHz);

    static bool  HasAnySource   (const Ssi263PhonemeSpec & spec);

    const Ssi263PhonemeSpec &  GetActiveSpec () const;

    // The chip's own time base, and the rate of the cycles Tick is fed. They
    // are different clocks; see the two-domain note above. Zero means no owner
    // has stated a tick rate, in which case it follows XCK -- resolved at each
    // use through GetTickClockHz rather than copied, so the two cannot drift.
    double     m_xckHz       = kDefaultXckHz;
    double     m_tickClockHz = 0.0;
    uint32_t   m_sampleRate  = 0;

    // The glottal source's break, as a one-pole coefficient for the current
    // device rate (see s_kSourceBreakHz). Set by SetSampleRate.
    float      m_sourcePole   = 0.0065f;

    Byte       m_reg[kRegCount] = {};

    // Mode latched on the CTL one-to-zero transition, from the DR bits as they
    // stood at that moment -- NOT re-read as DR changes afterwards.
    Byte       m_mode = kModeArDisabled;

    // A/R is active-low on the pin; this tracks the logical "wants data" state.
    bool       m_request = false;

    // Remaining TICK-clock cycles in the current phoneme, and whether one is
    // sounding at all.
    double     m_phonemeCycles = 0.0;
    bool       m_sounding      = false;

    // Acoustic table in force; the built-in set unless replaced.
    const Ssi263PhonemeSpec *   m_formants = nullptr;

    // Formant glide state: current center frequencies easing toward the
    // active phoneme's targets at the articulation rate.
    double     m_fCur[3] = { 0.0, 0.0, 0.0 };

    // Two-pole resonator history, one pair per formant stage.
    float      m_resY1[3] = { 0.0f, 0.0f, 0.0f };
    float      m_resY2[3] = { 0.0f, 0.0f, 0.0f };

    // Output amplitude envelope: eases toward the active level so phoneme
    // boundaries ramp instead of gating -- the linear amplitude transition
    // the datasheet describes, and what keeps boundaries click-free.
    float      m_envLevel = 0.0f;

    // Excitation state: glottal period phase, the two-pole smoothing that
    // shapes each impulse into a glottal pulse, and a fixed-seed noise
    // LFSR so output is deterministic.
    double     m_glottalPhase = 0.0;
    float      m_excLp1       = 0.0f;
    float      m_excLp2       = 0.0f;
    uint32_t   m_lfsr         = 0xACE1u;
    float      m_noiseLp      = 0.0f;

    // The two source amplitudes as actually applied, eased toward the active
    // phoneme's so a boundary is not a step (see s_kLevelTauSec).
    float      m_vaCur        = 0.0f;
    float      m_faCur        = 0.0f;

    // Parallel fricative branch state: its low-pass and its F2 resonator.
    float      m_fricLp       = 0.0f;
    float      m_fricLp2      = 0.0f;
    float      m_fricLp3      = 0.0f;
    float      m_fricY1       = 0.0f;
    float      m_fricY2       = 0.0f;

    // Radiation-characteristic differentiator history (previous cascade
    // output sample) and the output low-pass state that rounds the top
    // octave off the differentiated signal.
    float      m_radPrev      = 0.0f;
    float      m_outLp        = 0.0f;
    float      m_outLp2       = 0.0f;
};
