/*
 * soundaudioqueue.c - Implementation of the AudioQueue sound device.
 *
 * Written by
 *  Michael Klein <michael.klein@puffin.lb.shuttle.de>
 *  Christian Vogelgsang <C.Vogelgsang@web.de> (Ported to Intel Mac)
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

/*
 * Notation Hints:
 *
 * Mac OS X Audio:
 *  1 Packet = 1 Frame 
 *  1 Frame  = 1 or 2 Samples (SWORD)  1:mono SID, 2:stereo SID
 *  1 Slice  = n Frames
 *
 * VICE Audio:
 *  1 Fragment = n Frames     (n=fragsize)
 *  Soundbuffer = m Fragments (m=fragnum)
 *
 * VICE Fragment = CoreAudio Slice
 */

//#include "config.h"
#include "vice.h"

#include <AudioToolbox/AudioToolbox.h>
#include <libkern/OSAtomic.h>

#include "lib.h"
#include "log.h"
#include "sound.h"

/* type for atomic increments */
typedef volatile int atomic_int_t;

/* the cyclic buffer containing m fragments */
static SWORD *soundbuffer;

/* silence fragment */
static SWORD *silence;

/* current read position: no. of fragment in soundbuffer */
static unsigned int read_position;

/* the next position to write: no. of fragment in soundbuffer */
static unsigned int write_position;

static unsigned int frames_in_fragment;
static unsigned int bytes_in_fragment;
static unsigned int samples_in_fragment;

/* total number of fragments */
static unsigned int fragment_count;

/* current number of fragments in buffer */
static atomic_int_t fragments_in_queue;

/* number of interleaved channels (mono SID=1, stereo SID=2) */
static int in_channels;

/* samples left in current fragment */
static unsigned int frames_left_in_fragment;

/* bytes per input frame */
static unsigned int in_frame_byte_size;

static const int    kNumberBuffers = 3;

static AudioStreamBasicDescription sStreamFormat;
static AudioQueueRef			   sQueue;
static AudioQueueBufferRef		   sAQBuffers[kNumberBuffers];
static int                         sAQBufferByteSize = 0; 

static int                         sStopping = 0;
static int                         sAudioSessionInitialized = 0;
static int                         sAudioSessionInterrupted = 0;


static void sAudioQueueOutputCallback(void* inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
{
    if (sStopping)
        return;
    
	register short* aqBuffer	= (short*) inBuffer->mAudioData;
    
    UInt32 num_frames = sAQBufferByteSize / sizeof(short) / sStreamFormat.mChannelsPerFrame;
    
    SWORD *buffer;
    if (fragments_in_queue) 
    {
        /* too many -> crop to available in current fragment */
        if(num_frames > frames_left_in_fragment) {
            num_frames = frames_left_in_fragment;
        }
        
        /* calc position in sound buffer */
        int sample_offset_in_fragment = frames_in_fragment - frames_left_in_fragment;
        buffer = soundbuffer + samples_in_fragment * read_position + sample_offset_in_fragment * in_channels;
        
        /* update the samples left in the current fragment */
        frames_left_in_fragment -= num_frames;
        
        /* fetch next fragment */
        if(frames_left_in_fragment <= 0) {
            read_position = (read_position + 1) % fragment_count;
            OSAtomicDecrement32(&fragments_in_queue);
            frames_left_in_fragment = frames_in_fragment;
            
            //printf("At read: Fragments in queue: %d\n", fragments_in_queue);
        }
    }
    else
    {
        if(num_frames > frames_in_fragment)
            num_frames = frames_in_fragment;
        
        /* output silence */
        buffer = silence;
    }

    memcpy(aqBuffer, buffer, sAQBufferByteSize);
    
	// tell core audio how many bytes we have just filled
	inBuffer->mAudioDataByteSize = sAQBufferByteSize;
    
	//fprintf(stderr, "enqueuing new audio block w/ %d bytes\n", inBuffer->mAudioDataByteSize);

    if (sAudioSessionInterrupted)
        return;
    
	// enqueue the new buffer
	OSStatus err = AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, 0); // CBR has no packet descriptions
	if (err) fprintf(stderr, "AudioQueueEnqueueBuffer err %ld\n", (int)err);
}



