// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

// macOS helpers for b2b: permissions + ScreenCaptureKit screen grabs.
// Compiled with -fobjc-arc (see CMakeLists.txt).
#include "MacSupport.h"
#include "AppCapture.h"
#import <CoreMedia/CoreMedia.h>
#include <atomic>
#include <unistd.h>
#include <vector>

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <algorithm>
#include <mutex>

namespace macsupport {

bool hostDeclares(const char* key) {
    @autoreleasepool {
        id v = [[NSBundle mainBundle] objectForInfoDictionaryKey:[NSString stringWithUTF8String:key]];
        return v != nil;
    }
}

bool hasScreenAccess() { return CGPreflightScreenCaptureAccess(); }

void requestScreenAccess() {
    static std::once_flag once;
    std::call_once(once, [] { CGRequestScreenCaptureAccess(); });
}

// ---- shareable content cache (the query is slow; refresh every few seconds)
static std::mutex gMtx;
static SCShareableContent* gContent = nil;
static double gContentAt = 0;

static SCShareableContent* shareableContent() API_AVAILABLE(macos(12.3)) {
    const double now = [NSDate timeIntervalSinceReferenceDate];
    {
        std::lock_guard<std::mutex> l(gMtx);
        if (gContent != nil && now - gContentAt < 3.0) return gContent;
    }
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block SCShareableContent* result = nil;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* c, NSError* e) {
                                                     (void) e;
                                                     result = c;
                                                     dispatch_semaphore_signal(sem);
                                                 }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t) (2 * NSEC_PER_SEC)));
    std::lock_guard<std::mutex> l(gMtx);
    if (result != nil) { gContent = result; gContentAt = now; }
    return gContent;
}

std::vector<std::string> screenNames() {
    std::vector<std::string> names;
    if (@available(macOS 12.3, *)) {
        if (hasScreenAccess()) {
            @autoreleasepool {
                SCShareableContent* c = shareableContent();
                NSUInteger i = 0;
                for (SCDisplay* d in c.displays) {
                    ++i;
                    names.push_back("Display " + std::to_string(i) + "  (" + std::to_string((long) d.width) + "x"
                                    + std::to_string((long) d.height) + ")");
                }
            }
        }
    }
    if (names.empty()) names.push_back("Main display");
    return names;
}

bool grabScreen(int index, int maxW, int maxH, std::vector<uint8_t>& bgra, int& w, int& h) {
    if (!hasScreenAccess()) { requestScreenAccess(); return false; }
    if (@available(macOS 14.0, *)) {
        @autoreleasepool {
            SCShareableContent* c = shareableContent();
            if (c == nil || c.displays.count == 0) return false;
            NSUInteger idx = (NSUInteger) std::clamp(index, 0, (int) c.displays.count - 1);
            SCDisplay* d = c.displays[idx];

            const double sw = (double) d.width, sh = (double) d.height;
            const double sc = std::min({ 1.0, maxW / sw, maxH / sh });
            w = ((int) (sw * sc)) & ~1;
            h = ((int) (sh * sc)) & ~1;
            if (w < 16 || h < 16) return false;

            SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:d excludingWindows:@[]];
            SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
            cfg.width = (size_t) w;
            cfg.height = (size_t) h;
            cfg.showsCursor = YES;
            cfg.scalesToFit = YES;

            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block CGImageRef shot = NULL;
            [SCScreenshotManager captureImageWithFilter:filter
                                          configuration:cfg
                                      completionHandler:^(CGImageRef img, NSError* e) {
                                          (void) e;
                                          if (img) shot = CGImageRetain(img);
                                          dispatch_semaphore_signal(sem);
                                      }];
            if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t) (NSEC_PER_SEC / 2))) != 0) return false;
            if (!shot) return false;

            bgra.assign((size_t) w * h * 4, 0);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(bgra.data(), (size_t) w, (size_t) h, 8, (size_t) w * 4, cs,
                                                     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
            if (ctx) {
                CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
                CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), shot);
                CGContextRelease(ctx);
            }
            CGColorSpaceRelease(cs);
            CGImageRelease(shot);
            return ctx != NULL;
        }
    }
    return false; // screen share needs macOS 14+
}

} // namespace macsupport

// ============================================================== app audio (this DAW's own output)
API_AVAILABLE(macos(13.0))
@interface B2BAppAudioSink : NSObject <SCStreamOutput, SCStreamDelegate>
@end

namespace {
std::mutex gAppMtx;
appcapture::Callback gAppCb;
std::atomic<double> gAppRate { 0.0 };
id gAppStream = nil; // SCStream
id gAppSink = nil;
}

