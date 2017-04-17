/* tinycap.c
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/
#include "com_aispeech_audio_CpldAudioRecorder.h"
#include <tinyalsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1


#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <jni.h>
//#include <JNIHelp.h>

#include <android/log.h>
#define LOG_TAG "CPLD_AUDIO_JNI"
#define LOG_NDEBUG 1
#define PCM_IN     0x10000000
#define CPLD_AUDIO_CHANNEL_NUM      2
#define CPLD_AUDIO_RATE             64000
#define CPLD_AUDIO_PERIOD_SIZE      8000
#define CPLD_AUDIO_PERIOD_COUNT     8

#define CPLD_REAL_CHANNELS          6
#define CPLD_REAL_FRAME_SIZE        (3 * CPLD_REAL_CHANNELS) //24bits--->3bytes per channel

#define SSIZE 256
#define STRKEY "tlv320-pcm0-0"

static unsigned char g_s24le_data[CPLD_AUDIO_PERIOD_SIZE * CPLD_AUDIO_CHANNEL_NUM * 4]; //0.125 sec data
static unsigned int  g_s24le_data_pos = sizeof(g_s24le_data);
static unsigned int  g_s24le_periods = 0;

static struct pcm *g_pcm = NULL;

static FILE *g_s16le_pcm_file;
static int g_save_audio = 0;
static int cardn = 0;

#define ALOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__) // 定义LOGD类型
#define ALOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__) // 定义LOGI类型
#define ALOGW(...)  __android_log_print(ANDROID_LOG_WARN,LOG_TAG,__VA_ARGS__) // 定义LOGW类型
#define ALOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__) // 定义LOGE类型
#define ALOGF(...)  __android_log_print(ANDROID_LOG_FATAL,LOG_TAG,__VA_ARGS__) // 定义LOGF类型

static int check_card()
{
    char sbuf[SSIZE];
    FILE *fp = NULL;
    int ret = 0;

    if (ret == 0 && (fp = fopen("/proc/asound/card0/pcm0c/info","r")))
    {
        memset(sbuf, 0, SSIZE);
        fread(sbuf, SSIZE, 1, fp);
        
        if (strstr(sbuf, STRKEY))
            ret = 1;
        
        fclose(fp);
        fp = NULL;
    }
    
    if (ret == 0 && (fp = fopen("/proc/asound/card1/pcm0c/info","r")))
    {
        memset(sbuf, 0, SSIZE);
        fread(sbuf, SSIZE, 1, fp);
        
        if (strstr(sbuf, STRKEY))
            ret = 2;
     
        fclose(fp);
        fp = NULL;
    }
    
    if (ret == 0 && (fp = fopen("/proc/asound/card2/pcm0c/info","r")))
    {
        memset(sbuf, 0, SSIZE);
        fread(sbuf, SSIZE, 1, fp);
        
        if (strstr(sbuf, STRKEY))
            ret = 3;
        
        fclose(fp);
        fp = NULL;
    }
    
    return ret;
}

static void tinymix_set_value(struct mixer *mixer, const char *control,
                              char **values, unsigned int num_values)
{
    struct mixer_ctl *ctl;
    enum mixer_ctl_type type;
    unsigned int num_ctl_values;
    unsigned int i;


    ALOGE("tinymix_set_value111");
    if (isdigit(control[0]))
        ctl = mixer_get_ctl(mixer, atoi(control));
    else
        ctl = mixer_get_ctl_by_name(mixer, control);
    if (!ctl) {
        fprintf(stderr, "Invalid mixer control\n");
        ALOGE("tinymix_set_value113");
        return;
    }


    type = mixer_ctl_get_type(ctl);
    num_ctl_values = mixer_ctl_get_num_values(ctl);
    if (isdigit(values[0][0])) {
        if (num_values == 1) {
            /* Set all values the same */
            int value = atoi(values[0]);
            for (i = 0; i < num_ctl_values; i++) {
                if (mixer_ctl_set_value(ctl, i, value)) {
                    fprintf(stderr, "Error: invalid value\n");
                    return;
                }
            }
        } else {
            /* Set multiple values */
            if (num_values > num_ctl_values) {
                fprintf(stderr,
                        "Error: %d values given, but control only takes %d\n",
                        num_values, num_ctl_values);
                return;
            }
            for (i = 0; i < num_values; i++) {
                if (mixer_ctl_set_value(ctl, i, atoi(values[i]))) {
                    fprintf(stderr, "Error: invalid value for index %d\n", i);
                    return;
                }
            }
        }
    } else {
        if (type == MIXER_CTL_TYPE_ENUM) {
            if (num_values != 1) {
                fprintf(stderr, "Enclose strings in quotes and try again\n");
                return;
            }
            if (mixer_ctl_set_enum_by_string(ctl, values[0]))
                fprintf(stderr, "Error: invalid enum value\n");
        } else {
            fprintf(stderr, "Error: only enum types can be set with strings\n");
        }
    }
}



