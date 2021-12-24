/*
 * viceglview.m - VICEGLView
 *
 * Written by
 *  Christian Vogelgsang <chris@vogelgsang.org>
 *  Michael Klein <michael.klein@puffin.lb.shuttle.de>
 *
 *  Metal conversion by
 *    Andy Qua <andy.qua@gmail.com>
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

#import "vicemetalview.h"
#import "vicenotifications.h"
#import "vicemachine.h"

#import "MetalRenderer.h"
#import "ShaderUtilities.h"


//#define DEBUG_SYNC

@interface VICEMetalView()
// ----- local -----

- (BOOL) setupPixelBuffersWithSize:(CGSize)size;
- (void) deletePixelBuffer;
@end

@implementation VICEMetalView
{
    MetalRenderer *renderer;
}


// ----------------------------------------------------------------------------
- (void) awakeFromNib
// ----------------------------------------------------------------------------
{
    [super awakeFromNib];
    
    [self initMetal];

    renderer = [[MetalRenderer alloc] initWithMetalKitView:self];
    
    currentBufferIndex = 0;
    
    self.delegate = renderer;
    
    frameCount = 0;
    
    _inputToolbar.barTintColor = [UIColor colorWithRed:193.0f/255.0f green:197.0f/255.0f blue:205.0f/255.0f alpha:1.0f];
}



// ----------------------------------------------------------------------------
- (void) createCVTextureCache
// ----------------------------------------------------------------------------
{
    if (cvTextureCache != NULL) {
        CFRelease(cvTextureCache);
    }
    
    //  Create a new CVMetalTexture cache
    CVReturn err = CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, self.device, NULL, &cvTextureCache);
    if (err)
        NSLog(@"Error at CVOMetalTextureCacheCreate %d", err);
}


#define USE_PASSTHROUGH_SHADER

// ----------------------------------------------------------------------------
- (void) initMetal
// ----------------------------------------------------------------------------
{
    isRenderingAllowed = YES;
    
    // Use 2x scale factor on Retina displays.
    self.contentScaleFactor = [[UIScreen mainScreen] scale];
    
    // Setup Metal
    self.device = MTLCreateSystemDefaultDevice();
    
    [self createCVTextureCache];

    hostToUsFactor = (unsigned long)(CVGetHostClockFrequency() / 1000000UL);
    
    for (int i=0; i<CV_BUFFER_COUNT; i++)
        pixelBuffer[i] = nil;

    renderingLock = [[NSLock alloc] init];
    
    isMetalReady = YES;
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
    
    [renderingLock unlock];
}


// ---------- Cocoa Calls ---------------------------------------------------


// ----------------------------------------------------------------------------
- (void) reshape:(CGSize)size
// ----------------------------------------------------------------------------
{
    [[theVICEMachine app] resizeCanvas:nil withSize:size];
}


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
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer[currentBufferIndex], 0);
    
    id<MTLTexture> resultTexture = CVMetalTextureGetTexture(cvTexture[currentBufferIndex]);

    [renderer updateTexture:resultTexture];

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
    
    NSDictionary* options = [NSDictionary dictionaryWithObjectsAndKeys:[NSDictionary dictionary], kCVPixelBufferIOSurfacePropertiesKey,
                                                                       [NSNumber numberWithBool:YES], kCVPixelBufferMetalCompatibilityKey, nil];

    for (int i=0; i<CV_BUFFER_COUNT; i++)
    {
        CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, size.width, size.height, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef) options, &pixelBuffer[i]);
        
        if (pixelBuffer[i] == nil)
        {
            NSLog(@"ERROR creating pixel buffer of size %g x %g!", size.width, size.height);
            return NO;
        }
        
        // Create a CVOpenGLESTexture from the CVImageBuffer

        CVReturn err = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                 cvTextureCache,
                                                                 pixelBuffer[i],
                                                                 NULL,
                                                                 MTLPixelFormatBGRA8Unorm,
                                                                 size.width,
                                                                 size.height,
                                                                 0,
                                                                 &cvTexture[i]);
        
        if (cvTexture[i] == nil || err)
        {
            NSLog(@"CVMetalextureCacheCreateTextureFromImage failed (error: %d)", err);
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
