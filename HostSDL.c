//
//  HostSDL.c
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 02/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
//  2026 revision: SDL2 desktop backend. All SDL code now lives in this file.
//  - keyboard mapped by SDL scancode (physical key) to Amiga raw codes; the
//    old table was indexed by truncated SDL keycodes, which collided
//    (F2 = ';', F4 = '=', F3 = '<'...) and could be read out of bounds
//  - the emulated framebuffer is uploaded with SDL_UpdateTexture instead of
//    writing into a locked texture with an assumed pitch
//  - mouse buttons are tested with bit masks (left+right together works)

#include <stdio.h>
#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  else
#    include <SDL.h>
#  endif
#else
#  include <SDL2/SDL.h>
#endif

#include "Host.h"
#include "HostHarness.h"
#include "Floppy.h"
#include "CIA.h"
#include "image.h"
#include "Memory.h"
#include "Paula.h"
#include <stdlib.h>
#include <string.h>

static SDL_Window*   window;
static SDL_Renderer* renderer;
static SDL_Texture*  playfield;
static SDL_Texture*  commodoreLogo;
static SDL_Texture*  spinner;
static int LEDActive = 1;
static int running = 1;

// SDL scancode -> Amiga raw key code (0xFF = not mapped)
static uint8_t scanToAmiga[SDL_NUM_SCANCODES];

static void buildKeymap(void){
    for(int i=0;i<SDL_NUM_SCANCODES;++i) scanToAmiga[i] = 0xFF;

    static const struct { SDL_Scancode sc; uint8_t amiga; } map[] = {
        {SDL_SCANCODE_GRAVE,0x00},
        {SDL_SCANCODE_1,0x01},{SDL_SCANCODE_2,0x02},{SDL_SCANCODE_3,0x03},{SDL_SCANCODE_4,0x04},
        {SDL_SCANCODE_5,0x05},{SDL_SCANCODE_6,0x06},{SDL_SCANCODE_7,0x07},{SDL_SCANCODE_8,0x08},
        {SDL_SCANCODE_9,0x09},{SDL_SCANCODE_0,0x0A},{SDL_SCANCODE_MINUS,0x0B},{SDL_SCANCODE_EQUALS,0x0C},
        {SDL_SCANCODE_BACKSLASH,0x0D},{SDL_SCANCODE_KP_0,0x0F},
        {SDL_SCANCODE_Q,0x10},{SDL_SCANCODE_W,0x11},{SDL_SCANCODE_E,0x12},{SDL_SCANCODE_R,0x13},
        {SDL_SCANCODE_T,0x14},{SDL_SCANCODE_Y,0x15},{SDL_SCANCODE_U,0x16},{SDL_SCANCODE_I,0x17},
        {SDL_SCANCODE_O,0x18},{SDL_SCANCODE_P,0x19},{SDL_SCANCODE_LEFTBRACKET,0x1A},{SDL_SCANCODE_RIGHTBRACKET,0x1B},
        {SDL_SCANCODE_KP_1,0x1D},{SDL_SCANCODE_KP_2,0x1E},{SDL_SCANCODE_KP_3,0x1F},
        {SDL_SCANCODE_A,0x20},{SDL_SCANCODE_S,0x21},{SDL_SCANCODE_D,0x22},{SDL_SCANCODE_F,0x23},
        {SDL_SCANCODE_G,0x24},{SDL_SCANCODE_H,0x25},{SDL_SCANCODE_J,0x26},{SDL_SCANCODE_K,0x27},
        {SDL_SCANCODE_L,0x28},{SDL_SCANCODE_SEMICOLON,0x29},{SDL_SCANCODE_APOSTROPHE,0x2A},
        {SDL_SCANCODE_NONUSHASH,0x2B},
        {SDL_SCANCODE_KP_4,0x2D},{SDL_SCANCODE_KP_5,0x2E},{SDL_SCANCODE_KP_6,0x2F},
        {SDL_SCANCODE_Z,0x31},{SDL_SCANCODE_X,0x32},{SDL_SCANCODE_C,0x33},{SDL_SCANCODE_V,0x34},
        {SDL_SCANCODE_B,0x35},{SDL_SCANCODE_N,0x36},{SDL_SCANCODE_M,0x37},{SDL_SCANCODE_COMMA,0x38},
        {SDL_SCANCODE_PERIOD,0x39},{SDL_SCANCODE_SLASH,0x3A},
        {SDL_SCANCODE_KP_PERIOD,0x3C},{SDL_SCANCODE_KP_7,0x3D},{SDL_SCANCODE_KP_8,0x3E},{SDL_SCANCODE_KP_9,0x3F},
        {SDL_SCANCODE_SPACE,0x40},{SDL_SCANCODE_BACKSPACE,0x41},{SDL_SCANCODE_TAB,0x42},
        {SDL_SCANCODE_KP_ENTER,0x43},{SDL_SCANCODE_RETURN,0x44},{SDL_SCANCODE_ESCAPE,0x45},
        {SDL_SCANCODE_DELETE,0x46},{SDL_SCANCODE_KP_MINUS,0x4A},
        {SDL_SCANCODE_UP,0x4C},{SDL_SCANCODE_DOWN,0x4D},{SDL_SCANCODE_RIGHT,0x4E},{SDL_SCANCODE_LEFT,0x4F},
        {SDL_SCANCODE_F6,0x55},{SDL_SCANCODE_F7,0x56},{SDL_SCANCODE_F8,0x57},{SDL_SCANCODE_F9,0x58},
        {SDL_SCANCODE_F10,0x59},
        {SDL_SCANCODE_KP_DIVIDE,0x5C},{SDL_SCANCODE_KP_MULTIPLY,0x5D},{SDL_SCANCODE_KP_PLUS,0x5E},
        {SDL_SCANCODE_F11,0x5F},        // Help
        {SDL_SCANCODE_INSERT,0x5F},     // Help
        {SDL_SCANCODE_LSHIFT,0x60},{SDL_SCANCODE_RSHIFT,0x61},{SDL_SCANCODE_CAPSLOCK,0x62},
        {SDL_SCANCODE_LCTRL,0x63},{SDL_SCANCODE_RCTRL,0x63},
        {SDL_SCANCODE_LALT,0x64},{SDL_SCANCODE_RALT,0x65},
        {SDL_SCANCODE_LGUI,0x66},{SDL_SCANCODE_RGUI,0x67},
        {SDL_SCANCODE_PAGEUP,0x66},{SDL_SCANCODE_PAGEDOWN,0x67},  // Amiga keys for PCs without GUI keys
    };
    for(size_t i=0;i<sizeof(map)/sizeof(map[0]);++i){
        scanToAmiga[map[i].sc] = map[i].amiga;
    }
}

