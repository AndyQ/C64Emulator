/*
 * viceglview.m - VICEGLView
 *
 * Written by
 *  Christian Vogelgsang <chris@vogelgsang.org>
 *  Michael Klein <michael.klein@puffin.lb.shuttle.de>
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


#include <libkern/OSAtomic.h>
#include <CoreVideo/CVHostTime.h>
#include <QuartzCore/QuartzCore.h>

#include "lib.h"
#include "log.h"
#include "videoarch.h"
#include "vsync.h"
#include "keyboard.h"
#include "kbdbuf.h"
#include "charset.h"

#import "viceglview.h"
#import "vicenotifications.h"
#import "vicemachine.h"

#import "ShaderUtilities.h"


//#define DEBUG_SYNC

@implementation VICEGLView
{
    UIView *accessoryView;
}

// ----------------------------------------------------------------------------
+ (Class)layerClass
// ----------------------------------------------------------------------------
{
    return [CAEAGLLayer class];
}


// ----------------------------------------------------------------------------
- (void) awakeFromNib
// ----------------------------------------------------------------------------
{
    [super awakeFromNib];
    
    cvTextureCache = NULL;
    [self initOpenGL];
    
//    accessoryView = [[UIView alloc] initWithFrame:CGRectMake( 0, 0, 320, 77)];
//    accessoryView.backgroundColor = [UIColor redColor];
    
}


enum
{
    ATTRIB_VERTEX,
    ATTRIB_TEXTUREPOSITON,
    NUM_ATTRIBUTES
};


// attributes
static GLint sAttribLocation[NUM_ATTRIBUTES] = 
{
    ATTRIB_VERTEX, ATTRIB_TEXTUREPOSITON
};

static GLchar *sAttribName[NUM_ATTRIBUTES] = 
{
    "position", "textureCoordinate"
};


/*
- (UIView *) inputAccessoryView {
    return accessoryView;
}
*/

- (UITextAutocorrectionType) autocorrectionType {
    return UITextAutocorrectionTypeNo;
}

// ----------------------------------------------------------------------------
- (void) createCVTextureCache
// ----------------------------------------------------------------------------
{
    [EAGLContext setCurrentContext:eaglContext];
    
    if (cvTextureCache != NULL)
    {
        glDeleteFramebuffers(1, &frameBufferHandle);
        glDeleteRenderbuffers(1, &colorBufferHandle);
        CFRelease(cvTextureCache);
    }
    
    glGenFramebuffers(1, &frameBufferHandle);
    glBindFramebuffer(GL_FRAMEBUFFER, frameBufferHandle);
    
    glGenRenderbuffers(1, &colorBufferHandle);
    glBindRenderbuffer(GL_RENDERBUFFER, colorBufferHandle);

    // Allocate storage for render buffer from layer
    CAEAGLLayer* eaglLayer = (CAEAGLLayer *)self.layer;
    [eaglContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglLayer];
    
	glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &renderBufferWidth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &renderBufferHeight);
    
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBufferHandle);
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        NSLog(@"Failure with framebuffer generation");
    
    //  Create a new CVOpenGLESTexture cache
    CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, (__bridge CVEAGLContext)((__bridge_retained void*)eaglContext), NULL, &cvTextureCache);
    if (err)
        NSLog(@"Error at CVOpenGLESTextureCacheCreate %d", err);
}


#define USE_PASSTHROUGH_SHADER

// ----------------------------------------------------------------------------
- (void) initOpenGL
// ----------------------------------------------------------------------------
{
    isRenderingAllowed = YES;
    
    // Use 2x scale factor on Retina displays.
    self.contentScaleFactor = [[UIScreen mainScreen] scale];

    // Initialize OpenGL ES 2
    CAEAGLLayer* eaglLayer = (CAEAGLLayer *)self.layer;
    eaglLayer.opaque = YES;
    eaglLayer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [NSNumber numberWithBool:NO], kEAGLDrawablePropertyRetainedBacking,
                                    kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat,
                                    nil];
    //eaglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES1];
    eaglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    if (!eaglContext || ![EAGLContext setCurrentContext:eaglContext])
    {
        //NSLog(@"Problem with OpenGL context.");
        return;
    }
    
    glDisable(GL_DEPTH_TEST);
    
    [self createCVTextureCache];

    // Load vertex and fragment shaders

#ifdef USE_PASSTHROUGH_SHADER    
    const GLchar *vertSrc = "attribute vec4 position; attribute mediump vec4 textureCoordinate; varying mediump vec2 coordinate; void main() { gl_Position = position; coordinate = textureCoordinate.xy; }";    
    const GLchar *fragSrc = "varying highp vec2 coordinate; uniform sampler2D videoframe; void main() { gl_FragColor = texture2D(videoframe, coordinate); }";
#else    
    NSString* vertShaderPath = [[NSBundle mainBundle] pathForResource:@"crt_shader.vsh" ofType: nil];
    const GLchar *vertSrc = (GLchar *)[[NSString stringWithContentsOfFile:vertShaderPath encoding:NSUTF8StringEncoding error:nil] UTF8String];
    
    NSString* fragShaderPath = [[NSBundle mainBundle] pathForResource:@"crt_shader.fsh" ofType: nil];
    const GLchar *fragSrc = (GLchar *)[[NSString stringWithContentsOfFile:fragShaderPath encoding:NSUTF8StringEncoding error:nil] UTF8String];