@implementation B2BAppAudioSink
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sb ofType:(SCStreamOutputType)type API_AVAILABLE(macos(13.0)) {
    (void) stream;
    if (type != SCStreamOutputTypeAudio || !CMSampleBufferIsValid(sb)) return;
    CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription(sb);
    const AudioStreamBasicDescription* asbd = fd ? CMAudioFormatDescriptionGetStreamBasicDescription(fd) : nullptr;
    if (!asbd || !(asbd->mFormatFlags & kAudioFormatFlagIsFloat) || asbd->mBitsPerChannel != 32) return;
    gAppRate = asbd->mSampleRate;
    size_t ablSize = 0;
    CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sb, &ablSize, nullptr, 0, nullptr, nullptr, 0, nullptr);
    if (ablSize == 0) return;
    std::vector<uint8_t> mem(ablSize);
    auto* abl = reinterpret_cast<AudioBufferList*>(mem.data());
    CMBlockBufferRef block = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sb, nullptr, abl, ablSize, nullptr, nullptr,
                                                                kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr)
        return;
    const int n = (int) CMSampleBufferGetNumSamples(sb);
    std::vector<float> L((size_t) n), R((size_t) n);
    if (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) {
        const float* a = abl->mNumberBuffers > 0 ? (const float*) abl->mBuffers[0].mData : nullptr;
        const float* b = abl->mNumberBuffers > 1 ? (const float*) abl->mBuffers[1].mData : a;
        if (a) for (int i = 0; i < n; ++i) { L[(size_t) i] = a[i]; R[(size_t) i] = b[i]; }
    } else if (abl->mNumberBuffers > 0) {
        const float* f = (const float*) abl->mBuffers[0].mData;
        const int ch = (int) std::max<UInt32>(1, asbd->mChannelsPerFrame);
        for (int i = 0; i < n; ++i) { L[(size_t) i] = f[i * ch]; R[(size_t) i] = f[i * ch + (ch > 1 ? 1 : 0)]; }
    }
    if (block) CFRelease(block);
    appcapture::Callback cb;
    {
        std::lock_guard<std::mutex> l(gAppMtx);
        cb = gAppCb;
    }
    if (cb) cb(L.data(), R.data(), n);
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error API_AVAILABLE(macos(13.0)) {
    (void) stream; (void) error;
    gAppRate = 0.0;
}
@end

namespace appcapture {

std::string start(double sampleRate, Callback cb) {
    stop();
    if (@available(macOS 13.0, *)) {
        if (!macsupport::hasScreenAccess()) {
            macsupport::requestScreenAccess();
            return "macOS: allow Screen Recording for your DAW (System Settings > Privacy & Security > Screen Recording), then restart the DAW.";
        }
        @autoreleasepool {
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block SCShareableContent* content = nil;
            [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                       onScreenWindowsOnly:NO
                                                         completionHandler:^(SCShareableContent* c, NSError* e) {
                                                             (void) e;
                                                             content = c;
                                                             dispatch_semaphore_signal(sem);
                                                         }];
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t) (3 * NSEC_PER_SEC)));
            if (content == nil || content.displays.count == 0) return "Can't read the screen/app list (Screen Recording permission?)";
            SCRunningApplication* me = nil;
            const pid_t pid = getpid();
            for (SCRunningApplication* a in content.applications)
                if (a.processID == pid) { me = a; break; }
            if (me == nil) return "Can't find your DAW in the system's app list";

            SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:content.displays.firstObject
                                                         includingApplications:@[ me ]
                                                              exceptingWindows:@[]];
            SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
            cfg.capturesAudio = YES;
            cfg.excludesCurrentProcessAudio = NO; // we ARE the DAW: that's exactly what we want
            cfg.sampleRate = (NSInteger) sampleRate;
            cfg.channelCount = 2;
            cfg.width = 16;
            cfg.height = 16;
            cfg.minimumFrameInterval = CMTimeMake(1, 1); // video is required by the API; keep it tiny
            cfg.showsCursor = NO;

            B2BAppAudioSink* sink = [[B2BAppAudioSink alloc] init];
            SCStream* stream = [[SCStream alloc] initWithFilter:filter configuration:cfg delegate:sink];
            NSError* err = nil;
            dispatch_queue_t q = dispatch_queue_create("b2b.appaudio", DISPATCH_QUEUE_SERIAL);
            if (![stream addStreamOutput:sink type:SCStreamOutputTypeAudio sampleHandlerQueue:q error:&err])
                return "Can't capture app audio: " + std::string(err.localizedDescription.UTF8String ?: "");
            [stream addStreamOutput:sink type:SCStreamOutputTypeScreen sampleHandlerQueue:q error:nil];
            {
                std::lock_guard<std::mutex> l(gAppMtx);
                gAppCb = std::move(cb);
            }
            dispatch_semaphore_t started = dispatch_semaphore_create(0);
            __block NSError* startErr = nil;
            [stream startCaptureWithCompletionHandler:^(NSError* e) {
                startErr = e;
                dispatch_semaphore_signal(started);
            }];
            if (dispatch_semaphore_wait(started, dispatch_time(DISPATCH_TIME_NOW, (int64_t) (4 * NSEC_PER_SEC))) != 0)
                return "App audio capture didn't start (timeout)";
            if (startErr != nil) {
                std::lock_guard<std::mutex> l(gAppMtx);
                gAppCb = nullptr;
                return "Can't capture app audio: " + std::string(startErr.localizedDescription.UTF8String ?: "");
            }
            std::lock_guard<std::mutex> l(gAppMtx);
            gAppStream = stream;
            gAppSink = sink;
        }
        return {};
    }
    return "App audio needs macOS 13 or newer";
}

void stop() {
    id stream = nil;
    {
        std::lock_guard<std::mutex> l(gAppMtx);
        stream = gAppStream;
        gAppStream = nil;
        gAppCb = nullptr;
    }
    if (@available(macOS 13.0, *)) {
        if (stream != nil) {
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [(SCStream*) stream stopCaptureWithCompletionHandler:^(NSError* e) {
                (void) e;
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t) (2 * NSEC_PER_SEC)));
        }
    }
    std::lock_guard<std::mutex> l(gAppMtx);
    gAppSink = nil;
    gAppRate = 0.0;
}

double actualRate() { return gAppRate.load(); }

} // namespace appcapture