static void sAudioSessionInterruptionCallback(void* inClientData, UInt32 inInterruptionState)
{
    if (inInterruptionState == kAudioSessionBeginInterruption)
    {
        AudioQueuePause(sQueue);
        OSStatus err = AudioSessionSetActive(false);
        if (err)
            fprintf(stderr, "WARNING: AudioSessionSetActive err %d\n", (int)err);
        
        sAudioSessionInterrupted = true;
        printf("Audio session interrupted\n");
    }
    else if (inInterruptionState == kAudioSessionEndInterruption)
    {
        // set the audio session category
        UInt32 sessionCategory = kAudioSessionCategory_MediaPlayback;
        OSStatus err = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof (sessionCategory), &sessionCategory);
        if (err)
            fprintf(stderr, "WARNING: AudioSessionSetProperty (AudioCategory) err %ld\n", (int)err);
        
        err = AudioSessionSetActive(true);
        if (err)
            fprintf(stderr, "WARNING: AudioSessionSetActive err %d\n", (int)err);

        AudioQueueStart(sQueue, NULL);

        sAudioSessionInterrupted = false;
        read_position = 0;
        write_position = 0;
        fragments_in_queue = 0;
        frames_left_in_fragment = frames_in_fragment;
        printf("Audio session resumed\n");
    }
}

static void sAudioSessionPropertyChangeCallback(void* inClientData, AudioSessionPropertyID inID, UInt32 inDataSize, const void* inData)
{
	fprintf(stderr, "FIXME: handle audio session property change\n");
}

static void sAudioQueuePropertyChangeCallback(void* inUserData, AudioQueueRef inAQ, AudioQueuePropertyID inID)
{
	fprintf(stderr, "FIXME: handle audio queue property change\n");
}


static int audioqueue_resume(void);

static int audioqueue_init(const char *param, int *speed, int *fragsize, int *fragnr, int *channels)
{
    fragment_count     = *fragnr;
    frames_in_fragment = *fragsize;
    in_channels        = *channels;
    
    samples_in_fragment = frames_in_fragment * in_channels;
    bytes_in_fragment  = samples_in_fragment * sizeof(SWORD);
    
    in_frame_byte_size = sizeof(SWORD) * in_channels;
    
    soundbuffer = lib_calloc(fragment_count, bytes_in_fragment);
    silence = lib_calloc(1, bytes_in_fragment);
    
	sStreamFormat.mBitsPerChannel = 16;
	sStreamFormat.mSampleRate = *speed;
    
    // register with AudioSession API (iPhone OS 2.1 or later)
    OSStatus err = 0;
    
    if (!sAudioSessionInitialized)
    {
        err = AudioSessionInitialize(NULL, NULL, sAudioSessionInterruptionCallback, NULL);
        sAudioSessionInitialized = 1;
    }

	if (err) // will fail on the simulator
	{
		fprintf(stderr, "WARNING: AudioSessionInitialize err %ld\n", (int)err);
	}
	else
	{
		// set the audio session category
		UInt32 sessionCategory = kAudioSessionCategory_MediaPlayback;
		err = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof (sessionCategory), &sessionCategory);
		if (err)
			fprintf(stderr, "WARNING: AudioSessionSetProperty (AudioCategory) err %ld\n", (int)err);
	}

    sStreamFormat.mFormatID = kAudioFormatLinearPCM;
    sStreamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;

    if (in_channels == 1)
    {
        sStreamFormat.mBytesPerPacket = 2;
        sStreamFormat.mFramesPerPacket = 1;
        sStreamFormat.mBytesPerFrame = 2;
        sStreamFormat.mChannelsPerFrame = 1;
    }
    else
    {
        sStreamFormat.mBytesPerPacket = 4;
        sStreamFormat.mFramesPerPacket = 1;
        sStreamFormat.mBytesPerFrame = 4;
        sStreamFormat.mChannelsPerFrame = 2;
    }
        
    // create new audio queue
    err = AudioQueueNewOutput(&sStreamFormat,
                              sAudioQueueOutputCallback,
                              NULL,
                              CFRunLoopGetCurrent(),
                              kCFRunLoopCommonModes,
                              0,
                              &sQueue);
    
    if (err) fprintf(stderr, "AudioQueueNewOutput err %ld\n", (int)err);
    
    // allocate audio queue buffers
    sAQBufferByteSize = bytes_in_fragment;
    fprintf(stderr, "audio queue buffer size = %d bytes\n", sAQBufferByteSize);
    
    for (int i=0; i< kNumberBuffers; i++) {
        err = AudioQueueAllocateBuffer(sQueue, sAQBufferByteSize, &sAQBuffers[i]);
        if (err) fprintf(stderr, "AudioQueueAllocateBuffer [%d] err %ld\n",i, (int)err);
        
        // fill buffer
        sAudioQueueOutputCallback(NULL, sQueue, sAQBuffers[i]);
    }
    
    // global volume setup
    err = AudioQueueSetParameter(sQueue, kAudioQueueParam_Volume, 1.0f);
    if (err) fprintf(stderr, "AudioQueueSetParameter err %ld\n", (int)err);
    
    // register property change callback
    err = AudioQueueAddPropertyListener(sQueue, kAudioQueueProperty_IsRunning, sAudioQueuePropertyChangeCallback, NULL);
    if (err) fprintf(stderr, "AudioQueueAddPropertyListener err %ld\n", (int)err);
    
    // launch audio queues
    err = AudioQueueStart(sQueue, NULL);
    if (err) fprintf(stderr, "AudioQueueStart err %ld\n", (int)err);
    
    return 0;
}

