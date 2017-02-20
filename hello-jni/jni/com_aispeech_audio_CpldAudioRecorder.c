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
#define CPLD_AUDIO_CHANNEL_NUM   8
#define  CHANNELBITS_16   0
#define  CHANNELBITS_24   1
static struct pcm *g_pcm = NULL;

FILE *file;
FILE *raw_file;
int save_audio = 0;

#define ALOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__) // 定义LOGD类型
#define ALOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__) // 定义LOGI类型
#define ALOGW(...)  __android_log_print(ANDROID_LOG_WARN,LOG_TAG,__VA_ARGS__) // 定义LOGW类型
#define ALOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__) // 定义LOGE类型
#define ALOGF(...)  __android_log_print(ANDROID_LOG_FATAL,LOG_TAG,__VA_ARGS__) // 定义LOGF类型


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
#define u32 unsigned int
char remaind[32];
int  rmaind_cnt;
int find_channel0(u32 *RXBuff,u32 RXBuffLen)
{
    u32 Channel1Pos;
    u32 *p;
    p=RXBuff;
    while(!(*p&0x100))
    {
        if(p < RXBuff + RXBuffLen - 1)
        p++;
        else return -1;
    }
    return (p - RXBuff);
}

int check_channel0(char RXBuff)
{
    if(RXBuff & 0x01) 
    {
        return 1;
    } 
    else 
    {
        return 0;
    } 
}

static void JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1save_audio(JNIEnv *env, jclass clazz) {
    save_audio = 1;
}