#endif
    glueCreateProgram(vertSrc, fragSrc,
                      NUM_ATTRIBUTES, (const GLchar **)&sAttribName[0], sAttribLocation,
                      0, NULL, NULL,
                      &viceShaderProgram);
    
    if (!viceShaderProgram)
        NSLog(@"Error at glueCreateProgram");
    
    postponedReconfigure = NO;
    
    // ----- Multi Buffer -----
    machineRefreshPeriod = 0.0f;
    hostToUsFactor = (unsigned long)(CVGetHostClockFrequency() / 1000000UL);
    usToMsFactor = 1000UL;
    displayDelta = 0;
    
    // ----- Pixel Buffer -----
    pixelBuffer = nil;
    
    // ----- Texture -----
    [self initTextures];

    // ----- Pixel Aspect Ratio -----
    pixelAspectRatio = 1.0f;

    renderingLock = [[NSLock alloc] init];
    
    isOpenGLReady = YES;
}



// ----------------------------------------------------------------------------
- (BOOL)isOpaque
// ----------------------------------------------------------------------------
{
    return YES;
}


// ---------- interface -----------------------------------------------------

// called on startup and every time video param changes

// ----------------------------------------------------------------------------
- (void)reconfigureCanvas:(struct video_param_s *)param
// ----------------------------------------------------------------------------
{
    // copy params
    if(param != NULL) {
        memcpy(&video_param, param, sizeof(struct video_param_s));
    }

    // if OpenGL is not initialized yet then postpone reconfigure
    if(!isOpenGLReady) {
        postponedReconfigure = YES;
        return;
    }
    
    //NSLog(@"reconfiguring canvas [%d]", canvasId);
    BOOL usePixelBuffer = NO;

    // do sync draw
    if(video_param.sync_draw_mode > SYNC_DRAW_OFF) {
        
        handleFullFrames = video_param.sync_draw_flicker_fix;
        
        // no blending only nearest frame
        int numDrawBuffers;
        if(video_param.sync_draw_mode == SYNC_DRAW_NEAREST) {
            if(handleFullFrames)
                numDrawBuffers = 2;
            else
                numDrawBuffers = 1;
            blendingEnabled = NO;
        }
        // blending
        else {
            numDrawBuffers = video_param.sync_draw_buffers;
            if(numDrawBuffers == 0) {
                if(handleFullFrames)
                    numDrawBuffers = 8;
                else
                    numDrawBuffers = 4;
            }
            blendingEnabled = YES;
            
            // flicker fixing needs a pixel buffer
            if(handleFullFrames) {
                handleFullFrames = [self setupPixelBufferWithSize:textureSize];
                usePixelBuffer = YES;
            } 
        }

        // limit max buffers
        if(numDrawBuffers > 16)
            numDrawBuffers = 16;
        
        // allocate textures
        [self setupTextures:numDrawBuffers withSize:textureSize];
        
        // init blending
        [self initBlend];
    } 
    // no sync draw
    else {
        
        [self setupTextures:1 withSize:textureSize];
        
        blendingEnabled = NO;
        handleFullFrames = NO;
    }

    // init buffer setup
    numDrawn = 0;
    drawPos = 0;
    displayPos = blendingEnabled ? 1 : 0;
    firstDrawTime = 0;
    lastDrawTime = 0;
    lastDisplayTime = 0;
    blendAlpha = 1.0f;
    lastWasFullFrame = NO;
    
    int i;
    for(i=0;i<MAX_BUFFERS;i++) {
        texture[i].timeStamp = 0;
    }
    
    // configure GL blending
    [self toggleBlending:blendingEnabled];
    
    // disable unused pixel buffer
    if(!usePixelBuffer) {
        [self deletePixelBuffer];
    }
}

// called if the canvas size was changed by the machine (not the user!)

// ----------------------------------------------------------------------------
- (void)resizeCanvas:(CGSize)size
// ----------------------------------------------------------------------------
{
    // Resizes the view to match the canvas size
    [self reshape:size];
    
    //[self createCVTextureCache];

    // a resize might happen if the emulation video standard changes
    // so update the machine video parameters here
    float mrp = 1000.0f / (float)vsync_get_refresh_frequency();
    if(mrp != machineRefreshPeriod) {
        machineRefreshPeriod = mrp;
        //NSLog(@"machine refresh period=%g ms", machineRefreshPeriod);

        if(blendingEnabled)
            [self initBlend];
    }

    // if OpenGL is not initialized then keep size and return
    if(!isOpenGLReady) {
        textureSize = size;
        return;
    }
    
    //NSLog(@"resize canvas [%d] to %g x %g", canvasId, size.width, size.height);

    // re-adjust textures
    [self setupTextures:numTextures withSize:size];

    // re-adjust pixel buffer
    if(pixelBuffer != nil)
        [self deletePixelBuffer];
    
    [self setupPixelBufferWithSize:size];
}


