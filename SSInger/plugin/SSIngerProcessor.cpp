#include "SSIngerProcessor.h"
#include "SSIngerEditor.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace ids = ssinger_ids;

namespace {

/* Value text a DAW's generic parameter view (REAPER's, Ardour's, Logic's
 * Controls view) shows and a screen reader speaks: units, not 0..1. Each
 * parser takes the text back, with or without the unit. */
juce::String semitoneText(float v, int)
{
    return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " st";
}

juce::String clockText(float v, int)
{
    const double mhz = std::pow(2.0, v / 12.0);
    return semitoneText(v, 0) + " (" + juce::String(mhz, 3) + " MHz)";
}

float leadingNumber(const juce::String& t)
{
    return t.trim().retainCharacters("+-0123456789.").getFloatValue();
}

juce::String volumeText(float v, int)
{
    if (v <= 0.0f)
        return "-inf dB";
    return juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB";
}

float volumeFromText(const juce::String& t)
{
    const auto s = t.trim();
    if (s.startsWithIgnoreCase("-inf"))
        return 0.0f;
    if (s.endsWithIgnoreCase("db"))
        return juce::Decibels::decibelsToGain(leadingNumber(s));
    return leadingNumber(s);
}

/* Tour-rig filter offsets: "+12 from chip 1", "0 (same as chip 1)". */
juce::String offsetText(int v, int)
{
    if (v == 0)
        return "0 (same as chip 1)";
    return (v > 0 ? "+" : "") + juce::String(v) + " from chip 1";
}

/* The first signed whole number in the text ("-30", "+12 from chip 1"). */
int offsetFromText(const juce::String& t)
{
    const auto s = t.trim();
    int i = 0, sign = 1, v = 0;
    if (i < s.length() && (s[i] == '+' || s[i] == '-'))
        sign = s[i++] == '-' ? -1 : 1;
    while (i < s.length() && s[i] >= '0' && s[i] <= '9')
        v = v * 10 + (int)(s[i++] - '0');
    return juce::jlimit(-255, 255, sign * v);
}

} // namespace

SSIngerProcessor::SSIngerProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Carrier", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    ssi263_default_params(&engDefaults);
    std::memcpy(engRom, ssi263_default_rom(), sizeof(engRom));
    tmp.assign(1024, 0.0);
    startTimerHz(10);
}

SSIngerProcessor::~SSIngerProcessor()
{
    stopTimer();
    const juce::ScopedLock sl(buildLock);
    freeSlots();
}

juce::AudioProcessorValueTreeState::ParameterLayout SSIngerProcessor::createLayout()
{
    using PID = juce::ParameterID;
    using FloatAttr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    /* MIDI front end. */
    layout.add(std::make_unique<juce::AudioParameterInt>(
        PID{ ids::phonBase, 1 }, "Phoneme channel", 1, 16, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PID{ ids::voices, 1 }, "Voices",
        juce::StringArray{ "SEQ (1 voice)", "Quad (tour rig)" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PID{ ids::embod, 1 }, "MIDI embodiment",
        juce::StringArray{ "N/N+1 phoneme+pitch", "Expander (PC)" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PID{ ids::ctlMap, 1 }, "Wheel map",
        juce::StringArray{ "Patent (bend=filter)", "Clock bend (Polaxis-style)" }, 1));
    /* SC-02 registers, straight off the chip (RESEARCH.md section 3). */
    layout.add(std::make_unique<juce::AudioParameterInt>(
        PID{ ids::artic, 1 }, "Articulation (ART)", 0, 7, 5));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        PID{ ids::filterFF, 1 }, "Filter frequency (FF)", 0, 255, 0xE4));
    /* Tour rig: chips 2-4 sit this many FF steps from chip 1; the mod
     * wheel (or the patent map's pitch wheel) moves all four and keeps the
     * spread. One voice: no effect. */
    for (int k = 0; k < 3; k++)
        layout.add(std::make_unique<juce::AudioParameterInt>(
            PID{ ids::ffOff[k], 1 }, "Chip " + juce::String(k + 2) + " filter offset (tour rig)", -255, 255, 0,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction(offsetText)
                .withValueFromStringFunction(offsetFromText)));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        PID{ ids::rate, 1 }, "Rate", 0, 15, 8));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        PID{ ids::glide, 1 }, "Pitch glide (R1)", 0, 7, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        PID{ ids::dur, 1 }, "Phoneme DUR", 0, 3, 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PID{ ids::velCurve, 1 }, "Velocity curve",
        juce::StringArray{ "Linear", "Log (:L:)" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID{ ids::bendRange, 1 }, "Bend range (st)",
        juce::NormalisableRange<float>(1.0f, 48.0f, 0.5f), 24.0f,
        FloatAttr().withLabel("st")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " st"; })
            .withValueFromStringFunction(leadingNumber)));
    /* System. */
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID{ ids::clockSt, 1 }, "Master clock (st)",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        FloatAttr().withLabel("st")
            .withStringFromValueFunction(clockText)
            .withValueFromStringFunction(leadingNumber)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PID{ ids::carrier, 1 }, "Carrier",
        juce::StringArray{ "Internal", "External (sidechain)" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID{ ids::volume, 1 }, "Volume",
        juce::NormalisableRange<float>(0.0f, 1.5f, 0.01f), 0.8f,
        FloatAttr().withLabel("dB")
            .withStringFromValueFunction(volumeText)
            .withValueFromStringFunction(volumeFromText)));
    return layout;
}