/*
 * Class:     com_aispeech_audio_SpiAudioRecorder
 * Method:    native_read
 * Signature: ([BII)I
 */

static void JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1save_audio(JNIEnv *env, jclass clazz) {
    g_save_audio = 1;
}

static int cpld_s24le_pcm_read(struct pcm *pcm, char *buffer, int size)
{
    unsigned char *p_s24le_value;
    unsigned char *p_s24le_dst_buf;
    int dst_buf_index, frames;
    if (size % CPLD_REAL_FRAME_SIZE) {
        ALOGE("Incorrect pcm read size (%d), should be N*%d", size, CPLD_REAL_FRAME_SIZE);
        return -1;
    }
    frames = 0;
    p_s16le_dst_buf = buffer;
    for(dst_buf_index = 0; dst_buf_index < size / 3; dst_buf_index++) 
    {
        if(g_s24le_data_pos >= sizeof(g_s24le_data))
        {
            g_s24le_data_pos = 0;
            if (!pcm_read(pcm, &g_s24le_data[0], sizeof(g_s24le_data))) {
                g_s24le_periods++;
            } else {
                ALOGE("pcm read error, pcm handler=0x%08x", pcm);
                return -1;
            }
        }
    }
    p_s24le_value = (unsigned char *)&g_s24le_data[g_s24le_data_pos];  
    if(*(p_s24le_value + 2) & 0x01) {
        //First channel
        if (dst_buf_index % CPLD_REAL_CHANNELS) {
                ALOGW("WARNING: s24le to s24le is not aligned, dst_buf_index=%d, frames=%d, g_s24le_periods=%u\n", dst_buf_index, frames, g_s24le_periods);
                dst_buf_index = (dst_buf_index / CPLD_REAL_CHANNELS) * CPLD_REAL_CHANNELS;
            }

            frames++;
            if (frames > (size / CPLD_REAL_FRAME_SIZE * 2)) {
                //Invalid data, assume read frames is impossible over 2 expected frames.
                frames = 0;
                break;
            }
    }
    *(p_s16le_dst_buf + dst_buf_index) = *(p_s24le_value);
    *(p_s16le_dst_buf + dst_buf_index + 1) = *(p_s24le_value + 1);
    *(p_s16le_dst_buf + dst_buf_index + 2) = *(p_s24le_value + 2);
    g_s24le_data_pos = g_s24le_data_pos + 3;
    if (frames) {
        return size;
    } else {
        return -1;
    }
    
}

