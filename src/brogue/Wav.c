#include "Wav.h"

#include <OpenAL/al.h>
#include <OpenAL/alc.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define WAVH_CHANNELS_MONO 1
#define WAVH_CHANNELS_STEREO 2

const int WAVH_RIFF = 0x46464952;    // "RIFF"
const int WAVH_WAVE = 0x45564157;    // "WAVE"
const int WAVH_FMT = 0x20746D66;     // "fmt"
const int WAVH_FACT = 0x74636166;    // "fact"
const int WAVH_LIST = 0x5453494c;    // "LIST"
const int WAVH_OV_DATA = 0x61746164; // "data"
const int WAVH_WFORMATLENGTH = 16;
const short WAVH_WFORMATTAG_PCM = 1;

extern char dataDirectory[];

typedef struct wav_cache_rec {
    char *filename;
    char *data;
    struct wav_cache_rec *next;
} wav_cache;

static wav_cache *next_wav_cache = NULL;

static wav_cache* wavMatches(const char *filename) {
    wav_cache *cache = next_wav_cache;
    while (cache) {
        if (strcmp(filename, cache->filename) == 0) {
            return cache;
        }
        cache = cache->next;
    }
    return (wav_cache *)NULL;
}

static wav_cache* loadWav(char *filename) {
    char path[256];
    snprintf(path, 255, "%s/assets/sounds/%s", dataDirectory, filename);

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    struct stat st;
    stat(path, &st);

    char *data = malloc(st.st_size);
    int rsize = fread(data, 1, st.st_size, fp);
    fclose(fp);
    if (rsize != st.st_size) {
        free(data);
        return NULL;
    }

    wav_cache *new_cache = (wav_cache*)malloc(sizeof(wav_cache));
    new_cache->data = data;
    new_cache->filename = malloc(strlen(filename) + 1);
    strcpy(new_cache->filename, filename);
    new_cache->next = next_wav_cache;

    next_wav_cache = new_cache;
    return new_cache;
}

int convInt(unsigned char *header, int start) {
    int ret = (header[start + 3] << 24)
              | (header[start + 2] << 16)
              | (header[start + 1] << 8)
              | (header[start + 0]);
    return ret;
}

short convShort(unsigned char *header, int start) {
    int ret = (header[start + 1] << 8) | (header[start + 0]);
    return ret;
}

int parseWavHeader(char *data, int *channels, int *bits, int *size, int *samplingrate) {
    unsigned char header[256];

    // copy header
    memcpy(header, data, 256);

    int next = 0;
    int riff = convInt(header, next);
    if (riff != WAVH_RIFF) {
        printf("NOT match riff \n");
        return 2;
    }

    next += 8;
    int wave = convInt(header, next);
    if (wave != WAVH_WAVE) {
        printf("NOT match wave \n");
        return 3;
    }

    next += 4;
    int fmt = convInt(header, next);
    if (fmt == WAVH_FACT) {
        next += 12;
        fmt = convInt(header, next);
        if (fmt != WAVH_FMT) {
            printf("NOT match fmt (has fmt)\n");
            return 4;
        }
    } else if (fmt != WAVH_FMT) {
        printf("NOT match fmt \n");
        return 4;
    }
    int fmtPos = next;
    int chunkSize = convInt(header, fmtPos + 4);

    short pcm = convShort(header, fmtPos + 8);
    if (pcm != WAVH_WFORMATTAG_PCM) {
        return 5;
    }

    short wavchannels = (int) convShort(header, fmtPos + 10);
    int samplesPerSec = convInt(header, fmtPos + 12);
    short bitsParSample = (int) convShort(header, fmtPos + 22);

    next = next + 8 + chunkSize;
    int ov_data = convInt(header, next);
    if (ov_data == WAVH_FACT) {
        next += 12;
        ov_data = convInt(header, next);
        if (ov_data != WAVH_OV_DATA) {
            printf("NOT match data (has fact)\n");
            return 6;
        }
    } else if (ov_data == WAVH_LIST) {
        chunkSize = convInt(header, next + 4);
        next = next + 8 + chunkSize;
        ov_data = convInt(header, next);
        if (ov_data != WAVH_OV_DATA) {
            printf("NOT match data (has LIST)\n");
            return 6;
        }
    } else if (ov_data != WAVH_OV_DATA) {
        printf("NOT match data \n");
        return 6;
    }

    int ov_datasize = convInt(header, next + 4);
    *channels = (int) wavchannels;
    *bits = (int) bitsParSample;
    *samplingrate = samplesPerSec;
    *size = ov_datasize;

    return 0;
}

ALenum getFormat(int wavChannels, int wavBit) {
    ALenum format;
    if (wavChannels == WAVH_CHANNELS_MONO) {
        if(wavBit == 8) {
            format = AL_FORMAT_MONO8;
        } else if(wavBit == 16) {
            format = AL_FORMAT_MONO16;
        }
    } else if(wavChannels == WAVH_CHANNELS_STEREO) {
        if(wavBit== 8){
            format = AL_FORMAT_STEREO8;
        } else if(wavBit == 16) {
            format = AL_FORMAT_STEREO16;
        }
    }
    return format;
}

void playWavData(char *data) {
    ALCdevice *device = alcOpenDevice(NULL);
    if (!device) {
        printf("alcOpenDevice Faild");
        return;
    }

    ALCcontext *context = alcCreateContext(device, NULL);
    if (!context) {
        printf("alcCreateContext Faild");
        return;
    }
    alcMakeContextCurrent(context);

    ALuint buffer, source;
    alGenBuffers(1, &buffer);
    alGenSources(1, &source);

    int wavChannels, wavBit, wavSize, wavSamplingrate;
    int ret = parseWavHeader(data, &wavChannels, &wavBit, &wavSize, &wavSamplingrate);
    if (ret != 0) {
        return;
    }
    int time_playback = (float)wavSize / (float)(4*wavSamplingrate);

    ALenum format = getFormat(wavChannels,  wavBit);
    alBufferData(buffer, format, data, wavSize, wavSamplingrate);
    alSourcei (source, AL_BUFFER, buffer);
    alSourcePlay (source);

    int time_count = 0;
    while(1) {
        sleep (1);
        time_count++;
        if(time_count > time_playback) {
            break;
        }
    }

    alDeleteBuffers(1, &buffer);
    alDeleteSources(1, &source);
}

void playWavDataInThread(void *arg) {
    char *data = (char*)arg;
    playWavData(data);
}

void playWavFileInThread(void *arg) {
    char *filename = (char*)arg;

    wav_cache *cache = wavMatches(filename);
    if (!cache) {
        cache = loadWav(filename);
        if (!cache) {
            return;
        }
    }
    playWavData(cache->data);
}

void playWavFile(char *filename) {
    pthread_t pthread;
    pthread_create(&pthread, NULL, &playWavFileInThread, filename);
}

void playWav(char *data) {
    pthread_t pthread;
    pthread_create(&pthread, NULL, &playWavDataInThread, data);
}

void initWav() {
    char path[256];
    snprintf(path, 255, "%s/assets/sounds", dataDirectory);

    DIR *dir = opendir(path);
    for(struct dirent *ds = readdir(dir); ds != NULL; ds = readdir(dir) ){
        const char *ext = strrchr(ds->d_name, '.');
        if (strcmp(".wav", ext) == 0) {
            loadWav(ds->d_name);
        }
    }
    closedir(dir);
}

