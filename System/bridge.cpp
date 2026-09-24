//
//  bridge.cpp
//  Plum
//
//  Created by Jarrod Norwell on 27/8/2026.
//

#import "Plum-Swift.h"
using namespace Plum;

#include "clowncd/audio_output.h"
#include "clowncd/bridge.h"

#include "clowncd/clowncommon/clowncommon.h"
#include "common/cd-reader.h"
#include "core/clownmdemu.h"

#include <array>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#define MAX_FILE_SIZE 8388608
#define SAMPLE_BUFFER_SIZE (MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME * 5) + 1
#define SECOND_NS 1000000000

class CUE
{
public:
    explicit CUE(std::string path)
    : path_(path)
    {
    }
    
    std::string firstTrackFilename() const
    {
        std::ifstream file(path_);
        
        if (!file)
            return {};
        
        std::string line;
        
        while (std::getline(file, line))
        {
            // Trim leading/trailing whitespace.
            const auto first =
            line.find_first_not_of(" \t\r\n");
            
            if (first == std::string::npos)
                continue;
            
            const auto last =
            line.find_last_not_of(" \t\r\n");
            
            const std::string trimmed =
            line.substr(first, last - first + 1);
            
            if (trimmed.rfind("FILE ", 0) != 0)
                continue;
            
            const auto firstQuote =
            trimmed.find('"');
            
            const auto lastQuote =
            trimmed.rfind('"');
            
            if (firstQuote == std::string::npos ||
                lastQuote == std::string::npos ||
                firstQuote == lastQuote)
            {
                continue;
            }
            
            return trimmed.substr(
                                  firstQuote + 1,
                                  lastQuote - firstQuote - 1
                                  );
        }
        
        return {};
    }
    
private:
    std::string path_;
};

struct CPPPlum {
    PlumCommon plumCommon{PlumCommon::init()};
    // PlumSystem plumSystem{PlumSystem::init()};
    
    ClownMDEmu_Callbacks callbacks;
    ClownMDEmu_Configuration configuration;
    ClownMDEmu_Constant constant;
    
    ClownMDEmu emu;
    ClownMDEmu_State emu_state;
    
    ClownCD_FileCallbacks reader_callbacks;
    CDReader_State reader_state;
    
    AudioOutput output;
    
    std::vector<uint8_t> rom;
    uint rom_size;
    
    std::vector<uint32_t> colours;
    std::vector<uint32_t> framebuffer;
    int width, height;
    std::array<std::map<uint32_t, bool>, 2> buttons;
    
    std::filesystem::path plum_path, memory_cards_path, system_data_path;
    
    std::atomic<bool> paused, running;
    std::jthread thread;
    std::mutex mutex;
    std::condition_variable_any cv;
} p_cntnr;

std::string getGenesisRegion(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
        return {};

    // Genesis/Mega Drive region field: 0x1F0, 3 bytes
    file.seekg(0x1F0);

    std::array<char, 3> region{};
    file.read(region.data(), region.size());

    if (file.gcount() != 3)
        return {};

    return std::string(region.data(), 3);
}

void plum::print_about(void) {
    printf("Welcome to Plum\n");
    printf("SEGA Genesis emulator based on ClownCDEmu\n");
}


void plum::initialize_paths(void) {
    auto plumDirectoryURL{p_cntnr.plumCommon.getPlumDirectoryURL()};
    if (plumDirectoryURL.isSome()) {
        auto plum_path{std::filesystem::path{plumDirectoryURL.get()}};
        
        p_cntnr.plum_path = plum_path;
        p_cntnr.memory_cards_path = plum_path / "memory_cards";
        p_cntnr.system_data_path = plum_path / "system_data";
    }
}

void plum::initialize_system(void) {
    p_cntnr.colours.resize(VDP_TOTAL_COLOURS);
    p_cntnr.framebuffer.resize(VDP_MAX_SCANLINE_WIDTH * VDP_MAX_SCANLINES);
    
    p_cntnr.rom.resize(MAX_FILE_SIZE);
    
    for (const auto& button : PlumButton::getAllCases())
        p_cntnr.buttons[0][button.getUint32()] = p_cntnr.buttons[1][button.getUint32()] = false;
}


void plum::destroy_system(void) {
    
}