/* Message thread (or the host's prepare thread), never the audio thread:
 * the only place that allocates. */
void SSIngerProcessor::buildSlot(int i, double sampleRate)
{
    Slot& s = slots[i];
    if (s.ok)
        ssinger_bus_free(&s.bus);
    /* Parameters reach it on its first block (processBlock -> applyParams). */
    s.ok = ssinger_bus_init(&s.bus, sampleRate, i == 0 ? 1 : 4, &engDefaults, engRom) != 0;
}

void SSIngerProcessor::freeSlots()
{
    for (auto& s : slots) {
        if (s.ok)
            ssinger_bus_free(&s.bus);
        s.ok = false;
        s.state.store(slotStale);
    }
}

/* Rebuild whichever slot the audio thread left behind. */
void SSIngerProcessor::timerCallback()
{
    const juce::ScopedLock sl(buildLock);
    if (preparedRate <= 0.0)
        return;
    for (int i = 0; i < 2; i++) {
        int expected = slotStale;
        if (slots[i].state.compare_exchange_strong(expected, slotBuilding, std::memory_order_acq_rel)) {
            buildSlot(i, preparedRate);
            /* Ready only if it built; a failed build stays stale and is retried. */
            slots[i].state.store(slots[i].ok ? slotReady : slotStale, std::memory_order_release);
        }
    }
}

void SSIngerProcessor::applyParams(ssinger_bus_t& bus)
{
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    auto& cfg = bus.fw.cfg;
    /* a change lets go of the held phonemes: their note-offs could not reach them any more (#82) */
    ssinger_set_routing(&bus.fw, juce::jlimit(0, 15, (int)get(ids::phonBase) - 1),
                        (int)get(ids::embod) == 0 ? SG_EMB_PHONEME_PITCH : SG_EMB_EXPANDER);
    cfg.ctlmap = (int)get(ids::ctlMap) == 0 ? SG_MAP_PATENT : SG_MAP_POLAXIS;

    int newRate = (int)get(ids::rate);
    int newArt = (int)get(ids::artic);
    int newGlide = (int)get(ids::glide);
    /* First, so a Glide or Rate change below already writes R1 in the
     * mode's layout; crossing DUR 3 re-latches the chips' mode. */
    ssinger_bus_set_dur(&bus, (int)get(ids::dur));
    if (newRate != cfg.rate || newGlide != cfg.glide) {
        cfg.rate = newRate & 15;
        cfg.glide = newGlide & 7;
        for (int v = 0; v < bus.nvoices; v++)
            ssinger_write_inflection(&bus.fw, v, bus.fw.v[v].inflection);
    }
    if (newArt != cfg.articulation) {
        cfg.articulation = newArt & 7;
        for (int v = 0; v < bus.nvoices; v++)
            ssinger_write_r3(&bus.fw, v, bus.fw.v[v].amplitude);
    }
    cfg.vel_curve = (int)get(ids::velCurve);
    cfg.bend_range_st = get(ids::bendRange);

    /* FF is chip 1's filter and resets the shared filter for every chip,
     * as before; chips 2-4 keep their offsets from it (tour rig). */
    int newFF = (int)get(ids::filterFF);
    if (newFF != cfg.filter_ff)
        ssinger_set_filter(&bus.fw, newFF & 0xFF);
    for (int k = 0; k < 3; k++) {
        const int off = (int)get(ids::ffOff[k]);
        if (off != cfg.filter_off[k + 1])
            ssinger_set_filter_offset(&bus.fw, k + 1, off);
    }

    double base = 1000000.0 * std::pow(2.0, get(ids::clockSt) / 12.0);
    if (base != bus.fw.nominal_xck)
        ssinger_bus_master_write(base, &bus);

    volumeCache = get(ids::volume);
    carrierCache = (int)get(ids::carrier);
}

void SSIngerProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::ScopedLock sl(buildLock);
    preparedRate = sampleRate;
    for (int i = 0; i < 2; i++) {
        slots[i].state.store(slotBuilding);
        buildSlot(i, sampleRate);
        slots[i].state.store(slots[i].ok ? slotReady : slotStale, std::memory_order_release);
    }
    active = slotForVoicesParam(apvts.getRawParameterValue(ids::voices)->load());
}

void SSIngerProcessor::releaseResources()
{
}

bool SSIngerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    /* Carrier sidechain: anything the host offers, including disconnected. */
    return true;
}

/* Render [from, to) of the block. The carrier input and the output share
 * the host's channel memory (JUCE processes in place), so each carrier
 * sample is read before its output sample is written. */
void SSIngerProcessor::renderSpan(ssinger_bus_t& bus, juce::AudioBuffer<float>& out,
                                  const juce::AudioBuffer<float>& carrier, int from, int to)
{
    constexpr int maxCh = 2; /* isBusesLayoutSupported: mono or stereo out */
    const int nch = juce::jmin(maxCh, out.getNumChannels());
    const int ncar = carrierCache == 1 ? juce::jmin(nch, carrier.getNumChannels()) : 0;
    const float vol = volumeCache;
    while (from < to) {
        const int n = juce::jmin(to - from, (int)tmp.size());
        float* dst[maxCh] = {};
        const float* car[maxCh] = {};
        for (int ch = 0; ch < nch; ch++)
            dst[ch] = out.getWritePointer(ch, from);
        for (int ch = 0; ch < ncar; ch++)
            car[ch] = carrier.getReadPointer(ch, from);
        ssinger_bus_run(&bus, n, tmp.data());
        for (int i = 0; i < n; i++) {
            const float v = (float)std::tanh((float)(tmp[(size_t)i] * vol));
            for (int ch = 0; ch < nch; ch++) {
                if (ch < ncar) {
                    /* External carrier (sidechain monitor; TP1/TP2 filter
                     * injection is Phase 2 -- RESEARCH.md section 5.4). */
                    const float c = car[ch][i];
                    dst[ch][i] = (float)std::tanh(v * 0.5f + c * 0.5f * vol);
                } else {
                    dst[ch][i] = v;
                }
            }
        }
        from += n;
    }
}

void SSIngerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    /* Voices: swap to the other prebuilt system if it is ready; the one left
     * behind is rebuilt fresh on the message thread. */
    const int want = slotForVoicesParam(apvts.getRawParameterValue(ids::voices)->load());
    /* slotReady is only ever published after a successful build, so the
     * state alone says the slot is safe to use. */
    if (want != active && slots[want].state.load(std::memory_order_acquire) == slotReady) {
        slots[active].state.store(slotStale, std::memory_order_release);
        active = want;
    }
    Slot& s = slots[active];
    if (s.state.load(std::memory_order_acquire) != slotReady) {
        buffer.clear();
        return;
    }
    applyParams(s.bus);

    auto out = getBusBuffer(buffer, false, 0);
    const auto carrier = getTotalNumInputChannels() > 0 ? getBusBuffer(buffer, true, 0)
                                                        : juce::AudioBuffer<float>();
    const int numSamples = buffer.getNumSamples();

    int last = 0;
    for (const auto meta : midiMessages) {
        const int pos = juce::jlimit(last, numSamples, meta.samplePosition);
        renderSpan(s.bus, out, carrier, last, pos);
        for (int i = 0; i < meta.numBytes; i++)
            ssinger_bus_midi_byte(&s.bus, meta.data[i]);
        last = pos;
    }
    renderSpan(s.bus, out, carrier, last, numSamples);

    /* Input-only channels past the outputs carry no output. */
    for (int ch = out.getNumChannels(); ch < buffer.getNumChannels(); ch++)
        buffer.clear(ch, 0, numSamples);
}

juce::AudioProcessorEditor* SSIngerProcessor::createEditor()
{
    return new SSIngerEditor(*this);
}

void SSIngerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SSIngerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            /* Tell the host every value moved (CLAP: params rescan; VST3 and
             * AU re-read on their own), so its generic parameter view and a
             * screen reader show the restored values, not the old ones. */
            updateHostDisplay(ChangeDetails().withProgramChanged(true));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SSIngerProcessor();
}