// ----------------------------------------------------------------------------
- (CGSize) textureSize
// ----------------------------------------------------------------------------
{
    return textureSize;
}


// the emulation wants to draw a new frame (called from machine thread!)

// ----------------------------------------------------------------------------
- (BYTE *)beginMachineDraw:(int)frameNo
// ----------------------------------------------------------------------------
{
    unsigned long timeStamp = (unsigned long)(CVGetCurrentHostTime() / hostToUsFactor);
    
	CVPixelBufferLockBaseAddress(pixelBuffer, 0);
	
//	size_t bufferWidth = CVPixelBufferGetWidth(pixelBuffer);
//	size_t bufferHeight = CVPixelBufferGetHeight(pixelBuffer);
    
	return CVPixelBufferGetBaseAddress(pixelBuffer);
    
/*    
    // no drawing possible right now
    if(numTextures == 0) {
        NSLog(@"FATAL: no textures to draw...");
        return NULL;
    }
    
    // blend mode
    if(blendingEnabled) {

        // delta too small: frames arrive in < 1ms!
        // mainly needed on startup...
        if((timeStamp - lastDrawTime) < usToMsFactor) {
#ifdef DEBUG_SYNC
            printf("COMPENSATE: #%d\n", drawPos);
#endif
            overwriteBuffer = YES;            
        }
        // no buffer free - need to overwrite the last one
        else if(numDrawn == numTextures) {
#ifdef DEBUG_SYNC
            printf("OVERWRITE: #%d\n", drawPos);
#endif
            overwriteBuffer = YES;
        } 
        // use next free buffer
        else {
            int oldPos = drawPos;
            overwriteBuffer = NO;
            drawPos++;
            if(drawPos == numTextures)
                drawPos = 0;
            
            // copy current image as base for next buffer (VICE does partial updates)
            memcpy(texture[drawPos].buffer, texture[oldPos].buffer, textureByteSize);      
        }
    }
    
    // store time stamp
    texture[drawPos].timeStamp = timeStamp;
    texture[drawPos].frameNo   = frameNo;
    lastDrawTime = timeStamp;
    if(firstDrawTime == 0) {
        firstDrawTime = timeStamp;
    }
    
    return texture[drawPos].buffer;
*/ 
}

// the emulation did finish drawing a new frame (called from machine thread!)

// ----------------------------------------------------------------------------
- (void)endMachineDraw
// ----------------------------------------------------------------------------
{
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    

    dispatch_sync(dispatch_get_main_queue(), ^{
        if ([UIApplication sharedApplication].applicationState != UIApplicationStateBackground)
            [self displayPixelBuffer:pixelBuffer];
    });

    
    return;

/*    
    // update drawn texture
    [self updateTexture:drawPos];
    
    // blend draw
    if(blendingEnabled) {    
        // count written buffer
        if(!overwriteBuffer) {
            OSAtomicIncrement32(&numDrawn);
        }

#ifdef DEBUG_SYNC
        unsigned long ltime = 0;
        ltime = texture[drawPos].timeStamp - firstDrawTime;
        ltime /= usToMsFactor;
        unsigned long ddelta = 0;
        if(numDrawn > 1) {
            int lastPos = (drawPos + numTextures - 1) % numTextures;
            ddelta = texture[drawPos].timeStamp - texture[lastPos].timeStamp;
            ddelta /= usToMsFactor;
        }
        int frameNo = texture[drawPos].frameNo;
        printf("D %lu: +%lu @%d - draw #%d (total %d)\n", ltime, ddelta, frameNo, drawPos, numDrawn);
#endif
    }
*/    
}


// ----------------------------------------------------------------------------
- (int)getCanvasPitch
// ----------------------------------------------------------------------------
{
    return textureSize.width * 4;
}


// ----------------------------------------------------------------------------
- (int)getCanvasDepth
// ----------------------------------------------------------------------------
{
    return 32;
}


// ----------------------------------------------------------------------------
- (void)setCanvasId:(int)c
// ----------------------------------------------------------------------------
{
    canvasId = c;
}


// ----------------------------------------------------------------------------
- (int)canvasId
// ----------------------------------------------------------------------------
{
    return canvasId;
}


// ----------------------------------------------------------------------------
- (void)setPixelAspectRatio:(float)par
// ----------------------------------------------------------------------------
{
    if(par != pixelAspectRatio) {
        pixelAspectRatio = par;
        //NSLog(@"set canvas [%d] pixel aspect ratio to %g", canvasId, pixelAspectRatio);
    }
}


// ----------------------------------------------------------------------------
- (void) setIsRenderingAllowed:(BOOL)renderingAllowed
// ----------------------------------------------------------------------------
{
    [renderingLock lock];
    
    isRenderingAllowed = renderingAllowed;
    
    if (!renderingAllowed)
        glFinish();

    [renderingLock unlock];
}


// ---------- Cocoa Calls ---------------------------------------------------


// ----------------------------------------------------------------------------
- (void)toggleBlending:(BOOL)on
// ----------------------------------------------------------------------------
{
    [EAGLContext setCurrentContext:eaglContext];

    if(on)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
}