void plum::insert_disc(std::string path) {
    ClownMDEmu_Parameters_Initialise(&p_cntnr.emu, &p_cntnr.configuration, &p_cntnr.constant, &p_cntnr.emu_state, &p_cntnr.callbacks);
    
    p_cntnr.callbacks.cartridge_read = [](void* user_data, cc_u32f address) -> cc_u8f {
        CPPPlum* object = (CPPPlum*)user_data;
        return address < object->rom_size ? object->rom.at(address) : 0;
    };
    
    p_cntnr.callbacks.cartridge_written = [](void* user_data, cc_u32f address, cc_u8f value) {};
    
    p_cntnr.callbacks.user_data = &p_cntnr;
    
    p_cntnr.callbacks.colour_updated = [](void* user_data, cc_u16f index, cc_u16f colour) {
        const cc_u32f r = colour & 0xF;
        const cc_u32f g = colour >> 4 & 0xF;
        const cc_u32f b = colour >> 8 & 0xF;
        
        ((CPPPlum*)user_data)->colours.at(index) = static_cast<uint32_t>(0xFF | (r << 8) | (r << 12) | (g << 16) | (g << 20) | (b << 24) | (b << 28));
    };
    
    p_cntnr.callbacks.scanline_rendered = [](void* user_data, cc_u16f scanline, const cc_u8l* pixels,
                                            cc_u16f left_boundary, cc_u16f right_boundary,
                                            cc_u16f screen_width, cc_u16f screen_height) {
        CPPPlum* object = (CPPPlum*)user_data;
        
        object->width = screen_width;
        object->height = screen_height;
        
        const uint8_t* input = pixels + left_boundary;
        uint32_t* output = &object->framebuffer.at(scanline * object->width + left_boundary);
        for (int i = left_boundary; i < right_boundary; ++i)
            *output++ = object->colours.at(*input++);
    };
    
    p_cntnr.callbacks.input_requested = [](void* user_data, cc_u8f player_id, ClownMDEmu_Button button) -> cc_bool {
        return p_cntnr.buttons[player_id][button];
    };
    
    p_cntnr.callbacks.fm_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                   void (*generate_fm_audio)(const struct ClownMDEmu* clownmdemu,
                                                                             cc_s16l* sample_buffer, size_t total_frames)) {
        CPPPlum* object = (CPPPlum*)user_data;
        generate_fm_audio(clownmdemu, object->output.MixerAllocateFMSamples(total_frames), total_frames);
    };
    
    p_cntnr.callbacks.psg_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                    void (*generate_psg_audio)(const struct ClownMDEmu* clownmdemu,
                                                                               cc_s16l* sample_buffer, size_t total_frames)) {
        CPPPlum* object = (CPPPlum*)user_data;
        generate_psg_audio(clownmdemu, object->output.MixerAllocatePSGSamples(total_frames), total_frames);
    };
    
    p_cntnr.callbacks.pcm_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                    void (*generate_pcm_audio)(const struct ClownMDEmu* clownmdemu,
                                                                               cc_s16l* sample_buffer, size_t total_frames)) {
        CPPPlum* object = (CPPPlum*)user_data;
        generate_pcm_audio(clownmdemu, object->output.MixerAllocatePCMSamples(total_frames), total_frames);
    };
    
    p_cntnr.callbacks.cdda_audio_to_be_generated = [](void* user_data, const struct ClownMDEmu* clownmdemu, size_t total_frames,
                                                     void (*generate_cdda_audio)(const struct ClownMDEmu* clownmdemu,
                                                                                 cc_s16l* sample_buffer, size_t total_frames)) {
        CPPPlum* object = (CPPPlum*)user_data;
        generate_cdda_audio(clownmdemu, object->output.MixerAllocateCDDASamples(total_frames), total_frames);
    };
    
    ClownMDEmu_SetLogCallback([](void* const user_data, const char* const format, va_list args) {
        
    }, NULL);
    
    
    p_cntnr.callbacks.save_file_opened_for_reading = [](void *user_data, const char *filename) -> cc_bool {
        printf("save_file_opened_for_reading\n");
        return false;
    };
    
    p_cntnr.callbacks.save_file_read = [](void *user_data) -> cc_s16f {
        printf("save_file_read\n");
        return 0;
    };
    
    p_cntnr.callbacks.save_file_opened_for_writing = [](void *user_data, const char *filename) -> cc_bool {
        printf("save_file_opened_for_writing\n");
        return false;
    };
    
    p_cntnr.callbacks.save_file_written = [](void *user_data, cc_u8f byte) {
        printf("save_file_written\n");
    };
    
    p_cntnr.callbacks.save_file_closed = [](void *user_data) {
        printf("save_file_closed\n");
    };
    
    p_cntnr.callbacks.save_file_removed = [](void *user_data, const char *filename) -> cc_bool {
        printf("save_file_removed\n");
        return false;
    };
    
    p_cntnr.callbacks.save_file_size_obtained = [](void *user_data, const char *filename, size_t *size) -> cc_bool {
        printf("save_file_size_obtained\n");
        return false;
    };
    
    
    if (path.find("cue") != std::string::npos)
        plum::insert_megadrive(path);
    else
        plum::insert_genesis(path);
}