static int cpld_s16le_pcm_read(struct pcm *pcm, char *buffer, int size)
{
    unsigned int s24le_value, *p_s24le_value;
    unsigned short *p_s16le_dst_buf;
    int dst_buf_index, frames;

    if (size % CPLD_REAL_FRAME_SIZE) {
        ALOGE("Incorrect pcm read size (%d), should be N*%d", size, CPLD_REAL_FRAME_SIZE);
        return -1;
    }

    frames = 0;
    p_s16le_dst_buf = (unsigned short *)buffer;
    for (dst_buf_index=0; dst_buf_index<(size / sizeof(unsigned short)); dst_buf_index++) {
        if (g_s24le_data_pos >= sizeof(g_s24le_data)) {
            g_s24le_data_pos = 0;
            if (!pcm_read(pcm, &g_s24le_data[0], sizeof(g_s24le_data))) {
                g_s24le_periods++;
            } else {
                ALOGE("pcm read error, pcm handler=0x%08x", pcm);
                return -1;
            }
        }

        p_s24le_value = (unsigned int *)&g_s24le_data[g_s24le_data_pos];
        s24le_value = (*p_s24le_value) >> 8;
        if (s24le_value & 0x01) {
            // Firt channel
            if (dst_buf_index % CPLD_REAL_CHANNELS) {
                ALOGW("WARNING: s24le to s16le is not aligned, dst_buf_index=%d, frames=%d, g_s24le_periods=%u\n", dst_buf_index, frames, g_s24le_periods);
                dst_buf_index = (dst_buf_index / CPLD_REAL_CHANNELS) * CPLD_REAL_CHANNELS;
            }

            frames++;
            if (frames > (size / CPLD_REAL_FRAME_SIZE * 2)) {
                //Invalid data, assume read frames is impossible over 2 expected frames.
                frames = 0;
                break;
            }
        }

        *(p_s16le_dst_buf + dst_buf_index) = (unsigned short)(s24le_value >> 8);
        g_s24le_data_pos = g_s24le_data_pos + sizeof(unsigned int);
    }

    if (frames) {
        return size;
    } else {
        return -1;
    }
}

static jint JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1read(JNIEnv *env, jclass clazz, jbyteArray javaAudioData,
    jint channelBits, jint sizeInBytes)
{
    jbyte* recordBuff = NULL;
    int readbytes;

    if (!g_pcm) {  
       ALOGE("g_pcm is null");
       return 0;
    }
    if (!pcm_is_ready(g_pcm)) {
        ALOGE("Unable to open PCM device (%s)\n", pcm_get_error(g_pcm));
        return 0;
    }
    if (!javaAudioData) {
        ALOGE("Invalid Java array to store recorded audio, can't record");
        return 0;
    }

    recordBuff = (jbyte *)(*env)->GetByteArrayElements(env, javaAudioData, NULL);
    if (recordBuff == NULL)  {
        ALOGE("Error retrieving destination for recorded audio data, can't record");
        return 0;
    }

    readbytes = cpld_s24le_pcm_read(g_pcm, recordBuff, sizeInBytes);
    if (readbytes > 0) {
    } else {
        readbytes = 0;
        ALOGE("Invalid operation, handler=0x%08x", g_pcm);
    }

    if ((g_s16le_pcm_file) && (readbytes > 0)) {
        if (fwrite(recordBuff, 1, readbytes, g_s16le_pcm_file) != readbytes) {
            ALOGE("Error capturing raw_file\n");
        }
    }

    (*env)->ReleaseByteArrayElements(env, javaAudioData, recordBuff, 0);
    return (jint)readbytes;
}


/*
 * Class:     com_aispeech_audio_SpiAudioRecorder
 * Method:    native_setup
 * Signature: (IIIII)I
 */
static jint JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1setup(JNIEnv *env,jclass clazz,jint rate,jint channelNum,jint  format,jint period_size,jint period_count)
{

    struct pcm_config config;
    char *buffer;
    unsigned int size;
    unsigned int bytes_read = 0;
    config.channels = CPLD_AUDIO_CHANNEL_NUM;
    config.rate = CPLD_AUDIO_RATE;
    config.period_size = CPLD_AUDIO_PERIOD_SIZE;
    config.period_count = CPLD_AUDIO_PERIOD_COUNT;
    config.format = PCM_FORMAT_S24_LE;
    config.silence_threshold = 0;
    config.start_threshold = 0;
    config.stop_threshold = 0;

    if (g_pcm != NULL) {
        pcm_close(g_pcm);
        ALOGE("CPLD Audio has been open!");
    //    return (jint)-1;
    }

    int n = check_card();
    
    if (n == 0)
        cardn = 1;
    else if (n > 0)
        cardn = n - 1;

    ALOGE("*********card:%d",cardn);
    g_pcm = pcm_open(cardn, 0, PCM_IN, &config);
    if (!g_pcm) {
        ALOGE("g_pcm is null");
        return 0;
    }
    if (!pcm_is_ready(g_pcm)) {
        ALOGE("Unable to open PCM device (%s)\n", pcm_get_error(g_pcm));
        pcm_close(g_pcm);

        g_pcm = NULL;
        return 0;
    }

    g_s24le_data_pos = sizeof(g_s24le_data);
    g_s24le_periods = 0;

    if(g_save_audio)
    {    
        if (g_s16le_pcm_file !=NULL) fclose(g_s16le_pcm_file);

        g_s16le_pcm_file = fopen("/sdcard/raw16bit.pcm", "wb");
        if (!g_s16le_pcm_file) ALOGE("Unable to create /sdcard/raw16bit.pcm file ");
    }

    return 0;
}