// ----------------------------------------------------------------------------
- (void) reshape:(CGSize)size
// ----------------------------------------------------------------------------
{
    UIView* superView = [self superview];
    
    [[theVICEMachine app] resizeCanvas:nil withSize:size];
}




// The texture vertices are set up such that we flip the texture vertically.
// This is so that our top left origin buffers match OpenGL's bottom left texture coordinate system.
static const GLfloat textureVertices[] =
{
    0.0f, 1.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f
};


// ----------------------------------------------------------------------------
- (void)displayPixelBuffer:(CVImageBufferRef)thePixelBuffer
// ----------------------------------------------------------------------------
{
    if (cvTextureCache == NULL)
        return;
    
    [renderingLock lock];

    if (!isRenderingAllowed)
    {
        [renderingLock unlock];
        return;
    }
    
    // Create a CVOpenGLESTexture from the CVImageBuffer
	size_t frameWidth = CVPixelBufferGetWidth(thePixelBuffer);
	size_t frameHeight = CVPixelBufferGetHeight(thePixelBuffer);

    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault, 
                                                                cvTextureCache,
                                                                thePixelBuffer,
                                                                NULL,
                                                                GL_TEXTURE_2D,
                                                                GL_RGBA,
                                                                frameWidth,
                                                                frameHeight,
                                                                GL_BGRA,
                                                                GL_UNSIGNED_BYTE,
                                                                0,
                                                                &cvTexture);
    
    
    if (!cvTexture || err) 
    {
        //NSLog(@"CVOpenGLESTextureCacheCreateTextureFromImage failed (error: %d)", err);
        [renderingLock unlock];
        return;
    }
    
	glBindTexture(CVOpenGLESTextureGetTarget(cvTexture), CVOpenGLESTextureGetName(cvTexture));
    
    // Set texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   
    
    glBindFramebuffer(GL_FRAMEBUFFER, frameBufferHandle);
    
    // Set the view port to the entire view
    glViewport(0, 0, renderBufferWidth, renderBufferHeight);
	
    static const GLfloat squareVertices[] =
    {
        -1.0f, -1.0f,
        1.0f, -1.0f,
        -1.0f,  1.0f,
        1.0f,  1.0f,
    };
    
    // Use shader program.
    glUseProgram(viceShaderProgram);
    
    // Update attribute values.
	glVertexAttribPointer(ATTRIB_VERTEX, 2, GL_FLOAT, 0, 0, squareVertices);
	glEnableVertexAttribArray(ATTRIB_VERTEX);
	glVertexAttribPointer(ATTRIB_TEXTUREPOSITON, 2, GL_FLOAT, 0, 0, textureVertices);
	glEnableVertexAttribArray(ATTRIB_TEXTUREPOSITON);
    
//    GLint location = glGetUniformLocation(viceShaderProgram, "screenSize");
//    glUniform2f(location, renderBufferWidth, renderBufferHeight);
//    CGSize viewSize = self.frame.size;
//    glUniform2f(location, viewSize.width, viewSize.height);
    
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Present
    glBindRenderbuffer(GL_RENDERBUFFER, colorBufferHandle);
    [eaglContext presentRenderbuffer:GL_RENDERBUFFER];
    
    glBindTexture(CVOpenGLESTextureGetTarget(cvTexture), 0);
    
    // Flush the CVOpenGLESTexture cache and release the texture
    CVOpenGLESTextureCacheFlush(cvTextureCache, 0);
    CFRelease(cvTexture);
    
    [renderingLock unlock];
}



// redraw view

// ----------------------------------------------------------------------------
- (void)drawRect:(CGRect)r
// ----------------------------------------------------------------------------
{

}

// ---------- Multi Buffer Blending -----------------------------------------


// ----------------------------------------------------------------------------
- (void)initBlend
// ----------------------------------------------------------------------------
{
    float dd = 1.0f;
    if(handleFullFrames) {
        dd = 4.0f;
    }
    
    dd *= machineRefreshPeriod;
    //NSLog(@"displayDelta: %g ms", dd);
    
    // the display delta for multi buffer is a machine refresh
    displayDelta = (unsigned long)((dd * (float)usToMsFactor)+0.5f);
}