// F1-F4 insert/eject DF0-DF3, F5 toggles the on-screen LEDs (as in the
// original), the key right of left shift quits. Amiga F1-F5 are therefore
// not available; F6-F10 are passed through.
static int handleHotkey(SDL_Scancode sc){
    switch(sc){
        case SDL_SCANCODE_F1: floppyInsert(0); return 1;
        case SDL_SCANCODE_F2: floppyInsert(1); return 1;
        case SDL_SCANCODE_F3: floppyInsert(2); return 1;
        case SDL_SCANCODE_F4: floppyInsert(3); return 1;
        case SDL_SCANCODE_F5: toggleLEDs();    return 1;
        case SDL_SCANCODE_NONUSBACKSLASH: running = 0; return 1;
        default: return 0;
    }
}

static SDL_Texture* makeTexture(const unsigned char* bgra){
    SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_STATIC, 50, 50);
    if(t){
        SDL_UpdateTexture(t, NULL, bgra, 50*4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

static SDL_AudioDeviceID audioDevice = 0;

// Joysticks: the first game controller -> Amiga port 2, the second -> port 1
// (only with OMEGA_PORT1=pad, otherwise port 1 keeps the mouse).
// OMEGA_KBJOY=1: arrows + right Ctrl (fire) + right Alt (2nd button) are a
// joystick in port 2 instead of Amiga keys.
static SDL_GameController* pads[2] = {NULL, NULL};
static int kbJoy = 0;
static unsigned kbJoyBits = 0;

static unsigned kbJoyBit(SDL_Scancode sc){
    switch(sc){
        case SDL_SCANCODE_UP:    return JOY_UP;
        case SDL_SCANCODE_DOWN:  return JOY_DOWN;
        case SDL_SCANCODE_LEFT:  return JOY_LEFT;
        case SDL_SCANCODE_RIGHT: return JOY_RIGHT;
        case SDL_SCANCODE_RCTRL: return JOY_FIRE;
        case SDL_SCANCODE_RALT:  return JOY_FIRE2;
        default: return 0;
    }
}

static unsigned padBits(SDL_GameController* c){
    if(!c) return 0;
    unsigned b = 0;
    const int dead = 16000;
    if(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP)    || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) < -dead) b |= JOY_UP;
    if(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) >  dead) b |= JOY_DOWN;
    if(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) < -dead) b |= JOY_LEFT;
    if(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) >  dead) b |= JOY_RIGHT;
    if(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X)) b |= JOY_FIRE;
    if(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B) || SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y)) b |= JOY_FIRE2;
    return b;
}

