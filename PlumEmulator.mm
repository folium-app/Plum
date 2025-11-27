//
//  PlumEmulator.mm
//  Plum
//
//  Created by Jarrod Norwell on 8/11/2025.
//

#import "AudioOutput.h"
#import "PlumEmulator.h"

#import "Plum-Swift.h"

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mach/mach_time.h>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "common/cd-reader.h"
#define MIXER_IMPLEMENTATION
#include "common/mixer.h"
#include "core/clownmdemu.h"
#include "clowncd/clowncommon/clowncommon.h"

#define MAX_FILE_SIZE 8388608
#define SAMPLE_BUFFER_SIZE (MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME * 5) + 1

struct Object {
    ClownMDEmu_Callbacks callbacks;
    ClownMDEmu_Configuration configuration;
    ClownMDEmu_Constant constant;
    
    ClownMDEmu emu;
    ClownMDEmu_State emu_state;
    
    ClownCD_FileCallbacks reader_callbacks;
    CDReader_State reader_state;
    
    AudioOutput output;
    
    std::vector<uint8_t> rom;
    NSUInteger rom_size;
    
    std::vector<uint32_t> colours;
    std::vector<uint32_t> framebuffer;
    NSInteger width, height;
    
    std::jthread thread;
    std::atomic<bool> paused;
    std::mutex mutex;
    std::condition_variable_any cv;
    
    std::array<std::map<SGButton, bool>, 2> buttons;
} object;

@implementation PlumEmulator
-(PlumEmulator *)init {
    if (self = [super init]) {
        object.colours.resize(VDP_TOTAL_COLOURS);
        object.framebuffer.resize(VDP_MAX_SCANLINE_WIDTH * VDP_MAX_SCANLINES);
        
        object.rom.resize(MAX_FILE_SIZE);
        
        for (int i = 0; i < SGButtonCount; i++) {
            object.buttons[0][static_cast<SGButton>(i)] = false;
            object.buttons[1][static_cast<SGButton>(i)] = false;
        }
    } return self;
}

+(PlumEmulator *) sharedInstance {
    static PlumEmulator *sharedInstance = NULL;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}

