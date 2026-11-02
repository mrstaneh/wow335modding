// wow335modding: the browser build has no FMOD Ex yet (its platform layer is Windows audio and x86 assembly), so it
// runs silent: FMOD_System_Create fails and SoundEngine takes its "disable" path, as on a PC without a sound device.
// Every other FMOD method exists only so the client links, and reports FMOD_ERR_UNSUPPORTED.
// Generated from the linker's list of undefined FMOD symbols. ponytail: port FMOD's platform layer with a Web Audio
// output to get sound.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "fmod.hpp"

FMOD_RESULT F_API FMOD_Memory_Initialize(void*, int, FMOD_MEMORY_ALLOCCALLBACK, FMOD_MEMORY_REALLOCCALLBACK, FMOD_MEMORY_FREECALLBACK) {
    return FMOD_OK;
}

FMOD_RESULT F_API FMOD_Debug_SetLevel(FMOD_DEBUGLEVEL) {
    return FMOD_OK;
}

FMOD_RESULT F_API FMOD_System_Create(FMOD_SYSTEM** system) {
    *system = nullptr;
    return FMOD_ERR_OUTPUT_INIT;
}

FMOD_RESULT F_API FMOD::Channel::addDSP(FMOD::DSP*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::get3DAttributes(FMOD_VECTOR*, FMOD_VECTOR*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::get3DMinMaxDistance(float*, float*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::get3DPanLevel(float*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getAudibility(float*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getFadeVolume(float*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getFrequency(float*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getMode(unsigned int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getPosition(unsigned int*, unsigned int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getPriority(int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getReverbProperties(FMOD_REVERB_CHANNELPROPERTIES*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getUserData(void**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::getVolume(float*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::isPlaying(bool*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::isVirtual(bool*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::set3DAttributes(FMOD_VECTOR const*, FMOD_VECTOR const*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::set3DConeOrientation(FMOD_VECTOR*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::set3DConeSettings(float, float, float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::set3DMinMaxDistance(float, float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::set3DPanLevel(float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setCallback(FMOD_CHANNEL_CALLBACKTYPE, FMOD_RESULT (*)(FMOD_CHANNEL*, FMOD_CHANNEL_CALLBACKTYPE, int, unsigned int, unsigned int), int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setChannelGroup(FMOD::ChannelGroup*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setFrequency(float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setPaused(bool) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setPosition(unsigned int, unsigned int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setPriority(int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setReverbProperties(FMOD_REVERB_CHANNELPROPERTIES const*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setSpeakerMix(float, float, float, float, float, float, float, float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setUserData(void*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::setVolume(float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Channel::stop() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::ChannelGroup::getDSPHead(FMOD::DSP**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::ChannelGroup::getNumChannels(int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::ChannelGroup::release() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::ChannelGroup::setVolume(float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::addInput(FMOD::DSP*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::disconnectAll(bool, bool) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::getOutput(int, FMOD::DSP**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::getUserData(void**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::release() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::remove() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::setActive(bool) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::setBypass(bool) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::DSP::setParameter(int, float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::getLength(unsigned int*, unsigned int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::getName(char*, int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::getUserData(void**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::lock(unsigned int, unsigned int, void**, void**, unsigned int*, unsigned int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::readyForRelease(bool*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::release() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::setSoundGroup(FMOD::SoundGroup*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::Sound::unlock(void*, void*, unsigned int, unsigned int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::SoundGroup::release() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::SoundGroup::setMaxAudible(int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::SoundGroup::setMaxAudibleBehavior(FMOD_SOUNDGROUP_BEHAVIOR) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::SoundGroup::setMuteFadeSpeed(float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::close() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::createChannelGroup(char const*, FMOD::ChannelGroup**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::createDSP(FMOD_DSP_DESCRIPTION*, FMOD::DSP**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::createDSPByType(FMOD_DSP_TYPE, FMOD::DSP**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::createSound(char const*, unsigned int, FMOD_CREATESOUNDEXINFO*, FMOD::Sound**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::createSoundGroup(char const*, FMOD::SoundGroup**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::createStream(char const*, unsigned int, FMOD_CREATESOUNDEXINFO*, FMOD::Sound**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::get3DListenerAttributes(int, FMOD_VECTOR*, FMOD_VECTOR*, FMOD_VECTOR*, FMOD_VECTOR*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getDriverCaps(int, unsigned int*, int*, int*, FMOD_SPEAKERMODE*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getDriverName(int, char*, int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getHardwareChannels(int*, int*, int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getNumDrivers(int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getOutput(FMOD_OUTPUTTYPE*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getRecordDriverName(int, char*, int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getRecordNumDrivers(int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getRecordPosition(unsigned int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::getVersion(unsigned int*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::init(int, unsigned int, void*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::isRecording(bool*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::playDSP(FMOD_CHANNELINDEX, FMOD::DSP*, bool, FMOD::Channel**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::playSound(FMOD_CHANNELINDEX, FMOD::Sound*, bool, FMOD::Channel**) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::recordStart(FMOD::Sound*, bool) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::recordStop() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::release() { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::set3DListenerAttributes(int, FMOD_VECTOR const*, FMOD_VECTOR const*, FMOD_VECTOR const*, FMOD_VECTOR const*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::set3DRolloffCallback(float (*)(FMOD_CHANNEL*, float)) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::set3DSettings(float, float, float) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setCallback(FMOD_SYSTEM_CALLBACKTYPE, FMOD_RESULT (*)(FMOD_SYSTEM*, FMOD_SYSTEM_CALLBACKTYPE, void*, void*)) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setDriver(int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setDSPBufferSize(unsigned int, int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setHardwareChannels(int, int, int, int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setOutput(FMOD_OUTPUTTYPE) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setRecordDriver(int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setReverbProperties(FMOD_REVERB_PROPERTIES const*) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setSoftwareChannels(int) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setSoftwareFormat(int, FMOD_SOUND_FORMAT, int, int, FMOD_DSP_RESAMPLER) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::setSpeakerMode(FMOD_SPEAKERMODE) { return FMOD_ERR_UNSUPPORTED; }
FMOD_RESULT F_API FMOD::System::update() { return FMOD_ERR_UNSUPPORTED; }

#endif
