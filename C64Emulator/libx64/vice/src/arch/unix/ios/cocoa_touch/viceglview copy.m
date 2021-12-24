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
    currentBufferIndex = 0;
    
    [self initOpenGL];
    
    frameCount = 0;
    
    _inputToolbar.barTintColor = [UIColor colorWithRed:193.0f/255.0f green:197.0f/255.0f blue:205.0f/255.0f alpha:1.0f];
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
    glGenRenderbuffers(1, &colorBufferHandle);

    glBindFramebuffer(GL_FRAMEBUFFER, frameBufferHandle);
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
    
    EAGLSharegroup* shareGroup = [[EAGLSharegroup alloc] init];
    eaglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2 sharegroup:shareGroup];
    eaglContext.multiThreaded = NO;
    eaglContext.debugLabel = @"VICEGLView";
    if (!eaglContext || ![EAGLContext setCurrentContext:eaglContext])
    {
        NSLog(@"Problem with OpenGL context.");
        return;
    }
    
    glDisable(GL_DEPTH_TEST);
    
    [self createCVTextureCache];

    // Load vertex and fragment shaders

#ifdef USE_PASSTHROUGH_SHADER    
    const GLchar* vertSrc = "attribute vec4 position; attribute mediump vec4 textureCoordinate; varying highp vec2 coordinate; void main() { gl_Position = position; coordinate = textureCoordinate.xy; }";
    const GLchar* fragSrc = "varying mediump vec2 coordinate; uniform sampler2D videoframe; void main() { gl_FragColor = texture2D(videoframe, coordinate); }";
#else    
    NSString* vertShaderPath = [[NSBundle mainBundle] pathForResource:@"crt_shader.vsh" ofType: nil];
    const GLchar* vertSrc = (GLchar *)[[NSString stringWithContentsOfFile:vertShaderPath encoding:NSUTF8StringEncoding error:nil] UTF8String];
    
    NSString* fragShaderPath = [[NSBundle mainBundle] pathForResource:@"crt_shader.fsh" ofType: nil];
    const GLchar* fragSrc = (GLchar *)[[NSString stringWithContentsOfFile:fragShaderPath encoding:NSUTF8StringEncoding error:nil] UTF8String];
#endif
    glueCreateProgram(vertSrc, fragSrc, NUM_ATTRIBUTES, (const GLchar **)&sAttribName[0], sAttribLocation, 0, NULL, NULL, &viceShaderProgram);
    
    if (!viceShaderProgram)
        NSLog(@"Error at glueCreateProgram");
    
    // Use shader program.
    glUseProgram(viceShaderProgram);

    glGenTextures(1, &textureName);

    hostToUsFactor = (unsigned long)(CVGetHostClockFrequency() / 1000000UL);
    
    for (int i=0; i<CV_BUFFER_COUNT; i++)
        pixelBuffer[i] = nil;

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


// called if the canvas size was changed by the machine (not the user!)
// ----------------------------------------------------------------------------
- (void) resizeCanvas:(CGSize)size
// ----------------------------------------------------------------------------
{
    // Resizes the view to match the canvas size
    [self reshape:size];
    
    textureSize = size;
    
    //NSLog(@"resize canvas [%d] to %g x %g", canvasId, size.width, size.height);

    // re-adjust pixel buffer
    if(pixelBuffer[0] != nil)
        [self deletePixelBuffer];
    
    [self setupPixelBuffersWithSize:size];
}


// ----------------------------------------------------------------------------
- (CGSize) textureSize
// ----------------------------------------------------------------------------
{
    return textureSize;
}


// the emulation wants to draw a new frame (called from machine thread!)

// ----------------------------------------------------------------------------
- (BYTE*) beginMachineDraw:(int)frameNo
// ----------------------------------------------------------------------------
{
    [EAGLContext setCurrentContext:eaglContext];

    frameCount = frameNo;
    
    beginFrameTime = (unsigned long)(CVGetCurrentHostTime() / hostToUsFactor);
    
	CVPixelBufferLockBaseAddress(pixelBuffer[currentBufferIndex], 0);

    return CVPixelBufferGetBaseAddress(pixelBuffer[currentBufferIndex]);
}


// the emulation did finish drawing a new frame (called from machine thread!)