-(NSArray<NSString *> *) insertCartridge:(NSURL *)url {
    ClownMDEmu_Parameters_Initialise(&object.emu, &object.configuration, &object.constant, &object.emu_state, &object.callbacks);
    
    object.callbacks.cartridge_read = [](void* user_data, cc_u32f address) -> cc_u8f {
        Object* object = (Object*)user_data;
        return address < object->rom_size ? object->rom.at(address) : 0;
    };
    
    object.callbacks.cartridge_written = [](void* user_data, cc_u32f address, cc_u8f value) {};
    
    object.callbacks.user_data = &object;
    
    object.callbacks.colour_updated = [](void* user_data, cc_u16f index, cc_u16f colour) {
        const cc_u32f r = colour & 0xF;
        const cc_u32f g = colour >> 4 & 0xF;
        const cc_u32f b = colour >> 8 & 0xF;
        
        ((Object*)user_data)->colours.at(index) = static_cast<uint32_t>(0xFF | (r << 8) | (r << 12) | (g << 16) | (g << 20) | (b << 24) | (b << 28));
    };
    
    object.callbacks.scanline_rendered = [](void* user_data, cc_u16f scanline, const cc_u8l* pixels,
                                            cc_u16f left_boundary, cc_u16f right_boundary,
                                            cc_u16f screen_width, cc_u16f screen_height) {
        Object* object = (Object*)user_data;
        
        object->width = screen_width;
        object->height = screen_height;
        
        const uint8_t* input = pixels + left_boundary;
        uint32_t* output = &object->framebuffer.at(scanline * object->width + left_boundary);
        for (int i = left_boundary; i < right_boundary; ++i)
            *output++ = object->colours.at(*input++);
    };
    
    object.callbacks.input_requested = [](void* user_data, cc_u8f player_id, ClownMDEmu_Button button) -> cc_bool {
        return object.buttons[player_id][static_cast<SGButton>(button)];
    };
    
    object.callbacks.fm_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                   void (*generate_fm_audio)(const struct ClownMDEmu* clownmdemu,
                                                                             cc_s16l* sample_buffer, size_t total_frames)) {
        Object* object = (Object*)user_data;
        generate_fm_audio(clownmdemu, object->output.MixerAllocateFMSamples(total_frames), total_frames);
    };
    
    object.callbacks.psg_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                    void (*generate_psg_audio)(const struct ClownMDEmu* clownmdemu,
                                                                               cc_s16l* sample_buffer, size_t total_frames)) {
        Object* object = (Object*)user_data;
        generate_psg_audio(clownmdemu, object->output.MixerAllocatePSGSamples(total_frames), total_frames);
    };
    
    object.callbacks.pcm_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                    void (*generate_pcm_audio)(const struct ClownMDEmu* clownmdemu,
                                                                               cc_s16l* sample_buffer, size_t total_frames)) {
        Object* object = (Object*)user_data;
        generate_pcm_audio(clownmdemu, object->output.MixerAllocatePCMSamples(total_frames), total_frames);
    };
    
    object.callbacks.cdda_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                     void (*generate_cdda_audio)(const struct ClownMDEmu* clownmdemu,
                                                                                 cc_s16l* sample_buffer, size_t total_frames)) {
        Object* object = (Object*)user_data;
        generate_cdda_audio(clownmdemu, object->output.MixerAllocateCDDASamples(total_frames), total_frames);
    };
    
    ClownMDEmu_SetLogCallback([](void* const user_data, const char* const format, va_list args) {
        
    }, NULL);
    
    NSString *extension = [url.pathExtension lowercaseString];
    if ([extension isEqualToString:@"cue"])
        return [self insertMegaDrive:url];
    else
        return [self insertGenesis:url];
}

