// wow335modding: see fmod_output_webaudio.h; modelled on src/fmod_output_nosound.cpp.
#include "fmod_settings.h"

#ifdef FMOD_SUPPORT_WEBAUDIO

#include "fmod_memory.h"
#include "fmod_output_webaudio.h"
#include "fmod_soundi.h"
#include "fmod_string.h"
#include "fmod_systemi.h"

#include <stdint.h>

extern "C" void wowAudioStart(float *ring, unsigned int frames, int channels, int rate, volatile int32_t *readPos);

namespace FMOD
{

FMOD_OUTPUT_DESCRIPTION_EX webaudiooutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputWebAudio::getDescriptionEx()
{
    FMOD_memset(&webaudiooutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    webaudiooutput.name          = "FMOD Web Audio Output";
    webaudiooutput.version       = 0x00010100;
    webaudiooutput.polling       = true;
    webaudiooutput.getnumdrivers = &OutputWebAudio::getNumDriversCallback;
    webaudiooutput.getdrivername = &OutputWebAudio::getDriverNameCallback;
    webaudiooutput.getdrivercaps = &OutputWebAudio::getDriverCapsCallback;
    webaudiooutput.init          = &OutputWebAudio::initCallback;
    webaudiooutput.close         = &OutputWebAudio::closeCallback;
    webaudiooutput.getposition   = &OutputWebAudio::getPositionCallback;
    webaudiooutput.lock          = &OutputWebAudio::lockCallback;

    webaudiooutput.mType         = FMOD_OUTPUTTYPE_WEBAUDIO;
    webaudiooutput.mSize         = sizeof(OutputWebAudio);

    return &webaudiooutput;
}

FMOD_RESULT OutputWebAudio::getNumDrivers(int *numdrivers)
{
    *numdrivers = 1;
    return FMOD_OK;
}

FMOD_RESULT OutputWebAudio::getDriverName(int driver, char *name, int namelen)
{
    FMOD_strncpy(name, "Browser (Web Audio)", namelen);
    return FMOD_OK;
}

FMOD_RESULT OutputWebAudio::getDriverCaps(int id, FMOD_CAPS *caps)
{
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL | FMOD_CAPS_OUTPUT_FORMAT_PCMFLOAT);
    return FMOD_OK;
}

FMOD_RESULT OutputWebAudio::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    Plugin::init();

    /* Float frames are what Web Audio plays; the ring holds every DSP buffer FMOD mixes ahead. */
    *outputformat = FMOD_SOUND_FORMAT_PCMFLOAT;
    mRingFrames = dspbufferlength * dspnumbuffers;
    SoundI::getBytesFromSamples(mRingFrames, &mRingBytes, outputchannels, *outputformat);

    mRing = (float *)FMOD_Memory_Calloc(mRingBytes);
    if (!mRing)
    {
        return FMOD_ERR_MEMORY;
    }

    mReadPos = 0;
    wowAudioStart(mRing, mRingFrames, outputchannels, *outputrate, &mReadPos);
    return FMOD_OK;
}

FMOD_RESULT OutputWebAudio::close()
{
    /* ponytail: the worklet keeps reading the ring until the page closes, so it is not freed; the output is created once. */
    return FMOD_OK;
}

FMOD_RESULT OutputWebAudio::getPosition(unsigned int *pcm)
{
    *pcm = (unsigned int)__atomic_load_n(&mReadPos, __ATOMIC_ACQUIRE);
    return FMOD_OK;
}

FMOD_RESULT OutputWebAudio::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    offset %= mRingBytes;

    *ptr1 = (char *)mRing + offset;
    if (offset + length > mRingBytes)
    {
        *ptr2 = (char *)mRing;
        *len1 = mRingBytes - offset;
        *len2 = length - *len1;
    }
    else
    {
        *ptr2 = NULL;
        *len1 = length;
        *len2 = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK OutputWebAudio::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputWebAudio *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputWebAudio::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputWebAudio *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputWebAudio::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputWebAudio *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputWebAudio::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputWebAudio *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputWebAudio::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputWebAudio *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputWebAudio::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputWebAudio *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputWebAudio::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputWebAudio *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}

#endif /* FMOD_SUPPORT_WEBAUDIO */