// ----------------------------------------------------------------------------
- (void) endMachineDraw
// ----------------------------------------------------------------------------
{
//    BYTE* target = CVPixelBufferGetBaseAddress(pixelBuffer);
//    int size = 384*272*4/2;
//    int r = random() % 0xff;
//    for (int i=0; i<size; i++)
//        target[i] = r;
    
//    for (int i=0; i<frameCount; i++)
//    {
//        target[i*4 + 0] = 255;
//        target[i*4 + 1] = 255;
//        target[i*4 + 2] = 255;
//        target[i*4 + 3] = 255;
//    }
    
//    frameCount++;
    
//    if (frameCount < 1)
//        return;
    
//    dispatch_async(dispatch_get_main_queue(), ^{
//        if ([UIApplication sharedApplication].applicationState != UIApplicationStateBackground)
//            [self displayPixelBuffer];
//    });

    [self displayPixelBuffer];
}


// ----------------------------------------------------------------------------
- (int) getCanvasPitch
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
- (void) reshape:(CGSize)size
// ----------------------------------------------------------------------------
{
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


static const GLfloat squareVertices[] =
{
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f,  1.0f,
    1.0f,  1.0f,
};


// ----------------------------------------------------------------------------
- (void) displayPixelBuffer
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
    
//    unsigned long endFrameTime = (unsigned long)(CVGetCurrentHostTime() / hostToUsFactor);
//    unsigned long frameDuration = endFrameTime - beginFrameTime;

    CVPixelBufferUnlockBaseAddress(pixelBuffer[currentBufferIndex], 0);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Set the view port to the entire view
    glViewport(0, 0, renderBufferWidth, renderBufferHeight);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(CVOpenGLESTextureGetTarget(cvTexture[currentBufferIndex]), CVOpenGLESTextureGetName(cvTexture[currentBufferIndex]));
    
    currentBufferIndex = (currentBufferIndex + 1) % CV_BUFFER_COUNT;

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

    // Flush the CVOpenGLESTexture cache and release the texture
    //CVOpenGLESTextureCacheFlush(cvTextureCache, 0);
    
    unsigned long startTime = (unsigned long)(CVGetCurrentHostTime() / hostToUsFactor);

    glFlush();

    // Present
    BOOL success = [eaglContext presentRenderbuffer:GL_RENDERBUFFER];
    if (!success)
    {
        NSLog(@"presentRenderbuffer failed\n");
        [renderingLock unlock];
        return;
    }

    unsigned long endTime = (unsigned long)(CVGetCurrentHostTime() / hostToUsFactor);
    unsigned long duration = endTime - startTime;

    if (duration > 0 && duration < 2000)
    {
        //NSLog(@"present duration %ld", duration);
        //[NSThread sleepForTimeInterval:0.002];
    }

    //if (frameDuration > 0 && frameDuration < 2000)
//    if (frameDuration > 1000)
//    {
//        //NSTimeInterval paddingTime = ((NSTimeInterval)(2000 - frameDuration))/1000000.0;
//        NSTimeInterval paddingTime = 0.002;
//        [NSThread sleepForTimeInterval:paddingTime];
//        //NSLog(@"frame duration %ld, sleeping for %fs", frameDuration, paddingTime);
//    }

    [renderingLock unlock];
}

// ---------- Pixel Buffer --------------------------------------------------


// ----------------------------------------------------------------------------
- (BOOL) setupPixelBuffersWithSize:(CGSize)size
// ----------------------------------------------------------------------------
{
    // already allocated
    if (pixelBuffer[0] != nil)
        return YES;
    
//    NSDictionary *options = [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES], kCVPixelBufferCGBitmapContextCompatibilityKey,
//                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferCGImageCompatibilityKey,
//                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferOpenGLESTextureCacheCompatibilityKey,
//                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferOpenGLCompatibilityKey, nil];

    NSDictionary* options = [NSDictionary dictionaryWithObjectsAndKeys:[NSDictionary dictionary], kCVPixelBufferIOSurfacePropertiesKey,
                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferOpenGLESTextureCacheCompatibilityKey, nil];

    for (int i=0; i<CV_BUFFER_COUNT; i++)
    {
        CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, size.width, size.height, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef) options, &pixelBuffer[i]);
        
        if (pixelBuffer[i] == nil)
        {
            NSLog(@"ERROR creating pixel buffer of size %g x %g!", size.width, size.height);
            return NO;
        }
        
        // Create a CVOpenGLESTexture from the CVImageBuffer
        CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                    cvTextureCache,
                                                                    pixelBuffer[i],
                                                                    NULL,
                                                                    GL_TEXTURE_2D,
                                                                    GL_RGBA,
                                                                    size.width,
                                                                    size.height,
                                                                    GL_BGRA,
                                                                    GL_UNSIGNED_BYTE,
                                                                    0,
                                                                    &cvTexture[i]);
        
        
        if (cvTexture[i] == nil || err)
        {
            NSLog(@"CVOpenGLESTextureCacheCreateTextureFromImage failed (error: %d)", err);
            return NO;
        }
    }

    return YES;
}