// ----------------------------------------------------------------------------
- (int)calcBlend
// ----------------------------------------------------------------------------
{
    // nothing to do yet
    if((firstDrawTime==0)||(displayDelta==0)) {
        return 1;
    }
    
    // convert now render time to frame time
    unsigned long now = (unsigned long)(CVGetCurrentHostTime() / hostToUsFactor);
    unsigned long frameNow = now - displayDelta;
    
    // find display frame interval where we fit in
    int np = displayPos;
    int nd = numDrawn;    
    int i = 0;

    for(i=0;i<nd;i++) {
        // next timestamp is larger -> keep i
        if(texture[np].timeStamp > frameNow)
            break;
        
        // next slot in ring buffer
        np++;
        if(np == numTextures)
            np = 0;            
    }
    
    // display is beyond or before current frame
    BOOL beyond = NO;
    BOOL before = NO;    
    if(i==0) {
        before = YES;
    } else {
        if(i == nd) {
            beyond = YES;
            if(lastWasFullFrame && (i > 0)) {
                i--; // keep full frame
            }
        }
        i--;
    }

    // the current display frame lies between machine frames [a;b]
    // at offset [aPos;bPos] = [i;i+1] to the last displayed frame
    int aOffset = i;
    int bOffset = i+1;
    int a = (displayPos + aOffset) % numTextures;
    int b = (displayPos + bOffset) % numTextures;
    
    // retrieve full frame for flicker fixing
    // we assume "full frame = even + odd frame" here
    BOOL leftFullFrame = NO;
    BOOL rightFullFrame = NO;
    int l1,l2; // left full frame [l1;l2]
    int r1,r2; // right full frame [r1;r2]
    if(handleFullFrames && !before && !beyond) {
        int aFrameNo = texture[a].frameNo;
        int bFrameNo = texture[b].frameNo;
        int aEven = (aFrameNo & 1) == 0;
    
        // make sure a and b are consecutive frames
        // (this is the essential prerequisite for flicker fixing)
        if((aFrameNo + 1) == bFrameNo) {
            // potential begin of a right full frame
            int r1Pos;
            int r1FrameNo;
            int r1Offset;
            
            // F=[a;b] is a full frame
            if(aEven) {
                leftFullFrame = YES;
                l1 = a;
                l2 = b;
                
                // possible right full frame start
                r1        = (b + 1) % numTextures;
                r1Offset  = bOffset + 1;
                r1FrameNo = texture[r1].frameNo;
            }
            // F=[c=a-1;a] could be a full frame
            else if(i>0) {
                l1 = (displayPos + i - 1) % numTextures;
                int l1FrameNo = texture[l1].frameNo;
                if((l1FrameNo + 1) == aFrameNo) {
                    leftFullFrame = YES;
                    l2 = a;
                    i--; // keep l1!
                    
                    // possible right full frame start
                    r1        = b;
                    r1Offset  = bOffset;
                    r1FrameNo = bFrameNo;
                }
            }

            // a left full frame was found
            if(leftFullFrame == YES) {
                int r2Offset = r1Offset + 1;
                // we have enough frames for a second full frame
                if(r2Offset < nd) {
                    r2 = (r1 + 1) % numTextures;
                    int r2FrameNo = texture[r2].frameNo;
                    if((r1FrameNo + 1) == r2FrameNo) {
                        rightFullFrame = YES;
                    }
                }
            }
        }
    }

#ifdef DEBUG_SYNC
    unsigned long ltime = 0;
    if(frameNow > firstDrawTime)
        ltime = frameNow - firstDrawTime;
    unsigned long ddelta = ltime - lastDisplayTime;
    lastDisplayTime = ltime;
    ltime /= usToMsFactor;
    ddelta /= usToMsFactor;
    int oldDisplayPos = displayPos;
#endif

    // skip now unused frames... make room for drawing
    // and set new display pos
    if(i>0) {
        OSAtomicAdd32(-i, &numDrawn);
        displayPos = (displayPos + i) % numTextures;
    }

    // number of blended frames
    int count;
    
    // before first frame
    if(before) {
        count = 1;
        lastWasFullFrame = NO;
#ifdef DEBUG_SYNC
        unsigned long delta = texture[displayPos].timeStamp - frameNow; 
        delta /= usToMsFactor;
        printf("R %lu: +%lu BEFORE: #%d delta=%lu skip=%d\n", ltime, ddelta, displayPos, delta, i);
#endif
    }
    // beyond last frame
    else if(beyond) {
        // hold last full frame
        if(lastWasFullFrame) {
            count = 2;
            blendAlpha = 0.5f;
#ifdef DEBUG_SYNC
            unsigned long delta = frameNow - texture[displayPos].timeStamp;  
            delta /= usToMsFactor;
            printf("R %lu: +%lu HOLD LAST FF: #%d delta=%lu skip=%d avail=%d\n", ltime, ddelta, displayPos, delta, i, nd);
#endif            
        } 
        // show last frame
        else {
            count = 1;
#ifdef DEBUG_SYNC
            unsigned long delta = frameNow - texture[displayPos].timeStamp;  
            delta /= usToMsFactor;
            printf("R %lu: +%lu BEYOND: #%d delta=%lu skip=%d\n", ltime, ddelta, displayPos, delta, i);
#endif
        }
    } 
    // full frame (flicker fixer) handling
    else if(leftFullFrame) {
        lastWasFullFrame = YES;
        // two full frame pairs available: blend between them
        if(rightFullFrame) {
            unsigned long frameDelta = texture[r1].timeStamp - texture[l1].timeStamp;
            unsigned long dispDelta  = texture[r1].timeStamp - frameNow;
            blendAlpha = (float)dispDelta / (float)frameDelta;
#ifdef DEBUG_SYNC
            printf("R %lu: +%lu TWO FF: #%d [%d,%d=-%lu ; %d,%d=%lu] oldpos=%d skip=%d avail=%d -> alpha=%g\n", 
                   ltime, ddelta, 
                   displayPos,
                   l1, l2, (frameNow - texture[l1].timeStamp) / usToMsFactor,
                   r1, r2, (texture[r1].timeStamp - frameNow) / usToMsFactor,
                   oldDisplayPos, i, nd,
                   blendAlpha);
#endif
            
            count = 4;
        } 
        // only a single full frame available: show it
        else {
            blendAlpha = 0.5f;
            count = 2;
#ifdef DEBUG_SYNC
            unsigned long delta = frameNow - texture[l1].timeStamp;  
            delta /= usToMsFactor;
            printf("R %lu: +%lu ONE FF: #%d [%d,%d=-%lu] oldpos=%d skip=%d avail=%d\n",
                   ltime, ddelta,
                   displayPos, 
                   l1, l2, delta,
                   oldDisplayPos, i, nd);
#endif        
        }
    }
    // between two frames
    else {
        unsigned long frameDelta = texture[b].timeStamp - texture[a].timeStamp;
        unsigned long dispDelta  = texture[b].timeStamp - frameNow;
        blendAlpha = (float)dispDelta / (float)frameDelta;
        count = 2;
        lastWasFullFrame = NO;
#ifdef DEBUG_SYNC
        printf("R %lu: +%lu between: #%d [%d=-%lu ; %d=%lu] skip=%d -> alpha=%g\n", 
               ltime, ddelta, 
               displayPos,
               a, frameNow - texture[a].timeStamp,
               b, texture[b].timeStamp - frameNow,
               i,
               blendAlpha);
#endif
    }
        
    return count;
}