-(NSArray<NSString *> *) insertGenesis:(NSURL *)url {
    NSData *data = [NSData dataWithContentsOfURL:url];
    object.rom_size = [data length];
    [data getBytes:object.rom.data() length:object.rom_size];
    
    auto region = static_cast<char>(object.rom.at(0x1F0));
    
    object.configuration.general.region = [@[@"E", @"U"] containsObject:@(region)] ? CLOWNMDEMU_REGION_OVERSEAS : CLOWNMDEMU_REGION_DOMESTIC;
    object.configuration.general.tv_standard = [@[@"J", @"U"] containsObject:@(region)] ? CLOWNMDEMU_TV_STANDARD_NTSC : CLOWNMDEMU_TV_STANDARD_PAL;
    object.output.SetPALMode(object.configuration.general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL);
    
    ClownMDEmu_Constant_Initialise(&object.constant);
    ClownMDEmu_State_Initialise(&object.emu_state);
    ClownMDEmu_Reset(&object.emu, cc_false, [[NSNumber numberWithUnsignedInteger:object.rom_size] unsignedLongValue]);
    
    std::string io(reinterpret_cast<const char*>(&object.rom.at(0x190)), reinterpret_cast<const char*>(&object.rom.at(0x190 + 16)));
    
    NSMutableArray *characterArray = [NSMutableArray array];
    NSString *ios = [[NSString stringWithCString:io.c_str() encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    for (int i = 0; i < ios.length; i++) {
        unichar character = [ios characterAtIndex:i];
        NSString *characterAsString = [NSString stringWithFormat:@"%C", character]; // Use %C for unichar
        [characterArray addObject:characterAsString];
    }
    
    return characterArray;
}

-(NSArray<NSString *> *) insertMegaDrive:(NSURL *)url {
    NSString *fileName = [[[CUE alloc] init:url] firstTrackFilename];
    
    NSURL *bin = [[url URLByDeletingLastPathComponent] URLByAppendingPathComponent:fileName];
    
    NSData *data = [NSData dataWithContentsOfURL:bin];
    object.rom_size = [data length];
    [data getBytes:object.rom.data() length:object.rom_size];
    
    object.callbacks.cd_seeked = [](void* user_data, cc_u32f sector_index) {
        Object* object = (Object*)user_data;
        CDReader_SeekToSector(&object->reader_state, sector_index);
    };
    
    object.callbacks.cd_sector_read = [](void* user_data, cc_u16l *buffer) {
        Object* object = (Object*)user_data;
        CDReader_ReadSector(&object->reader_state, buffer);
    };
    
    object.callbacks.cd_track_seeked = [](void* user_data, cc_u16f track_index, ClownMDEmu_CDDAMode mode) -> cc_bool {
        Object* object = (Object*)user_data;
        CDReader_PlaybackSetting playback_setting;
        switch (mode) {
            default:
                SDL_assert(false);
                return cc_false;
            case CLOWNMDEMU_CDDA_PLAY_ALL:
                playback_setting = CDReader_PlaybackSetting::CDREADER_PLAYBACK_ALL;
                break;
            case CLOWNMDEMU_CDDA_PLAY_ONCE:
                playback_setting = CDReader_PlaybackSetting::CDREADER_PLAYBACK_ONCE;
                break;
            case CLOWNMDEMU_CDDA_PLAY_REPEAT:
                playback_setting = CDReader_PlaybackSetting::CDREADER_PLAYBACK_REPEAT;
                break;
        } return CDReader_PlayAudio(&object->reader_state, track_index, playback_setting);
    };
    
    object.callbacks.cd_audio_read = [](void* user_data, cc_s16l* sample_buffer, size_t total_frames) -> size_t {
        Object* object = (Object*)user_data;
        return CDReader_ReadAudio(&object->reader_state, sample_buffer, total_frames);
    };
    
    object.reader_callbacks.read = [](void *buffer, size_t size, size_t count, void *stream) -> size_t {
        if (size == 0 || count == 0)
            return 0;
        return SDL_ReadIO(static_cast<SDL_IOStream*>(stream), buffer, size * count) / size;
    };
    
    object.reader_callbacks.open = [](const char *filename, ClownCD_FileMode mode) -> void* {
        const char *mode_string;
        switch (mode) {
            case CLOWNCD_RB:
                mode_string = "rb";
                break;
            case CLOWNCD_WB:
                mode_string = "wb";
                break;
            default:
                return nullptr;
        } return SDL_IOFromFile(filename, mode_string);
    };
    
    object.reader_callbacks.close = [](void *stream) -> int {
        return SDL_CloseIO(static_cast<SDL_IOStream*>(stream));
    };
    
    object.reader_callbacks.seek = [](void *stream, long position, ClownCD_FileOrigin origin) -> int {
        SDL_IOWhence whence;
        switch (origin) {
            case CLOWNCD_SEEK_SET:
                whence = SDL_IO_SEEK_SET;
                break;
            case CLOWNCD_SEEK_CUR:
                whence = SDL_IO_SEEK_CUR;
                break;
            case CLOWNCD_SEEK_END:
                whence = SDL_IO_SEEK_END;
                break;
            default:
                return -1;
        } return SDL_SeekIO(static_cast<SDL_IOStream*>(stream), position, whence) == -1 ? -1 : 0;
    };
    
    object.reader_callbacks.tell = [](void *stream) -> long {
        const auto position = SDL_TellIO(static_cast<SDL_IOStream*>(stream));
        if (position < LONG_MIN || position > LONG_MAX)
            return -1L;
        return position;
    };
    
    object.reader_callbacks.write = [](const void *buffer, size_t size, size_t count, void *stream) -> size_t {
        if (size == 0 || count == 0)
            return 0;
        return SDL_WriteIO(static_cast<SDL_IOStream*>(stream), buffer, size * count) / size;
    };
    
    CDReader_Initialise(&object.reader_state);
    CDReader_Open(&object.reader_state, SDL_IOFromFile([url.path UTF8String], "rb"), [url.path UTF8String], &object.reader_callbacks);
    
    auto region = static_cast<char>(object.rom.at(0x200));
    
    object.configuration.general.region = [@[@"E", @"U"] containsObject:@(region)] ? CLOWNMDEMU_REGION_OVERSEAS : CLOWNMDEMU_REGION_DOMESTIC;
    object.configuration.general.tv_standard = [@[@"J", @"U"] containsObject:@(region)] ? CLOWNMDEMU_TV_STANDARD_NTSC : CLOWNMDEMU_TV_STANDARD_PAL;
    object.output.SetPALMode(object.configuration.general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL);
    
    ClownMDEmu_Constant_Initialise(&object.constant);
    ClownMDEmu_State_Initialise(&object.emu_state);
    ClownMDEmu_Reset(&object.emu, cc_true, [[NSNumber numberWithUnsignedInteger:object.rom_size] unsignedLongValue]);
    
    std::string io(reinterpret_cast<const char*>(&object.rom.at(0x1A0)), reinterpret_cast<const char*>(&object.rom.at(0x1A0 + 16)));
    
    NSMutableArray *characterArray = [NSMutableArray array];
    NSString *ios = [[NSString stringWithCString:io.c_str() encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    for (int i = 0; i < ios.length; i++) {
        unichar character = [ios characterAtIndex:i];
        NSString *characterAsString = [NSString stringWithFormat:@"%C", character]; // Use %C for unichar
        [characterArray addObject:characterAsString];
    }
    
    return characterArray;
}

-(void) start {
    object.thread = std::jthread([&](std::stop_token token) {
        using namespace std::chrono;

        const int fps = object.configuration.general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? 50 : 60;
        const auto frameDuration = duration<double>(1.0 / fps);

        while (!token.stop_requested()) {
            {
                std::unique_lock lock(object.mutex);
                object.cv.wait(lock, token, []() {
                    return !object.paused.load();
                });
                
                if (token.stop_requested())
                    break;
            }
            
            auto frameStart = steady_clock::now();
            
            object.output.MixerBegin();
            ClownMDEmu_Iterate(&object.emu);
            object.output.MixerEnd();
            
            if (auto buffer = [[PlumEmulator sharedInstance] framebuffer])
                buffer(object.framebuffer.data(), object.width, object.height);
            
            auto frameEnd = steady_clock::now();
            auto elapsed = frameEnd - frameStart;
            if (elapsed < frameDuration)
                std::this_thread::sleep_for(frameDuration - elapsed);
        }
    });
}

-(void) pause:(BOOL)pause {
    if (pause)
        object.paused.store(true);
    else {
        object.paused.store(false);
        object.cv.notify_all();
    }
}

-(BOOL) isPaused {
    return object.paused.load();
}

-(void) stop {
    object.thread.request_stop();
    if (object.thread.joinable())
        object.thread.join();
    
    object.paused.store(false);
}

-(void) updateSettings {
    NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];
    
    object.configuration.general.region = [userDefaults integerForKey:@"plum.v1.35.region"] == 0 ? CLOWNMDEMU_REGION_DOMESTIC : CLOWNMDEMU_REGION_OVERSEAS ;
    object.configuration.general.tv_standard = [userDefaults integerForKey:@"plum.v1.35.tvStandard"] == 0 ? CLOWNMDEMU_TV_STANDARD_NTSC : CLOWNMDEMU_TV_STANDARD_PAL;
}

-(void) input:(NSInteger)slot button:(uint32_t)button pressed:(BOOL)pressed {
    object.buttons[slot][static_cast<SGButton>(button)] = pressed;
}
@end