/*
 * Class:     com_aispeech_audio_SpiAudioRecorder
 * Method:    native_release
 * Signature: ()V
 */
static void JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1release(JNIEnv *env, jclass clazz) {
    if (g_pcm) {
        pcm_close(g_pcm);
        g_pcm = NULL;
        if(g_s16le_pcm_file != NULL)
        fclose(g_s16le_pcm_file);
    }
}


/*
 * Class:     com_aispeech_audio_SpiAudioRecorder
 * Method:    native_set_channels_gain
 * Signature: ([BI)I
 */
static jint JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1set_1channels_1gain(JNIEnv *env, jclass clazz, jbyteArray javaChannelsGainData,
    jint channelNum) {
    jbyte* channelsGainData = NULL;
    int i, errCode=0;
    char buff_value[2][10];
    char buff_CH[5];
    char *dddd[]={};
    char *p;



    struct mixer *mixer=NULL;
    mixer = mixer_open(cardn);
    if (!mixer) {
    fprintf(stderr, "Failed to open mixer\n");
    return EXIT_FAILURE;
    }

    channelsGainData = (jbyte *)(*env)->GetByteArrayElements(env, javaChannelsGainData, NULL);

    if (channelsGainData == NULL) {
    ALOGE("Error retrieving destination for channels gain data");
    return -1;
    }
    
    // //int sprintf( char *buffer, const char *format, [ argument] … );
    i = 0;
    do {
        sprintf(buff_value[0], "%d",channelsGainData[i]);

        ALOGE("buff_value[0]=%s", buff_value[0]);

        sprintf(buff_CH, "%d", i);

        ALOGE("buff_CH=%s", buff_CH);

        p = buff_value[0];

        dddd[0] = p;
        
        tinymix_set_value(mixer, buff_CH, &dddd[0], 1); 

        i++;
    } while(i<channelNum);

    mixer_close(mixer);
    (*env)->ReleaseByteArrayElements(env, javaChannelsGainData, channelsGainData, 0);
    return (jint)errCode;
}



static JNINativeMethod methods[] = {

    {"native_setup", "(IIIII)I", Java_com_aispeech_audio_CpldAudioRecorder_native_1setup },

    {"native_read", "([BII)I", Java_com_aispeech_audio_CpldAudioRecorder_native_1read },

    {"native_release", "()V", Java_com_aispeech_audio_CpldAudioRecorder_native_1release },

    {"native_set_channels_gain", "([BI)I", Java_com_aispeech_audio_CpldAudioRecorder_native_1set_1channels_1gain },

    {"native_save_audio", "()V", Java_com_aispeech_audio_CpldAudioRecorder_native_1save_audio },

};


int registerNatives(JNIEnv *env) {
/*
    return jniRegisterNativeMethods(
            env, "com/aispeech/audio/SpiAudioRecorder",
            methods, sizeof(methods) / sizeof(JNINativeMethod));
*/
    jclass jcls = (*env)->FindClass(env, "com/aispeech/audio/CpldAudioRecorder");
    if (!jcls) return JNI_ERR;

    return (*env)->RegisterNatives(env, jcls, methods, sizeof(methods)/sizeof(methods[0]));
}

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = NULL;

    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
        return JNI_ERR;
    }

    if (registerNatives(env)) {
        //ALOGE("failed to register native methods for 'com/aispeech/audio/SpiAudioRecorder'");
        return JNI_ERR;
    }

    return JNI_VERSION_1_4;
}