static jint JNICALL Java_com_aispeech_audio_CpldAudioRecorder_native_1read(JNIEnv *env, jclass clazz, jbyteArray javaAudioData,
    jint channelBits, jint sizeInBytes)
{
    jbyte* recordBuff = NULL;
    jbyte* p =NULL ;
    int sendPointCnt;
    int needReadBytes;
    int ch1_pos;
    if (!g_pcm)
    {  
       ALOGE("g_pcm is null");
       return 0;
    }
    if (!pcm_is_ready(g_pcm))
    {
        // fprintf(stderr, "Unable to open PCM device (%s)\n",
        // pcm_get_error(g_pcm));
        ALOGE("Unable to open PCM device (%s)\n", pcm_get_error(g_pcm));
        return 0;
    }
    if (!javaAudioData) {
        ALOGE("Invalid Java array to store recorded audio, can't record");
        return 0;
    }

    recordBuff = (jbyte *)(*env)->GetByteArrayElements(env, javaAudioData, NULL);
  
    if (recordBuff == NULL) 
    {
        ALOGE("Error retrieving destination for recorded audio data, can't record");
        return 0;
    }


    jbyte* recordBuff_read=recordBuff+32;
    u32 cnt_read=sizeInBytes-32;
    u32 channels_NUM =8;//[0,7]

    if (pcm_read(g_pcm,recordBuff_read ,cnt_read ) < 0) //==0 is OK 
    {
        ALOGE("Invalid operation, handler=0x%08x", g_pcm);
        return 0;
    }

    if(save_audio)
    {  
        if (fwrite(recordBuff_read, 1, cnt_read, raw_file) != cnt_read)
        {
            ALOGE("Error capturing raw_file\n");
        }
    }

    ch1_pos=find_channel0((u32 *)recordBuff_read,8);
    if(ch1_pos<0)
    {
        ALOGE("find_channel0 Error head");
        //return 0;
        p=recordBuff_read-(rmaind_cnt)*4;

        memcpy(p, remaind, rmaind_cnt*4);
        sendPointCnt =cnt_read/4+rmaind_cnt;
    }
    else if((ch1_pos+rmaind_cnt) % (channels_NUM)==0)
    {
       p=recordBuff_read-(rmaind_cnt)*4;

       memcpy(p, remaind, rmaind_cnt*4);
       //ALOGE(" sizeInBytes= %d", sizeInBytes);
       sendPointCnt =cnt_read/4+rmaind_cnt;
       //ALOGE(" sendPointCnt =  %d", sendPointCnt);
    }
    else 
    {
        p =recordBuff_read+ch1_pos*4;
        sendPointCnt =cnt_read/4 -ch1_pos;
        //ALOGE(" sendPointCnt=%d", sendPointCnt);

    }


    ch1_pos=find_channel0((u32 *)((recordBuff_read+cnt_read-32)),8);
    //if(ch1_pos<0)
    //{
        //ALOGE("find_channel0 Error tail");
        //return 0;
    //}
         //ALOGE("SEC_ch1_pos %d", ch1_pos);
    if(ch1_pos > 0) 
    {
        rmaind_cnt =8 -ch1_pos;
        memcpy(remaind, recordBuff_read+cnt_read-(rmaind_cnt)*4 , rmaind_cnt*4);
        sendPointCnt-=rmaind_cnt;
    }
    else
    {
        rmaind_cnt =0;
    } 
          //  3 copy mass data

          if(channelBits&0x1)
          {
                int i=0;
                int destIndex=0;
                int lastIndex=0;
                while(i<sendPointCnt)
                {
                    int isChannel0 = check_channel0(*(p + i*4 + 1));
                    if(destIndex % 8 == 0 && !isChannel0)
                    {
                        i++;
                        continue;
                    }
                    else if(destIndex % 8 != 0 && isChannel0) 
                    {
                        //let destIndex to be last channel0 index
                        destIndex = lastIndex;
                        i++;
                        continue;
                    }
                    else if(destIndex % 8 ==0 && isChannel0)
                    {
                        lastIndex = destIndex;
                    }
                    
                    recordBuff[destIndex*3]  = *(p+i*4+1);    
                    recordBuff[destIndex*3+1]= *(p+i*4+2);   
                    recordBuff[destIndex*3+2]= *(p+i*4+3);
                    
                    destIndex++;
                    i++;

                }
                (*env)->ReleaseByteArrayElements(env, javaAudioData, recordBuff, 0);
                if(destIndex < 8)
                { 
                  rmaind_cnt = 0;
                  return (jint) 0;
                }
                return (jint) destIndex*3;

          }else //CHANNELBITS_16
          {
              int i=0;
              int destIndex=0;
              int lastIndex=0;
              while(i<sendPointCnt)
              {
                int isChannel0 = check_channel0(*(p + i*4 + 1));
                if(destIndex % 8 == 0 && !isChannel0)
                {
                    ALOGE("destIndex : %d, i : %d, expected 0 channel, but not 0 channel", destIndex, i);
                    i++;
                    continue;
                }
                else if(destIndex % 8 != 0 && isChannel0) 
                {
                    ALOGE("destIndex : %d, i : %d, expected not 0 channel, but 0 channel", destIndex, i);
                    //let destIndex to be last channel0 index
                    destIndex = lastIndex;
                    i++;
                    continue;
                }
                else if(destIndex % 8 ==0 && isChannel0)
                {
                    lastIndex = destIndex;
                }
                recordBuff[destIndex*2]  =  *(p+i*4+2); 
                recordBuff[destIndex*2+1]= *(p+i*4+3);

                destIndex++;
                i++;
              }
              if(destIndex < 8)
              { 
                 rmaind_cnt = 0;
                 (*env)->ReleaseByteArrayElements(env, javaAudioData, recordBuff, 0);
                 return (jint) 0;
              }                

              if(save_audio)
              {  
                  if (fwrite(recordBuff, 1, sendPointCnt*2, file) != sendPointCnt*2)
                  {
                    ALOGE("Error capturing sample\n");
                  }
              }
        
                (*env)->ReleaseByteArrayElements(env, javaAudioData, recordBuff, 0);
                // ALOGE("sendPointCnt*2= %d ", sendPointCnt*2);//32000
                return (jint) destIndex*2;

          }
          
        return 0;
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
    config.channels = 2;
    config.rate = 64000;
    config.period_size = 8000;
    config.period_count = 8;
    config.format = PCM_FORMAT_S24_LE;
    config.silence_threshold = 0;
    config.start_threshold = 0;
    config.stop_threshold = 0;

    if (g_pcm != NULL) {
        pcm_close(g_pcm);
        ALOGE("CPLD Audio has been open!");
    //    return (jint)-1;
    }


    g_pcm = pcm_open(1, 0, PCM_IN, &config);
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


    if(save_audio)
    {    
        if(file !=NULL)
            fclose(file);
        file = fopen("/sdcard/raw16bit.pcm", "wb");
        if (!file) 
            ALOGE("Unable to create raw16bit.pcm file ");
        if(raw_file != NULL)
            fclose(raw_file);
        raw_file = fopen("/sdcard/raw32bit.pcm", "wb");
        if(raw_file != NULL)
            ALOGE("Unable to create raw32bit.pcm file ");
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
        if(file != NULL)
        fclose(file);
        if(raw_file != NULL)
        fclose(raw_file);
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
    mixer = mixer_open(1);
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