#include "Windows.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <objbase.h>
#include "assert.h"
#include "sound.h"
#include <inttypes.h>

__CRT_UUID_DECL(IAudioMeterInformation, 0xC02216F6, 0x8C67, 0x4B5B, 0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64);

MIDL_INTERFACE("C02216F6-8C67-4B5B-9D00-D008E73E0064")
IAudioMeterInformation : public IUnknown
{
public:
    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetPeakValue(
        /* [out] */ float* pfPeak) = 0;

    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(
        /* [out] */
          UINT* pnChannelCount) = 0;

    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(
        /* [in] */ UINT32 u32ChannelCount,
        /* [size_is][out] */ float* afPeakValues) = 0;

    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE QueryHardwareSupport(
        /* [out] */
          DWORD* pdwHardwareSupportMask) = 0;
};

#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

static IAudioMeterInformation* _sound_info;
static IMMDeviceEnumerator* _sound_enum;
static IMMDevice* _sound_device;

extern "C" void sound_init() {
    HRESULT err = 0;

    CoInitialize(NULL);

    // Get enumerator for audio endpoint devices.
    err = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                          NULL, CLSCTX_INPROC_SERVER,
                          __uuidof(IMMDeviceEnumerator),
                          (void**)&_sound_enum);
    uint32_t e = (uint32_t)err;
    assert(SUCCEEDED(e));

    // Get peak meter for default audio-rendering device.
    err = _sound_enum->GetDefaultAudioEndpoint(eRender, eConsole, &_sound_device);
    assert(SUCCEEDED(err));

    err = _sound_device->Activate(__uuidof(IAudioMeterInformation),
                           CLSCTX_ALL, NULL, (void**)&_sound_info);
    assert(SUCCEEDED(err));
}

extern "C" void sound_deinit() {
    SAFE_RELEASE(_sound_enum);
    SAFE_RELEASE(_sound_device);
    SAFE_RELEASE(_sound_info);
    CoUninitialize();
}

extern "C" float sound_get() {
    float peak = 0;
    HRESULT err = _sound_info->GetPeakValue(&peak);
    assert(SUCCEEDED(err));
    return peak;
}