static void openPads(void){
    for(int i=0;i<2;i++){
        if(pads[i] && !SDL_GameControllerGetAttached(pads[i])){
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
    }
    for(int j=0; j<SDL_NumJoysticks(); j++){
        if(!SDL_IsGameController(j)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
        int used = 0;
        for(int i=0;i<2;i++){
            if(pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) used = 1;
        }
        for(int i=0;i<2 && !used;i++){
            if(!pads[i]){ pads[i] = SDL_GameControllerOpen(j); used = 1; }
        }
    }
}

static void updateJoysticks(void){
    int port1 = pads[1] && getenv("OMEGA_PORT1") && strcmp(getenv("OMEGA_PORT1"), "pad") == 0;
    inputJoystick(1, kbJoyBits | padBits(pads[0]) | (port1 ? 0 : padBits(pads[1])));
    inputPort1Joystick(port1);
    inputJoystick(0, port1 ? padBits(pads[1]) : 0);
}

int hostInit(void){
    // memory in KB: OMEGA_CHIP (256-2048, default 512) and OMEGA_SLOW (0-1792, default 512)
    if(getenv("OMEGA_CHIP") || getenv("OMEGA_SLOW")){
        memoryConfigure((getenv("OMEGA_CHIP") ? atoi(getenv("OMEGA_CHIP")) : 512) * 1024,
                        (getenv("OMEGA_SLOW") ? atoi(getenv("OMEGA_SLOW")) : 512) * 1024);
    }

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0 &&
       SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0 &&
       SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0){
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    window = SDL_CreateWindow("Omega v0.54 - Amiga 500 PAL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              FB_WIDTH, FB_VISIBLE_HEIGHT*2, SDL_WINDOW_RESIZABLE);
    if(!window){
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 0;
    }
    // no vsync: a 60 Hz monitor would make the Amiga run 20% fast. Frames are
    // paced to the PAL rate in hostDisplay() instead.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer){
        renderer = SDL_CreateRenderer(window, -1, 0);   // fall back to any renderer
    }
    if(!renderer){
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);

    playfield = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  FB_WIDTH, FB_VISIBLE_HEIGHT);
    commodoreLogo = makeTexture(clogo);
    spinner = makeTexture(floppyImage);

    buildKeymap();
    kbJoy = getenv("OMEGA_KBJOY") && atoi(getenv("OMEGA_KBJOY"));
    openPads();

    // Audio: 48 kHz stereo, fed once per frame (OMEGA_NOAUDIO=1 to disable)
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = PAULA_DEFAULT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL;
    if(!getenv("OMEGA_NOAUDIO") && SDL_WasInit(SDL_INIT_AUDIO)){
        audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    }
    if(audioDevice){
        SDL_PauseAudioDevice(audioDevice, 0);
    }else{
        printf("No audio output\n");
    }
    paulaSetEnabled(audioDevice != 0);
    return 1;
}

static void feedAudio(void){
    static int16_t buf[4096*2];
    if(!audioDevice){
        return;
    }
    int n = paulaRead(buf, 4096);
    int queued = (int)(SDL_GetQueuedAudioSize(audioDevice) / 4);
    if(queued > PAULA_DEFAULT_RATE / 4){
        SDL_ClearQueuedAudio(audioDevice);      // e.g. after a pause: drop the backlog
        queued = 0;
    }
    if(n > 0){
        SDL_QueueAudio(audioDevice, buf, (Uint32)n * 4);
    }
    paulaFollowQueue(queued, PAULA_DEFAULT_RATE * 60 / 1000);   // ~60 ms latency
}

void hostShutdown(void){
    SDL_Quit();
}

void toggleLEDs(void){
    LEDActive = 1 - LEDActive;
    // Capture the mouse when the overlay is hidden
    SDL_SetRelativeMouseMode(LEDActive ? SDL_FALSE : SDL_TRUE);
}

int hostPollEvents(void){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        switch(event.type){
            case SDL_QUIT:
                running = 0;
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if(event.key.repeat){
                    break;      // the Amiga does its own key repeat
                }
                SDL_Scancode sc = event.key.keysym.scancode;
                if(kbJoy && kbJoyBit(sc)){
                    if(event.type == SDL_KEYDOWN) kbJoyBits |= kbJoyBit(sc);
                    else kbJoyBits &= ~kbJoyBit(sc);
                    break;
                }
                if(event.type == SDL_KEYDOWN && handleHotkey(sc)){
                    break;
                }
                if(sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F5){
                    break;      // hotkey release
                }
                uint8_t code = scanToAmiga[sc];
                if(code == 0xFF){
                    break;
                }
                if(event.type == SDL_KEYDOWN){
                    inputKeyDown(code);
                }else{
                    inputKeyUp(code);
                }
                break;
            }

            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                openPads();
                break;

            default:
                break;
        }
    }
    updateJoysticks();
    return running;
}

