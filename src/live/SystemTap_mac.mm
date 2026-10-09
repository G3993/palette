// macOS system-audio capture via a Core Audio process tap (lifted from Easel's
// AudioAnalyzer_mac.mm). Listen-only: playback is never altered. The first
// run prompts "Palette would like to record this computer's audio".
#include "live/SystemTap.h"
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>
#include <unistd.h>

namespace palette::live {

static OSStatus tapIOProc(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* in, const AudioTimeStamp*,
                          AudioBufferList*, const AudioTimeStamp*, void* client) {
    auto* self = static_cast<SystemTap*>(client);
    if (!self || !self->sink || !in || in->mNumberBuffers == 0) return noErr;
    const AudioBuffer& b = in->mBuffers[0];
    const float* data = (const float*)b.mData;
    unsigned ch = b.mNumberChannels ? b.mNumberChannels : 1;
    unsigned frames = b.mDataByteSize / sizeof(float) / ch;
    if (!data || frames == 0) return noErr;
    if (in->mNumberBuffers > 1 && ch == 1) {
        // planar: average the buffers into a mono stream
        static thread_local std::vector<float> mono;
        mono.assign(frames, 0.0f);
        for (unsigned bi = 0; bi < in->mNumberBuffers; ++bi) {
            const float* d = (const float*)in->mBuffers[bi].mData; if (!d) continue;
            for (unsigned i = 0; i < frames; ++i) mono[i] += d[i] / in->mNumberBuffers;
        }
        self->sink(mono.data(), frames, 1);
    } else {
        self->sink(data, frames, ch);
    }
    return noErr;
}

bool SystemTap::available() {
    if (@available(macOS 14.2, *)) return true;
    return false;
}

bool SystemTap::start(Sink s) {
    stop();
    sink = std::move(s); m_error.clear();
    if (@available(macOS 14.2, *)) {
        @autoreleasepool {
            // exclude ourselves so a future Palette playback path can't feed back
            NSArray<NSNumber*>* excluded = @[];
            AudioObjectPropertyAddress pa = {kAudioHardwarePropertyTranslatePIDToProcessObject, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            pid_t pid = getpid(); AudioObjectID me = kAudioObjectUnknown; UInt32 sz = sizeof(me);
            if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, sizeof(pid), &pid, &sz, &me) == noErr && me != kAudioObjectUnknown) excluded = @[@(me)];

            CATapDescription* desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:excluded];
            desc.name = @"PaletteSystemTap";
            desc.privateTap = YES;
            desc.muteBehavior = CATapUnmuted;
            AudioObjectID tapID = kAudioObjectUnknown;
            OSStatus err = AudioHardwareCreateProcessTap(desc, &tapID);
            if (err != noErr || tapID == kAudioObjectUnknown) {
                m_error = "System audio needs permission: System Settings > Privacy & Security > Screen & System Audio Recording (" + std::to_string((int)err) + ")";
                return false;
            }
            m_tapID = tapID;
            NSDictionary* agg = @{
                @(kAudioAggregateDeviceNameKey): @"Palette Tap Aggregate",
                @(kAudioAggregateDeviceUIDKey): [NSString stringWithFormat:@"com.palette.tapagg.%d", getpid()],
                @(kAudioAggregateDeviceIsPrivateKey): @YES,
                @(kAudioAggregateDeviceTapAutoStartKey): @YES,
                @(kAudioAggregateDeviceTapListKey): @[@{ @(kAudioSubTapUIDKey): desc.UUID.UUIDString, @(kAudioSubTapDriftCompensationKey): @YES }],
            };
            AudioObjectID aggID = kAudioObjectUnknown;
            err = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)agg, &aggID);
            if (err != noErr || aggID == kAudioObjectUnknown) { m_error = "tap aggregate device failed (" + std::to_string((int)err) + ")"; stop(); return false; }
            m_aggID = aggID;
            Float64 rate = 48000; AudioObjectPropertyAddress ra = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            sz = sizeof(rate); if (AudioObjectGetPropertyData(aggID, &ra, 0, nullptr, &sz, &rate) == noErr && rate > 0) m_rate = rate;
            AudioDeviceIOProcID proc = nullptr;
            err = AudioDeviceCreateIOProcID(aggID, tapIOProc, this, &proc);
            if (err != noErr || !proc) { m_error = "tap IOProc failed (" + std::to_string((int)err) + ")"; stop(); return false; }
            m_proc = (void*)proc;
            err = AudioDeviceStart(aggID, proc);
            if (err != noErr) { m_error = "tap start failed (" + std::to_string((int)err) + ")"; stop(); return false; }
            m_running = true;
            return true;
        }
    }
    m_error = "System audio capture needs macOS 14.2 or newer";
    return false;
}

void SystemTap::stop() {
    if (@available(macOS 14.2, *)) {
        if (m_aggID) {
            if (m_proc) { AudioDeviceStop(m_aggID, (AudioDeviceIOProcID)m_proc); AudioDeviceDestroyIOProcID(m_aggID, (AudioDeviceIOProcID)m_proc); m_proc = nullptr; }
            AudioHardwareDestroyAggregateDevice(m_aggID); m_aggID = 0;
        }
        if (m_tapID) { AudioHardwareDestroyProcessTap(m_tapID); m_tapID = 0; }
    }
    m_running = false;
}

} // namespace palette::live