// ---------- Pixel Buffer --------------------------------------------------


// ----------------------------------------------------------------------------
- (BOOL)setupPixelBufferWithSize:(CGSize)size
// ----------------------------------------------------------------------------
{
    // already allocated
    if (pixelBuffer != nil)
        return YES;
    
    NSDictionary *options = [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES], kCVPixelBufferCGBitmapContextCompatibilityKey,
                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferCGImageCompatibilityKey,
                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferOpenGLCompatibilityKey, nil];
    
    CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, size.width, size.height, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef) options, &pixelBuffer);
    
    if (pixelBuffer == nil)
    {
        //NSLog(@"ERROR creating pixel buffer of size %g x %g!", size.width, size.height);
        return NO;
    }

    return YES;
}


// ----------------------------------------------------------------------------
- (void) deletePixelBuffer
// ----------------------------------------------------------------------------
{
    if(pixelBuffer != nil)
    {
        CVPixelBufferRelease(pixelBuffer);
        pixelBuffer = nil;
    }
}

// ---------- Texture Management --------------------------------------------


// ----------------------------------------------------------------------------
- (void)initTextures
// ----------------------------------------------------------------------------
{
    int i;
    
    numTextures = 0;
    for(i=0;i<MAX_BUFFERS;i++) {
        texture[i].buffer = NULL;
    }
}


// ----------------------------------------------------------------------------
- (void)deleteTexture:(int)tid
// ----------------------------------------------------------------------------
{
    lib_free(texture[tid].buffer);
    texture[tid].buffer = NULL;
    
    glDeleteTextures(1,&texture[tid].bindId);
}


// ----------------------------------------------------------------------------
- (void)deleteAllTextures
// ----------------------------------------------------------------------------
{
    [EAGLContext setCurrentContext:eaglContext];

    
    int i;
    for(i=0;i<numTextures;i++) {
        [self deleteTexture:i];
    }
    numTextures = 0;
}


// ----------------------------------------------------------------------------
- (void)setupTextures:(int)num withSize:(CGSize)size
// ----------------------------------------------------------------------------
{
    int i;
    
    //NSLog(@"setup textures: #%d %g x %g (was: #%d %g x %g)", num, size.width, size.height, numTextures, textureSize.width, textureSize.height);

    // clean up old textures
    if(numTextures > num) {
        [EAGLContext setCurrentContext:eaglContext];

        for(i=num;i<numTextures;i++) {
            [self deleteTexture:i];
        }
    }
    
    // if size differs then reallocate all otherwise only missing
    int start;
    if((size.width != textureSize.width)||(size.height != textureSize.height)) {
        start = 0;
    } else {
        start = numTextures;
    }
    
    // now adopt values
    textureSize = size;
    numTextures = num;    
    textureByteSize = size.width * size.height * 4;

    // setup texture memory
    for(i=start;i<numTextures;i++) {
        if (texture[i].buffer==NULL)
            texture[i].buffer = lib_malloc(textureByteSize);
        else
            texture[i].buffer = lib_realloc(texture[i].buffer, textureByteSize);
    
        // memory error
        if(texture[i].buffer == NULL) {
            numTextures = i;
            return;
        }
    
        // clear new texture - make sure alpha is set
        memset(texture[i].buffer, 0, textureByteSize);
    }
    
    // make GL context current
    [EAGLContext setCurrentContext:eaglContext];

/*    
    // bind textures and initialize them
    for(i=start;i<numTextures;i++) {
        glGenTextures(1, &texture[i].bindId);
        glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture[i].bindId);
        BYTE *data = texture[i].buffer;
        
        glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_EXT, textureByteSize, data);
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
                        GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);

        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_T, GL_CLAMP);    

        glTexEnvi(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_ENV_MODE, GL_DECAL);

        glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA,
                     textureSize.width, textureSize.height,
                     0, 
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 
                     data);
    }
*/
    
}