// ----------------------------------------------------------------------------
- (void) deletePixelBuffer
// ----------------------------------------------------------------------------
{
    for (int i=0; i<CV_BUFFER_COUNT; i++)
    {
        if (pixelBuffer[i] != nil)
        {
            CVPixelBufferRelease(pixelBuffer[i]);
            pixelBuffer[i] = nil;
        }
        
        if (cvTexture[i] != nil)
            CFRelease(cvTexture[i]);
    }
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
- (UITextAutocorrectionType) autocorrectionType
// ----------------------------------------------------------------------------
{
    return UITextAutocorrectionTypeNo;
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
    const unsigned char* cstr = (const unsigned char*) [text cStringUsingEncoding:NSUTF8StringEncoding];
    if (cstr == NULL)
        return;
    
    unsigned char key = *cstr;
    
    if (key == 0xe2 && cstr[1] == 0x80 && (cstr[2] == 0x98 || cstr[2] == 0x99))
        key = 0x27;
    if (key == 0xe2 && cstr[1] == 0x80 && (cstr[2] == 0x9c || cstr[2] == 0x9d))
        key = 0x22;

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
    
    if (key == 194)
    {
        [theVICEMachine setKeyRow:6 column:0 shift:0];  // Pound
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


// ----------------------------------------------------------------------------
- (UIView *)inputAccessoryView
// ----------------------------------------------------------------------------
{
    UIScrollView* scrollView = [_inputAccessoryView.subviews objectAtIndex:0];
    UIView* containedView = [scrollView.subviews objectAtIndex:0];
    scrollView.contentSize = containedView.frame.size;
    
    return _inputAccessoryView;
}

    
// ----------------------------------------------------------------------------
- (IBAction) specialKeyToolbarItemClicked:(id)sender
// ----------------------------------------------------------------------------
{
    NSInteger tag = [sender tag];
    
    switch (tag)
    {
        case 1:  [theVICEMachine setKeyRow:7 column:5 shift:0]; break; // C=
        case 2:  [theVICEMachine setKeyRow:7 column:2 shift:0]; break; // CTRL
        case 3:  [theVICEMachine setKeyRow:6 column:3 shift:1]; break; // CLR
        case 4:  [theVICEMachine setKeyRow:6 column:3 shift:0]; break; // HOME
        case 5:  [theVICEMachine setKeyRow:0 column:0 shift:1]; break; // INST
        case 6:  [theVICEMachine setKeyRow:0 column:2 shift:1]; break; // LEFT
        case 7:  [theVICEMachine setKeyRow:0 column:2 shift:0]; break; // RIGHT
        case 8:  [theVICEMachine setKeyRow:0 column:7 shift:1]; break; // UP
        case 9:  [theVICEMachine setKeyRow:0 column:7 shift:0]; break; // DOWN
        case 10: [theVICEMachine setKeyRow:0 column:4 shift:0]; break; // F1
        case 11: [theVICEMachine setKeyRow:0 column:4 shift:1]; break; // F2
        case 12: [theVICEMachine setKeyRow:0 column:5 shift:0]; break; // F3
        case 13: [theVICEMachine setKeyRow:0 column:5 shift:1]; break; // F4
        case 14: [theVICEMachine setKeyRow:0 column:6 shift:0]; break; // F5
        case 15: [theVICEMachine setKeyRow:0 column:6 shift:1]; break; // F6
        case 16: [theVICEMachine setKeyRow:0 column:3 shift:0]; break; // F7
        case 17: [theVICEMachine setKeyRow:0 column:3 shift:1]; break; // F8
        case 20: [theVICEMachine setKeyRow:7 column:1 shift:0]; break; // LEFT ARROW
        case 21: [theVICEMachine setKeyRow:6 column:6 shift:0]; break; // UP ARROW
        case 22: [theVICEMachine setKeyRow:6 column:6 shift:1]; break; // PI
        case 23: [theVICEMachine setKeyRow:7 column:7 shift:1]; break; // RUN
        case 24: [theVICEMachine setKeyRow:7 column:7 shift:0]; break; // STOP
        case 25: [theVICEMachine setKeyRow:-3 column:0 shift:0]; break; // RESTORE
    }
}
    
    
    
@end
