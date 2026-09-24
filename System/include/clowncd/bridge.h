//
//  bridge.h
//  Plum
//
//  Created by Jarrod Norwell on 27/8/2026.
//

#include <cstdint>
#include <string>

namespace plum {
void print_about(void);

void initialize_paths(void);
void initialize_system(void);

void destroy_system(void);

void insert_disc(std::string), insert_genesis(std::string), insert_megadrive(std::string);

bool is_paused(bool = false, bool = false);
bool is_running(bool = false, bool = false);

void start(void), stop(void);

int framebuffer_height(void), framebuffer_width(void);

using VideoBufferCallback = void(*)(void*, uint32_t*);
VideoBufferCallback callback;
void video_buffer_callback(VideoBufferCallback);

void press_button(uint32_t, int), release_button(uint32_t, int);

void* context;
void set_context(void*);
}
