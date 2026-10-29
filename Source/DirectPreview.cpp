#include "DirectPreview.h"

#if JUCE_MAC
 #include <AudioToolbox/AudioToolbox.h>
 #include <juce_audio_basics/juce_audio_basics.h>

struct DirectPreview::Impl
{
    static constexpr int kFrames = 1024, kBuffers = 3;

    AudioQueueRef queue = nullptr;
    juce::AudioBuffer<float> planar { 2, kFrames };
    Fill* fill = nullptr;

    // thread de la file : remplit un tampon (stéréo entrelacé) et le remet dans la file
    static void callback (void* user, AudioQueueRef q, AudioQueueBufferRef b)
    {
        auto& self = *static_cast<Impl*> (user);
        const int n = (int) (b->mAudioDataBytesCapacity / (2 * sizeof (float)));
        self.planar.clear();
        (*self.fill) (self.planar.getArrayOfWritePointers(), 2, n);
        auto* out = static_cast<float*> (b->mAudioData);
        for (int i = 0; i < n; ++i)
        {
            out[2 * i]     = self.planar.getSample (0, i);
            out[2 * i + 1] = self.planar.getSample (1, i);
        }
        b->mAudioDataByteSize = (UInt32) (n * 2 * sizeof (float));
        AudioQueueEnqueueBuffer (q, b, 0, nullptr);
    }
};

DirectPreview::DirectPreview (Fill f) : fill (std::move (f)), impl (std::make_unique<Impl>()) { impl->fill = &fill; }
DirectPreview::~DirectPreview() { stop(); }

bool DirectPreview::start (double sampleRate)
{
    stop();
    AudioStreamBasicDescription f {};
    f.mSampleRate       = sampleRate;
    f.mFormatID         = kAudioFormatLinearPCM;
    f.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel   = 32;
    f.mFramesPerPacket  = 1;
    f.mBytesPerFrame    = 2 * sizeof (float);
    f.mBytesPerPacket   = f.mBytesPerFrame;
    if (AudioQueueNewOutput (&f, Impl::callback, impl.get(), nullptr, nullptr, 0, &impl->queue) != noErr)
    {
        impl->queue = nullptr;
        return false;
    }
    for (int i = 0; i < Impl::kBuffers; ++i)
    {
        AudioQueueBufferRef b = nullptr;
        if (AudioQueueAllocateBuffer (impl->queue, (UInt32) (Impl::kFrames * f.mBytesPerFrame), &b) == noErr)
            Impl::callback (impl.get(), impl->queue, b);   // amorce : la file démarre avec des tampons pleins
    }
    if (AudioQueueStart (impl->queue, nullptr) != noErr)
    {
        AudioQueueDispose (impl->queue, true);
        impl->queue = nullptr;
        return false;
    }
    fs = sampleRate;
    running = true;
    return true;
}

void DirectPreview::stop()
{
    if (impl->queue == nullptr) return;
    AudioQueueStop (impl->queue, true);      // attend la fin du rappel en cours
    AudioQueueDispose (impl->queue, true);
    impl->queue = nullptr;
    running = false;
}

#else

struct DirectPreview::Impl {};

DirectPreview::DirectPreview (Fill f) : fill (std::move (f)) {}
DirectPreview::~DirectPreview() = default;
bool DirectPreview::start (double) { return false; }
void DirectPreview::stop() {}

#endif