// ----------------------------------------------------------------------------
- (void)updateTexture:(int)i
// ----------------------------------------------------------------------------
{
    [EAGLContext setCurrentContext:eaglContext];
    
    /*
    glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture[i].bindId);
    glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA,
                 textureSize.width, textureSize.height,
                 0, 
                 GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 
                 texture[i].buffer);
     */
}

// ----- Keyboard -----


// Defines row, column and shift flag for the key corresponding to ASCII value
//
static int sASCIIKeyMap[3*128] =
{
    0, 0, 0, // 00
    0, 0, 0, // 01
    0, 0, 0, // 02
    0, 0, 0, // 03
    0, 0, 0, // 04
    0, 0, 0, // 05
    0, 0, 0, // 06
    0, 0, 0, // 07
    0, 0, 0, // 08
    7, 2, 0, // 09
    0, 1, 0, // 0a
    0, 0, 0, // 0b
    0, 0, 0, // 0c
    0, 1, 0, // 0d
    0, 0, 0, // 0e
    0, 0, 0, // 0f
    0, 0, 0, // 10
    0, 0, 0, // 11
    0, 0, 0, // 12
    0, 0, 0, // 13
    0, 0, 0, // 14
    0, 0, 0, // 15
    0, 0, 0, // 16
    0, 0, 0, // 17
    0, 0, 0, // 18
    0, 0, 0, // 19
    0, 0, 0, // 1a
    0, 0, 0, // 1b
    0, 0, 0, // 1c
    0, 0, 0, // 1d
    0, 0, 0, // 1e
    0, 0, 0, // 1f
    7, 4, 0, // 20 space
    7, 0, 1, // 21 !
    7, 3, 1, // 22 "
    1, 0, 1, // 23 #
    1, 3, 1, // 24 $
    2, 0, 1, // 25 %
    2, 3, 1, // 26 &
    3, 0, 1, // 27 '
    3, 3, 1, // 28 (
    4, 0, 1, // 29 )
    6, 1, 0, // 2a *
    5, 0, 0, // 2b +
    5, 7, 0, // 2c ,
    5, 3, 0, // 2d -
    5, 4, 0, // 2e .
    6, 7, 0, // 2f /
    4, 3, 0, // 30 0
    7, 0, 0, // 31 1
    7, 3, 0, // 32 2
    1, 0, 0, // 33 3
    1, 3, 0, // 34 4
    2, 0, 0, // 35 5
    2, 3, 0, // 36 6
    3, 0, 0, // 37 7
    3, 3, 0, // 38 8
    4, 0, 0, // 39 9
    5, 5, 0, // 3a :
    6, 2, 0, // 3b ;
    5, 7, 1, // 3c <
    6, 5, 0, // 3d =
    5, 4, 1, // 3e >
    6, 7, 1, // 3f ?
    5, 6, 0, // 40 @
    1, 2, 1, // 41 A
    3, 4, 1, // 42 B
    2, 4, 1, // 43 C
    2, 2, 1, // 44 D
    1, 6, 1, // 45 E
    2, 5, 1, // 46 F
    3, 2, 1, // 47 G
    3, 5, 1, // 48 H
    4, 1, 1, // 49 I
    4, 2, 1, // 4a J
    4, 5, 1, // 4b K
    5, 2, 1, // 4c L
    4, 4, 1, // 4d M
    4, 7, 1, // 4e N
    4, 6, 1, // 4f O
    5, 1, 1, // 50 P
    7, 6, 1, // 51 Q
    2, 1, 1, // 52 R
    1, 5, 1, // 53 S
    2, 6, 1, // 54 T
    3, 6, 1, // 55 U 
    3, 7, 1, // 56 V
    1, 1, 1, // 57 W
    2, 7, 1, // 58 X
    3, 1, 1, // 59 Y
    1, 4, 1, // 5a Z
    5, 5, 1, // 5b [
    0, 7, 1, // 5c \ -> cursor down + shift
    6, 2, 1, // 5d ]
    6, 6, 0, // 5e ^ -> up arrow
    7, 1, 0, // 5f _ -> left arrow
    7, 1, 0, // 60 `
    1, 2, 0, // 61 a
    3, 4, 0, // 62 b
    2, 4, 0, // 63 c
    2, 2, 0, // 64 d
    1, 6, 0, // 65 e
    2, 5, 0, // 66 f
    3, 2, 0, // 67 g
    3, 5, 0, // 68 h
    4, 1, 0, // 69 i
    4, 2, 0, // 6a j
    4, 5, 0, // 6b k
    5, 2, 0, // 6c l
    4, 4, 0, // 6d m
    4, 7, 0, // 6e n
    4, 6, 0, // 6f o
    5, 1, 0, // 70 p
    7, 6, 0, // 71 q
    2, 1, 0, // 72 r
    1, 5, 0, // 73 s
    2, 6, 0, // 74 t
    3, 6, 0, // 75 u
    3, 7, 0, // 76 v
    1, 1, 0, // 77 w
    2, 7, 0, // 78 x
    3, 1, 0, // 79 y
    1, 4, 0, // 7a z
    0, 2, 1, // 7b { -> cursor left
    0, 7, 0, // 7c | -> cursor down
    0, 2, 0, // 7d } -> cursor right
    6, 3, 0, // 7e ~ -> clr/home
    0, 0, 0  // 7f   -> del
};