void plum::insert_genesis(std::string path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file)
        return;

    const std::streamsize fileSize = file.tellg();

    if (fileSize < 0)
        return;

    file.seekg(0, std::ios::beg);

    p_cntnr.rom_size = static_cast<uint>(fileSize);

    if (p_cntnr.rom.size() < p_cntnr.rom_size)
        p_cntnr.rom.resize(p_cntnr.rom_size);

    file.read(
        reinterpret_cast<char*>(p_cntnr.rom.data()),
        fileSize
    );

    if (!file)
        return;

    auto region{getGenesisRegion(path)};
    
    auto is_ntsc = [region](std::string path) -> bool {
        return region.find("4") != std::string::npos || region.find("E") != std::string::npos || region.find("U") != std::string::npos;
    };

    p_cntnr.configuration.general.region =
    is_ntsc(path) ? CLOWNMDEMU_REGION_OVERSEAS
    : CLOWNMDEMU_REGION_DOMESTIC;
    
    p_cntnr.configuration.general.tv_standard =
    is_ntsc(path) ? CLOWNMDEMU_TV_STANDARD_NTSC
    : CLOWNMDEMU_TV_STANDARD_PAL;

    p_cntnr.output.SetPALMode(
        p_cntnr.configuration.general.tv_standard ==
        CLOWNMDEMU_TV_STANDARD_PAL
    );

    ClownMDEmu_Constant_Initialise(
        &p_cntnr.constant
    );

    ClownMDEmu_State_Initialise(
        &p_cntnr.emu_state
    );

    ClownMDEmu_Reset(
        &p_cntnr.emu,
        cc_false,
        p_cntnr.rom_size
    );

    // Read the 16-byte string at ROM offset 0x190.
    const std::string io(
        reinterpret_cast<const char*>(
            p_cntnr.rom.data() + 0x190
        ),
        16
    );

    // Equivalent to stringByTrimmingCharactersInSet:
    std::string ios = io;

    const auto first =
        ios.find_first_not_of(" \t\n\r");

    if (first == std::string::npos)
    {
        ios.clear();
    }
    else
    {
        const auto last =
            ios.find_last_not_of(" \t\n\r");

        ios = ios.substr(
            first,
            last - first + 1
        );
    }

    // Equivalent to NSMutableArray.
    std::vector<std::string> characterArray;

    for (char character : ios)
    {
        characterArray.emplace_back(1, character);
    }

    // characterArray now contains each character separately.
}

