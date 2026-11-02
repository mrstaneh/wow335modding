// wow335modding: OggDecompress uses FMOD's copy of libvorbisfile, which allocates through FMOD's memory manager; the
// browser build has no FMOD yet (see SoundEngine/Web/FmodSilent.cpp), so Ogg streams fail to open, like the sound.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <vorbis/vorbisfile.h>

int ov_open_callbacks(void*, OggVorbis_File*, char*, ogg_int32_t, ov_callbacks) {
    return OV_EREAD;
}

int ov_clear(OggVorbis_File*) {
    return 0;
}

vorbis_info* ov_info(OggVorbis_File*, int) {
    return nullptr;
}

ogg_int32_t ov_read(OggVorbis_File*, char*, int, int, int, int, int*) {
    return OV_EREAD;
}

#endif
