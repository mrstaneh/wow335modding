// wow335modding: FMOD's output list for the browser build (win32/src/fmod_os_output.cpp on Windows).
#include "fmod_settings.h"

#include "fmod.h"
#include "fmod_output.h"
#include "fmod_output_webaudio.h"
#include "fmod_pluginfactory.h"

FMOD_RESULT FMOD_OS_Output_Register(FMOD::PluginFactory *pluginfactory)
{
    pluginfactory->registerOutput(FMOD::OutputWebAudio::getDescriptionEx());
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Output_GetDefault(FMOD_OUTPUTTYPE *outputtype)
{
    if (!outputtype)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *outputtype = FMOD_OUTPUTTYPE_WEBAUDIO;
    return FMOD_OK;
}
