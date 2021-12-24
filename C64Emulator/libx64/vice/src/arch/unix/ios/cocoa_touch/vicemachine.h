/*
 * vicemachine.h - VICEMachine - the emulatated machine main thread/class
 *
 * Written by
 *  Christian Vogelgsang <chris@vogelgsang.org>
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

#import <UIKit/UIKit.h>
#import "vicemachineprotocol.h"
#import "viceapplicationprotocol.h"
#import "vicemachinecontroller.h"
#import "vicemachinenotifier.h"

struct video_canvas_s;

@class VICEMetalView;
@class VICEMachineResourceKeyValue;


@interface VICEMachine : NSObject <VICEMachineProtocol>
{
    id<VICEApplicationProtocol > app;
    
    NSThread* machineThread;
    
    BOOL shallIDie;
    BOOL isPaused;
    BOOL isSleepPaused;
    BOOL doMonitorInPause;
    NSTimeInterval pausePollInterval;

    BOOL escapeFromPause;
    BOOL keepEscaping;
    BOOL inFrameStep;

    VICEMachineController *machineController;
    VICEMachineNotifier *machineNotifier;

    #define MAX_CANVAS 2
    int canvasNum;
    struct video_canvas_s *canvasArray[MAX_CANVAS];

    VICEMetalView* view;

    BOOL isWaitingForLineInput;
    NSString *submittedLineInput;
    
    NSArray* mediaFiles;
    NSUInteger currentMediaFileIndex;
    NSString* driveImageName;
    NSString* autoStartPath;
    
    NSTimer* resourceUpdateTimer;
}

// start the machine thread and establish connection
+ (VICEMachine*) sharedInstance;

// start the machine with established connection
- (void)startMachine:(id<VICEApplicationProtocol>)owner;
- (void) stopMachine;

- (NSThread*) machineThread;

- (NSArray*) mediaFiles;
- (void) setMediaFiles:(NSArray*)files;

- (NSUInteger) currentMediaFileIndex;
- (void) setCurrentMediaFileIndex:(NSUInteger)index;

- (NSString*) driveImageName;
- (void) setDriveImageName:(NSString*)image;

- (NSString*) autoStartPath;
- (void) setAutoStartPath:(NSString*)path;

// trigger the machine thread's run loop and terminate thread if shallIDie is set
- (void) triggerRunLoop;

- (BOOL) isPaused;
- (BOOL) togglePause;

- (BOOL) toggleWarpMode;

- (void) restart:(NSString*)datafilePath;

// activate the monitor in the pause loop
-(void)activateMonitorInPause;

// press a key in pause mode
-(void)keyPressedInPause:(unsigned int)code;

// release a key in pause mode
-(void)keyReleasedInPause:(unsigned int)code;

- (void) setKeyRow:(int)row column:(int)col shift:(int)shift;

// trigger runloop and wait for input submission from UI thread
-(NSString *)lineInputWithPrompt:(NSString *)prompt timeout:(double)seconds;

// return the application
-(id<VICEApplicationProtocol>)app;

// machine specific *ui.m sets its machine controller
-(void)setMachineController:(VICEMachineController *)controller;

-(VICEMachineController*) machineController;

// access the notifier
-(VICEMachineNotifier *)machineNotifier;

// ----- Canvas Registry -----
// register a canvas and return id
-(int)registerCanvas:(struct video_canvas_s *)canvas;
// get canvas for id
-(struct video_canvas_s *)getCanvasForId:(int)canvasId;
// get num canvases
-(int)getNumCanvases;

-(VICEMetalView*) view;

- (void) setResource:(VICEMachineResourceKeyValue*)keyValue;
- (void) setResourceOnceAfterDelay:(VICEMachineResourceKeyValue*)keyValue;

- (void) selectModel:(NSNumber*)modelValue;

@end

// the global machine thread's machine
extern VICEMachine *theVICEMachine;


@interface VICEMachineResourceKeyValue : NSObject

@property (nonatomic) NSString* resourceName;
@property (nonatomic) NSNumber* resourceValue;

- (id) initWithResource:(NSString*)resource andValue:(NSNumber*)value;

@end