static int audioqueue_write(SWORD *pbuf, size_t nr)
{
    int i, count;
    
    /* number of fragments */
    count = nr / samples_in_fragment;
    
    for (i = 0; i < count; i++)
    {
        if (fragments_in_queue == fragment_count)
        {
            log_warning(LOG_DEFAULT, "sound (coreaudio): buffer overrun");
            return 0;
        }
        
        memcpy(soundbuffer + samples_in_fragment * write_position,
               pbuf + i * samples_in_fragment,
               bytes_in_fragment);
        
        write_position = (write_position + 1) % fragment_count;
        
        OSAtomicIncrement32(&fragments_in_queue);
        
        //printf("At write: Fragments in queue: %d\n", fragments_in_queue);
    }

    return 0;
}

static int audioqueue_bufferspace(void)
{
    return (fragment_count - fragments_in_queue) * frames_in_fragment;
}

static void audioqueue_close(void)
{
	OSStatus err = AudioQueueFlush(sQueue);
	if (err) fprintf(stderr, "AudioQueueFlush err %ld\n", (int)err);

    sStopping = 1;
    
	err = AudioQueueStop(sQueue, true); // false = stop async
	if (err) fprintf(stderr, "AudioQueueStop err %ld\n", (int)err);

    sStopping = 0;

    AudioQueueDispose(sQueue, true);
	if (err) fprintf(stderr, "AudioQueueDispose err %ld\n", (int)err);

	err = AudioSessionSetActive(false);
	if (err)
		fprintf(stderr, "WARNING: AudioSessionSetActive err %d\n", (int)err);

    lib_free(soundbuffer);
    lib_free(silence);
}

static int audioqueue_suspend(void)
{
    
//	OSStatus err = AudioSessionSetActive(false);
//	if (err)
//		fprintf(stderr, "WARNING: AudioSessionSetActive err %d\n", (int)err);

    return 0;
}

static int audioqueue_resume(void)
{
    read_position = 0;
    write_position = 0;
    fragments_in_queue = 0;
    frames_left_in_fragment = frames_in_fragment;

//	OSStatus err = AudioSessionSetActive(true);
//	if (err)
//		fprintf(stderr, "WARNING: AudioSessionSetActive err %d\n", (int)err);
    
    return 0;
}

static sound_device_t audioqueue_device =
{
    "audioqueue",
    audioqueue_init,
    audioqueue_write,
    NULL,
    NULL,
    audioqueue_bufferspace,
    audioqueue_close,
    audioqueue_suspend,
    audioqueue_resume,
    1
};

int sound_init_audioqueue_device(void)
{
    return sound_register_device(&audioqueue_device);
}