// ----------------------------------------------------------------------------
- (BOOL) canBecomeFirstResponder
// ----------------------------------------------------------------------------
{
    return YES;
}


// ----------------------------------------------------------------------------
- (BOOL) hasText
// ----------------------------------------------------------------------------
{
    return YES;
}


// ----------------------------------------------------------------------------
- (void) insertText:(NSString *)text
// ----------------------------------------------------------------------------
{
    //NSLog(@"Insert text: %@", text);
    
    // Special Keys
    if ( [text hasPrefix:@"##"] ) {
        if ( [text hasPrefix:@"##F1"] )
            [theVICEMachine setKeyRow:0 column:4 shift:0];  // F1
        else if ( [text hasPrefix:@"##F2"] )
            [theVICEMachine setKeyRow:0 column:4 shift:1];  // F2
        else if ( [text hasPrefix:@"##F3"] )
            [theVICEMachine setKeyRow:0 column:5 shift:0];  // F3
        else if ( [text hasPrefix:@"##F4"] )
            [theVICEMachine setKeyRow:0 column:5 shift:1];  // F4
        else if ( [text hasPrefix:@"##F5"] )
            [theVICEMachine setKeyRow:0 column:6 shift:0];  // F5
        else if ( [text hasPrefix:@"##F6"] )
            [theVICEMachine setKeyRow:0 column:6 shift:1];  // F6
        else if ( [text hasPrefix:@"##F7"] )
            [theVICEMachine setKeyRow:0 column:3 shift:0];  // F7
        else if ( [text hasPrefix:@"##F8"] )
            [theVICEMachine setKeyRow:0 column:3 shift:1];  // F8
        else if ( [text hasPrefix:@"##RunS"] )
            [theVICEMachine setKeyRow:7 column:7 shift:0];  // RunStop
        else if ( [text hasPrefix:@"##Rest"] )
            [theVICEMachine setKeyRow:-3 column:0 shift:0];  // Restore
        else if ( [text hasPrefix:@"##Comm"] )
            [theVICEMachine setKeyRow:0 column:7 shift:1];  // Commodore
        else if ( [text hasPrefix:@"##Clr"] )
            [theVICEMachine setKeyRow:6 column:3 shift:1];  // Clr
        else if ( [text hasPrefix:@"##Up"] )
            [theVICEMachine setKeyRow:0 column:7 shift:1];  // Up
        else if ( [text hasPrefix:@"##Down"] )
            [theVICEMachine setKeyRow:0 column:7 shift:0];  // Down
        else if ( [text hasPrefix:@"##Left"] )
            [theVICEMachine setKeyRow:0 column:2 shift:1];  // Left
        else if ( [text hasPrefix:@"##Right"] )
            [theVICEMachine setKeyRow:0 column:2 shift:0];  // Right
        
        return;
    }
    
    const unsigned char* cstr = (const unsigned char*) [text cStringUsingEncoding:NSUTF8StringEncoding];
    if (cstr == NULL)
        return;
    
    unsigned char key = *cstr;
    if (key == 0xef && cstr[1] == 0x9c)
    {
        unsigned char utf8code = cstr[2];
        //printf("utf8code: 0x%02x\n", utf8code);
        switch (utf8code)
        {
            case 0x84:
                [theVICEMachine setKeyRow:0 column:4 shift:0];  // F1
                break;
            case 0x85:
                [theVICEMachine setKeyRow:0 column:4 shift:1];  // F2
                break;
            case 0x86:
                [theVICEMachine setKeyRow:0 column:5 shift:0];  // F3
                break;
            case 0x87:
                [theVICEMachine setKeyRow:0 column:5 shift:1];  // F4
                break;
            case 0x88:
                [theVICEMachine setKeyRow:0 column:6 shift:0];  // F5
                break;
            case 0x89:
                [theVICEMachine setKeyRow:0 column:6 shift:1];  // F6
                break;
            case 0x8a:
                [theVICEMachine setKeyRow:0 column:3 shift:0];  // F7
                break;
            case 0x8b:
                [theVICEMachine setKeyRow:0 column:3 shift:1];  // F8
                break;
        }
        
        return;
    }
    key = key & 0x7f;
    int row = sASCIIKeyMap[key*3];
    int col = sASCIIKeyMap[key*3+1];
    int shift = sASCIIKeyMap[key*3+2];
    
    [theVICEMachine setKeyRow:row column:col shift:shift];
}


// ----------------------------------------------------------------------------
- (void) deleteBackward
// ----------------------------------------------------------------------------
{
    [theVICEMachine setKeyRow:0 column:0 shift:0];
}


@end