static void drawDrive(int n, SDL_Rect spin, int winY){
    if((df[n].pra & 0x4) == 0){
        return;
    }
    double angle = ((double)df[n].index/6400.0)*360.0;
    SDL_RenderCopyEx(renderer, spinner, NULL, &spin, angle, NULL, SDL_FLIP_NONE);
    SDL_Rect head = {spin.x + 24, (winY-59)+(df[n].cylinder/6), 3, 3};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &head);
}

void hostDisplay(void){

    if(harnessFrame()){
        running = 0;
        exit(0);
    }

    // Mouse
    int x, y;
    uint32_t buttons = SDL_GetRelativeMouseState(&x, &y);
    inputMouseButtons((buttons & SDL_BUTTON_LMASK) != 0, (buttons & SDL_BUTTON_RMASK) != 0);
    inputMouseMove(x*2, y*2);

    int winX, winY;
    SDL_GetWindowSize(window, &winX, &winY);

    // Amiga display
    SDL_UpdateTexture(playfield, NULL, host.pixels, FB_WIDTH * 4);
    SDL_RenderCopy(renderer, playfield, NULL, NULL);

    if(LEDActive == 1){
        // Power light
        switch(CIAA.pra & 3){
            case 0:  SDL_SetRenderDrawColor(renderer, 255, 255, 31, 255); break;
            case 2:  SDL_SetRenderDrawColor(renderer, 182, 182, 31, 255); break;
            default: SDL_SetRenderDrawColor(renderer, 31, 31, 31, 255);   break;
        }
        SDL_Rect pLED = {winX-55, winY-40, 50, 7};
        SDL_RenderFillRect(renderer, &pLED);

        // Floppy light
        if((CIAB.prb & 128) == 0){
            SDL_SetRenderDrawColor(renderer, 255, 255, 31, 255);
        }else{
            SDL_SetRenderDrawColor(renderer, 31, 31, 31, 255);
        }
        SDL_Rect dLED = {winX-55, winY-20, 50, 7};
        SDL_RenderFillRect(renderer, &dLED);

        SDL_Rect clo = {winX-55, winY-100, 50, 50};
        SDL_RenderCopy(renderer, commodoreLogo, NULL, &clo);

        for(int n=0;n<4;++n){
            SDL_Rect spin = {5 + 50*n, winY-60, 50, 50};
            drawDrive(n, spin, winY);
        }
    }

    SDL_RenderPresent(renderer);
    feedAudio();

    // Amiga 500 PAL: one frame every 313 x 227 colour clocks = 20.032 ms
    static Uint64 next = 0;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 frame = freq * 20032 / 1000000;
    Uint64 now = SDL_GetPerformanceCounter();
    if(next == 0 || now > next + 4*frame){
        next = now;                         // first frame, or far behind: don't catch up
    }
    next += frame;
    while((now = SDL_GetPerformanceCounter()) < next){
        if(next - now > freq / 500){
            SDL_Delay(1);                   // sleep while more than 2 ms remain
        }
    }
}

long hostLoadFile(const char* path, uint8_t* buffer, long maxSize){
    SDL_RWops* f = SDL_RWFromFile(path, "rb");
    if(f == NULL){
        return -1;
    }
    long n = (long)SDL_RWread(f, buffer, 1, (size_t)maxSize);
    SDL_RWclose(f);
    return n;
}