void plum::insert_megadrive(std::string path)
{
    namespace fs = std::filesystem;

    const fs::path cuePath(path);

    // Equivalent to:
    // [[CUE alloc] init:url] firstTrackFilename
    CUE cue(cuePath);
    const std::string fileName = cue.firstTrackFilename();

    // Equivalent to:
    // [[url URLByDeletingLastPathComponent] URLByAppendingPathComponent:fileName]
    const fs::path binPath = cuePath.parent_path() / fileName;

    // Equivalent to NSData dataWithContentsOfURL:
    std::ifstream file(binPath, std::ios::binary | std::ios::ate);

    if (!file)
        return;

    const std::streamsize fileSize = file.tellg();

    if (fileSize < 0)
        return;

    file.seekg(0, std::ios::beg);

    p_cntnr.rom_size = static_cast<uint>(fileSize);

    // Make sure the ROM buffer is large enough.
    if (p_cntnr.rom.size() < p_cntnr.rom_size)
        p_cntnr.rom.resize(p_cntnr.rom_size);

    file.read(
        reinterpret_cast<char*>(p_cntnr.rom.data()),
        fileSize
    );

    if (!file)
        return;

    p_cntnr.callbacks.cd_seeked =
        [](void* user_data, cc_u32f sector_index)
        {
            auto* object = static_cast<CPPPlum*>(user_data);

            CDReader_SeekToSector(
                &object->reader_state,
                sector_index
            );
        };

    p_cntnr.callbacks.cd_sector_read =
        [](void* user_data, cc_u16l* buffer)
        {
            auto* object = static_cast<CPPPlum*>(user_data);

            CDReader_ReadSector(
                &object->reader_state,
                buffer
            );
        };

    p_cntnr.callbacks.cd_track_seeked =
        [](void* user_data,
           cc_u16f track_index,
           ClownMDEmu_CDDAMode mode) -> cc_bool
        {
            auto* object = static_cast<CPPPlum*>(user_data);

            CDReader_PlaybackSetting playback_setting;

            switch (mode)
            {
                default:
                    SDL_assert(false);
                    return cc_false;

                case CLOWNMDEMU_CDDA_PLAY_ALL:
                    playback_setting =
                        CDReader_PlaybackSetting::CDREADER_PLAYBACK_ALL;
                    break;

                case CLOWNMDEMU_CDDA_PLAY_ONCE:
                    playback_setting =
                        CDReader_PlaybackSetting::CDREADER_PLAYBACK_ONCE;
                    break;

                case CLOWNMDEMU_CDDA_PLAY_REPEAT:
                    playback_setting =
                        CDReader_PlaybackSetting::CDREADER_PLAYBACK_REPEAT;
                    break;
            }

            return CDReader_PlayAudio(
                &object->reader_state,
                track_index,
                playback_setting
            );
        };

    p_cntnr.callbacks.cd_audio_read =
        [](void* user_data,
           cc_s16l* sample_buffer,
           size_t total_frames) -> size_t
        {
            auto* object = static_cast<CPPPlum*>(user_data);

            return CDReader_ReadAudio(
                &object->reader_state,
                sample_buffer,
                total_frames
            );
        };

    p_cntnr.reader_callbacks.read =
        [](void* buffer,
           size_t size,
           size_t count,
           void* stream) -> size_t
        {
            if (size == 0 || count == 0)
                return 0;

            return SDL_ReadIO(
                static_cast<SDL_IOStream*>(stream),
                buffer,
                size * count
            ) / size;
        };

    p_cntnr.reader_callbacks.open =
        [](const char* filename,
           ClownCD_FileMode mode) -> void*
        {
            const char* mode_string;

            switch (mode)
            {
                case CLOWNCD_RB:
                    mode_string = "rb";
                    break;

                case CLOWNCD_WB:
                    mode_string = "wb";
                    break;

                default:
                    return nullptr;
            }

            return SDL_IOFromFile(filename, mode_string);
        };

    p_cntnr.reader_callbacks.close =
        [](void* stream) -> int
        {
            return SDL_CloseIO(
                static_cast<SDL_IOStream*>(stream)
            );
        };

    p_cntnr.reader_callbacks.seek =
        [](void* stream,
           long position,
           ClownCD_FileOrigin origin) -> int
        {
            SDL_IOWhence whence;

            switch (origin)
            {
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
            }

            return SDL_SeekIO(
                static_cast<SDL_IOStream*>(stream),
                position,
                whence
            ) == -1 ? -1 : 0;
        };

    p_cntnr.reader_callbacks.tell =
        [](void* stream) -> long
        {
            const auto position =
                SDL_TellIO(
                    static_cast<SDL_IOStream*>(stream)
                );

            if (position < LONG_MIN || position > LONG_MAX)
                return -1L;

            return static_cast<long>(position);
        };

    p_cntnr.reader_callbacks.write =
        [](const void* buffer,
           size_t size,
           size_t count,
           void* stream) -> size_t
        {
            if (size == 0 || count == 0)
                return 0;

            return SDL_WriteIO(
                static_cast<SDL_IOStream*>(stream),
                buffer,
                size * count
            ) / size;
        };

    CDReader_Initialise(&p_cntnr.reader_state);

    SDL_IOStream* stream =
        SDL_IOFromFile(path.c_str(), "rb");

    if (stream == nullptr)
        return;

    CDReader_Open(
        &p_cntnr.reader_state,
        stream,
        path.c_str(),
        &p_cntnr.reader_callbacks
    );

    // Equivalent to:
    // static_cast<char>(p_cntnr.rom.at(0x200))
    const char region =
        static_cast<char>(p_cntnr.rom.at(0x200));

    // Equivalent to:
    // @[@"E", @"U"] containsObject:@(region)
    const bool overseas =
        region == 'E' || region == 'U';

    p_cntnr.configuration.general.region =
        overseas
            ? CLOWNMDEMU_REGION_OVERSEAS
            : CLOWNMDEMU_REGION_DOMESTIC;

    // Equivalent to:
    // @[@"J", @"U"] containsObject:@(region)
    const bool ntsc =
        region == 'J' || region == 'U';

    p_cntnr.configuration.general.tv_standard =
        ntsc
            ? CLOWNMDEMU_TV_STANDARD_NTSC
            : CLOWNMDEMU_TV_STANDARD_PAL;

    p_cntnr.output.SetPALMode(
        p_cntnr.configuration.general.tv_standard ==
        CLOWNMDEMU_TV_STANDARD_PAL
    );

    ClownMDEmu_Constant_Initialise(
        &p_cntnr.constant
    );

    ClownMDEmu_State_Initialise(
        &p_cntnr.emu_state
    );

    ClownMDEmu_Reset(
        &p_cntnr.emu,
        cc_true,
        p_cntnr.rom_size
    );

    // Original:
    //
    // std::string io(
    //     reinterpret_cast<const char*>(&p_cntnr.rom.at(0x1A0)),
    //     reinterpret_cast<const char*>(&p_cntnr.rom.at(0x1A0 + 16))
    // );
    //
    // This is equivalent, but cleaner:
    const std::string io(
        reinterpret_cast<const char*>(
            p_cntnr.rom.data() + 0x1A0
        ),
        16
    );

    // NSString stringByTrimmingCharactersInSet:
    std::string ios = io;

    const auto first =
        ios.find_first_not_of(" \t\n\r");

    if (first == std::string::npos)
    {
        ios.clear();
    }
    else
    {
        const auto last =
            ios.find_last_not_of(" \t\n\r");

        ios = ios.substr(
            first,
            last - first + 1
        );
    }

    // Equivalent to NSMutableArray.
    std::vector<std::string> characterArray;

    for (char character : ios)
    {
        characterArray.emplace_back(1, character);
    }

    // characterArray now contains each character as a std::string.
}


