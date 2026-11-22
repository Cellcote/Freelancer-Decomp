// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the DirectX 8 SDK's <dsound.h> on macOS and Linux
// (src/include/posix, non-Windows only). Only the names the ported modules see through the sound
// headers (audio/dsound8.h: soundmanager.h, soundstreamer.h) are here: the IDirectSound and
// IDirectSoundBuffer interfaces with the SDK's vtable order, declared only. There is no
// DirectSound here; sound comes with SDL audio (docs/x64-client-plan.md, M4), and with it the
// rest of this header.

#pragma once

#if defined(_WIN32)
#error "posix/dsound.h is the stand-in for macOS and Linux; Windows uses the DirectX 8 SDK"
#endif

#include <windows.h>

#define DS_OK S_OK

// The structures are not declared yet (M4): the interfaces take them by pointer.
struct _DSBUFFERDESC;
struct _DSCAPS;
struct _DSBCAPS;
struct tWAVEFORMATEX;
typedef const _DSBUFFERDESC* LPCDSBUFFERDESC;
typedef _DSCAPS* LPDSCAPS;
typedef _DSBCAPS* LPDSBCAPS;
typedef tWAVEFORMATEX* LPWAVEFORMATEX;
typedef const tWAVEFORMATEX* LPCWAVEFORMATEX;

struct IDirectSound;
struct IDirectSoundBuffer;
typedef IDirectSound* LPDIRECTSOUND;
typedef IDirectSoundBuffer* LPDIRECTSOUNDBUFFER;

struct IDirectSound : public IUnknown
{
    STDMETHOD(CreateSoundBuffer)(LPCDSBUFFERDESC Desc, LPDIRECTSOUNDBUFFER* Buffer,
                                 LPUNKNOWN Outer) PURE;
    STDMETHOD(GetCaps)(LPDSCAPS Caps) PURE;
    STDMETHOD(DuplicateSoundBuffer)(LPDIRECTSOUNDBUFFER Original,
                                    LPDIRECTSOUNDBUFFER* Duplicate) PURE;
    STDMETHOD(SetCooperativeLevel)(HWND Window, DWORD Level) PURE;
    STDMETHOD(Compact)() PURE;
    STDMETHOD(GetSpeakerConfig)(LPDWORD SpeakerConfig) PURE;
    STDMETHOD(SetSpeakerConfig)(DWORD SpeakerConfig) PURE;
    STDMETHOD(Initialize)(LPCGUID Device) PURE;
};

struct IDirectSound8 : public IDirectSound
{
    STDMETHOD(VerifyCertification)(LPDWORD Certified) PURE;
};
typedef IDirectSound8* LPDIRECTSOUND8;

struct IDirectSoundBuffer : public IUnknown
{
    STDMETHOD(GetCaps)(LPDSBCAPS Caps) PURE;
    STDMETHOD(GetCurrentPosition)(LPDWORD PlayCursor, LPDWORD WriteCursor) PURE;
    STDMETHOD(GetFormat)(LPWAVEFORMATEX Format, DWORD SizeAllocated, LPDWORD SizeWritten) PURE;
    STDMETHOD(GetVolume)(LPLONG Volume) PURE;
    STDMETHOD(GetPan)(LPLONG Pan) PURE;
    STDMETHOD(GetFrequency)(LPDWORD Frequency) PURE;
    STDMETHOD(GetStatus)(LPDWORD Status) PURE;
    STDMETHOD(Initialize)(LPDIRECTSOUND DirectSound, LPCDSBUFFERDESC Desc) PURE;
    STDMETHOD(Lock)(DWORD Offset, DWORD Bytes, LPVOID* AudioPtr1, LPDWORD AudioBytes1,
                    LPVOID* AudioPtr2, LPDWORD AudioBytes2, DWORD Flags) PURE;
    STDMETHOD(Play)(DWORD Reserved, DWORD Priority, DWORD Flags) PURE;
    STDMETHOD(SetCurrentPosition)(DWORD NewPosition) PURE;
    STDMETHOD(SetFormat)(LPCWAVEFORMATEX Format) PURE;
    STDMETHOD(SetVolume)(LONG Volume) PURE;
    STDMETHOD(SetPan)(LONG Pan) PURE;
    STDMETHOD(SetFrequency)(DWORD Frequency) PURE;
    STDMETHOD(Stop)() PURE;
    STDMETHOD(Unlock)(LPVOID AudioPtr1, DWORD AudioBytes1, LPVOID AudioPtr2,
                      DWORD AudioBytes2) PURE;
    STDMETHOD(Restore)() PURE;
};
