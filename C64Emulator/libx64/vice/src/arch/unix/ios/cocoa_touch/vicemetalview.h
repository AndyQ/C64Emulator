/*
 * viceglview.h - VICEGLView
 *
 * Written by
 *  Christian Vogelgsang <chris@vogelgsang.org>
 *  Michael Klein <michael.klein@puffin.lb.shuttle.de>
 *
 *  Metal conversion by
 *  Andy Qua <andy.qua@gmail.com>
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

#include "video.h"
#include "videoparam.h"

#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>


#define CV_BUFFER_COUNT 1

@interface VICEMetalView : MTKView <UIKeyInput>
{
    GLuint                      textureName;
    CGSize                      textureSize;            /* size of canvas/texture for machine drawing */
    int                         canvasId;               /* the canvas id assigned to this view */
    
    BOOL                        isMetalReady;
    BOOL                        postponedReconfigure;
    BOOL                        isRenderingAllowed;

    unsigned long               hostToUsFactor;         /* how to convert host time to us */
    
    int                         frameCount;
    unsigned long               beginFrameTime;
    
    CVMetalTextureCacheRef      cvTextureCache;
    CVPixelBufferRef            pixelBuffer[CV_BUFFER_COUNT];
    CVMetalTextureRef           cvTexture[CV_BUFFER_COUNT];
    int                         currentBufferIndex;
	GLuint                      frameBufferHandle;
   	GLuint                      colorBufferHandle;
    int                         renderBufferWidth;
    int                         renderBufferHeight;
    GLuint                      viceShaderProgram;
    
    IBOutlet UIInputView*       _inputAccessoryView;
    IBOutlet UIToolbar*         _inputToolbar;
    
    NSLock*                     renderingLock;
}

// ----- interface -----

- (void) createCVTextureCache;

// the size of the canvas changed -> adapt textures and resources to new size
- (void) resizeCanvas:(CGSize)size;

- (CGSize) textureSize;

// get next render buffer for drawing by emu. may return NULL if out of buffers
- (BYTE*) beginMachineDraw:(int)frameNo;

// end rendering into buffer
- (void) endMachineDraw;

// report current canvas pitch in bytes
- (int) getCanvasPitch;

// report current canvas depth in bits
- (int) getCanvasDepth;

// register the id for this canvas
- (void) setCanvasId:(int)canvasId;

// return the current canvas id assigned to this view
- (int) canvasId;

- (void) setIsRenderingAllowed:(BOOL)renderingAllowed;

- (void) displayPixelBuffer;

- (IBAction) specialKeyToolbarItemClicked:(id)sender;


@end