bool plum::is_paused(bool change, bool set_paused) {
    if (change)
        p_cntnr.paused.store(set_paused);

    if (change && !set_paused)
        p_cntnr.cv.notify_one();
    
    return p_cntnr.paused.load();
}

bool plum::is_running(bool change, bool set_running) {
    if (change)
        p_cntnr.running.store(set_running);
    return p_cntnr.running.load();
}


void plum::start(void) {
    p_cntnr.output.device.Prepare();
    p_cntnr.thread = std::jthread([&](std::stop_token token) {
        using namespace std::chrono;

        const int fps = p_cntnr.configuration.general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? 50 : 60;
        const auto frameDuration = duration<double>(1.0 / fps);

        while (!token.stop_requested()) {
            {
                std::unique_lock lock(p_cntnr.mutex);
                p_cntnr.cv.wait(lock, token, []() {
                    return !p_cntnr.paused.load();
                });
                
                if (token.stop_requested())
                    break;
            }
            
            auto frameStart = steady_clock::now();
            
            p_cntnr.output.MixerBegin();
            ClownMDEmu_Iterate(&p_cntnr.emu);
            p_cntnr.output.MixerEnd();
            
            plum::callback(plum::context, p_cntnr.framebuffer.data());
            
            auto frameEnd = steady_clock::now();
            auto elapsed = frameEnd - frameStart;
            if (elapsed < frameDuration)
                std::this_thread::sleep_for(frameDuration - elapsed);
        }
    });
}

void plum::stop(void) {
    p_cntnr.thread.request_stop();
    if (p_cntnr.thread.joinable())
        p_cntnr.thread.join();
    
    p_cntnr.paused.store(false);
    p_cntnr.running.store(false);
}


int plum::framebuffer_height(void) {
    return p_cntnr.height;
}

int plum::framebuffer_width(void) {
    return p_cntnr.width;
}


void plum::video_buffer_callback(VideoBufferCallback callback) {
    plum::callback = callback;
}


void plum::press_button(uint32_t button, int index) {
    p_cntnr.buttons[index][button] = true;
}

void plum::release_button(uint32_t button, int index) {
    p_cntnr.buttons[index][button] = false;
}


void plum::set_context(void* context) {
    plum::context = context;
}
