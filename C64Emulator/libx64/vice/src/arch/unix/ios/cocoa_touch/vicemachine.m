/*
 * vicemachine.m - VICEMachine - the emulated machine main thread/class
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

#include "main.h"
#include "vsync.h"
#include "machine.h"
#include "keyboard.h"
#include "alarm.h"
#include "initcmdline.h"

#import "vicemachine.h"
#import "vicemachinecontroller.h"
#import "viceapplicationprotocol.h"

// our vice machine
VICEMachine* theVICEMachine = nil;

@implementation VICEMachine

- (void) receiveSleepNote: (NSNotification*) note
{
    // if not paused already then pause while system sleeps
    if (!isPaused) {
        isSleepPaused = YES;
        isPaused = YES;
    }
}

- (void) receiveWakeNote: (NSNotification*) note
{
    // wake up if the emulator was sleeping due to system sleep
    if (isSleepPaused) {
        isSleepPaused = NO;
        isPaused = NO;
    }
}

// initial start up of machine thread and setup of DO connection
+ (VICEMachine*) sharedInstance
{
    // create my own machine objct
    theVICEMachine = [[VICEMachine alloc] init];

    return theVICEMachine;
}


-(void)startMachine:(id<VICEApplicationProtocol>)owner
{
    // store important refs
    app = owner;
    
    NSArray* args = [owner arguments];
    
    // get command line args
    NSUInteger argc = [args count];
    char **argv = (char **)malloc(sizeof(char *) * argc);
    int i;
    for (i=0;i<argc;i++) {
        NSString *str = (NSString *)[args objectAtIndex:i];
        argv[i] = strdup([str cStringUsingEncoding:NSUTF8StringEncoding]);
    }
    
    // store global
    shallIDie = NO;
    isPaused = NO;
    isSleepPaused = NO;
    isWaitingForLineInput = NO;
    doMonitorInPause = NO;
    pausePollInterval = 0.5;

    // frame step control via key board
    escapeFromPause = NO;
    keepEscaping = NO;
    inFrameStep = NO;

    machineController = nil;
    machineNotifier = [[VICEMachineNotifier alloc] initWithOwner:owner];
    machineThread = [NSThread currentThread];
    
    resourceUpdateTimer = nil;
    
    view = [owner viceView];
    
    // register sleep and wake up notifiers
//    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(receiveSleepNote:) name: NSWorkspaceWillSleepNotification object: NULL];
//    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(receiveWakeNote:) name: NSWorkspaceDidWakeNotification object: NULL];
    
    // enter VICE main program and main loop
    main_program((int)argc,argv);
    
    // will never reach this point
}


- (NSThread*) machineThread
{
    return machineThread;
}


- (NSArray*) mediaFiles
{
    return mediaFiles;
}

- (void) setMediaFiles:(NSArray*)files
{
    mediaFiles = files;
}


- (NSUInteger) currentMediaFileIndex
{
    return currentMediaFileIndex;
}


- (void) setCurrentMediaFileIndex:(NSUInteger)index
{
    currentMediaFileIndex = index;
    
    NSString* imagePath = [mediaFiles objectAtIndex:index];
    
    if ([[imagePath pathExtension] caseInsensitiveCompare:@"d64"] == NSOrderedSame ||
        [[imagePath pathExtension] caseInsensitiveCompare:@"d71"] == NSOrderedSame ||
        [[imagePath pathExtension] caseInsensitiveCompare:@"d81"] == NSOrderedSame)
        [machineController attachDiskImage:8 path:imagePath];
    else if ([[imagePath pathExtension] caseInsensitiveCompare:@"t64"] == NSOrderedSame)
        [machineController attachTapeImage:imagePath];
}


- (NSString*) driveImageName
{
    return driveImageName;
}


- (void) setDriveImageName:(NSString*)image
{
    driveImageName = image;
}


- (void) setAutoStartPath:(NSString*)path
{
    autoStartPath = path;
}


- (NSString*) autoStartPath
{
    return autoStartPath;
}


-(id<VICEApplicationProtocol>)app
{
    return app;
}

- (void) stopMachine
{
    if (shallIDie)
        return;
    
    [machineController saveResources:nil];
    
    // flag to die after next run loop call
    shallIDie = YES;
    isPaused = NO;
}

- (BOOL) isPaused
{
    return isPaused;
}

- (BOOL) togglePause
{
    isPaused = !isPaused;
    return isPaused;
}


- (BOOL) toggleWarpMode
{
    NSNumber* currentWarpState = [machineController getIntResource:@"WarpMode"]; 
    NSNumber* warpState = [NSNumber numberWithBool:![currentWarpState boolValue]];
    [machineController setIntResource:@"WarpMode" value:warpState];
    return [warpState boolValue];
}


- (void) restart:(NSString*)datafilePath
{
    [machineController setIntResource:@"WarpMode" value:[NSNumber numberWithInt:0]];
    [machineController setIntResource:@"DriveTrueEmulation" value:[NSNumber numberWithInt:1]];
    [machineController smartAttachImage:datafilePath withProgNum:0 andRun:YES];
}


-(void)activateMonitorInPause
{
    doMonitorInPause = YES;
}

// press a key in pause mode
-(void)keyPressedInPause:(unsigned int)code
{
    const NSTimeInterval iv[] = { 0,0.05,0.1,0.2,0.4,0.8 };
    
    if(!inFrameStep) {
        switch(code) {
            case 0x31: // SPACE - single frame skip
                escapeFromPause = YES;
                keepEscaping = NO;
                inFrameStep = YES;
                pausePollInterval = 0.1;
                break;
            case 0x05:
            case 0x04:
            case 0x03: // a,s,d,f,g,h - multi frame skip
            case 0x02:
            case 0x01:
            case 0x00:
                escapeFromPause = YES;
                keepEscaping = YES;
                inFrameStep = YES;
                pausePollInterval = iv[code];
                break;
            default:
                printf("Unknown Pause key: 0x%04x\n", code);
                break;
        }
    }
}

// release a key in pause mode
-(void)keyReleasedInPause:(unsigned int)code
{
    if(inFrameStep) {
        switch(code) {
            case 0x31: // SPACE
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03: // a,s,d,f,g,h
            case 0x04:
            case 0x05:
                inFrameStep = NO;
                escapeFromPause = NO;
                break;
        }
    } 
}

-(void)triggerRunLoop
{    
    // enter a pause loop?
    if (isPaused)
    {
        // suspend speed evalution
        vsync_suspend_speed_eval();
        
        // enter pause loop -> wait for togglePause triggered from UI thread
        while (isPaused) {
            // check for events
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:pausePollInterval]];
            
            /*
            // user requested to enter monitor during pause
            if(doMonitorInPause) {
                doMonitorInPause = false;
                monitor_startup_trap();
                // leave run loop -> monitor will be entered via trap
                return;
            }
            */
             
            // shall we escape from pause?
            if(escapeFromPause) {
                if(!keepEscaping) {
                    escapeFromPause = NO;
                    pausePollInterval = 0.5;
                }
                return;
            }
        }
    }
    
    // run machine thread runloop once: 5ms
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0]];

    if (sCurrentKeyRow != -1)
    {
        sKeyDelay--;
        if (sKeyDelay == 0)
        {
            keyboard_key_released_rowcolumn(sCurrentKeyRow, sCurrentKeyColumn, sCurrentKeyShift);
            
            sCurrentKeyRow = -1;
        }
    }
    
    // the stop machine call triggered the die flag
    if (shallIDie)
    {
        // shut down VICE
        machine_shutdown();
    
        // tell ui that it the machine stopped
        [app machineDidStop];
                
        [NSThread exit];
    }    
}

#define DEFAULT_KEY_DELAY 5

static int sCurrentKeyRow = -1;
static int sCurrentKeyColumn = -1;
static int sCurrentKeyShift = -1;
static int sKeyDelay = DEFAULT_KEY_DELAY;


- (void) setKeyRow:(int)row column:(int)col shift:(int)shift
{
    if (sCurrentKeyRow != -1)
        keyboard_key_released_rowcolumn(sCurrentKeyRow, sCurrentKeyColumn, sCurrentKeyShift);

    sCurrentKeyRow = row;
    sCurrentKeyColumn = col;
    sCurrentKeyShift = shift;
    sKeyDelay = DEFAULT_KEY_DELAY;
    keyboard_key_pressed_rowcolumn(sCurrentKeyRow, sCurrentKeyColumn, sCurrentKeyShift);
}



/* 
   the machine thread needs some input (e.g. in the monitor) and waits
   until either a time out occured or the UI submitted the requested
   input line.
   
   this is done asynchronously by enabling the input in the UI thread,
   running a local run loop in the machine thread until the time out
   occured or the UI submitted the string.
*/
-(NSString *)lineInputWithPrompt:(NSString *)prompt timeout:(double)seconds
{
    isWaitingForLineInput = YES;
    submittedLineInput = nil;
    
    // notify app to begin input with prompt
    [app beginLineInputWithPrompt:(NSString *)prompt];
    
    NSDate *finishedDate;
    if (seconds == 0)
        finishedDate = [NSDate distantFuture];
    else
        finishedDate = [NSDate dateWithTimeIntervalSinceNow:seconds];
    
    // run loop until time out or line input was submitted
    while (isWaitingForLineInput && [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:finishedDate] )
    {}
    
    // end input in app
    [app endLineInput];
    
    NSString *result = submittedLineInput;
    submittedLineInput = nil;
    
    return result;
}

-(BOOL)isWaitingForLineInput
{
    return isWaitingForLineInput;
}

/* this is called by the UI thread to submit a line input */
-(void)submitLineInput:(NSString *)line
{
    if (isWaitingForLineInput) {
        submittedLineInput = line;
        isWaitingForLineInput = NO;
    }
}

// ----- Canvas Management -----

-(int)registerCanvas:(struct video_canvas_s *)canvas
{
    if (canvasNum==MAX_CANVAS) {
        //NSLog(@"FATAL: too many canvas registered!");
        return -1;
    }
    
    int canvasId = canvasNum;
    canvasNum++;
    canvasArray[canvasId] = canvas;
    return canvasId;
}

-(struct video_canvas_s *)getCanvasForId:(int)canvasId
{
    if ((canvasId<0)||(canvasId>=canvasNum))
        return NULL;
    return canvasArray[canvasId];
}

-(int)getNumCanvases
{
    return canvasNum;
}


-(VICEGLView*) view
{
    return view;
}


-(void) setResource:(VICEMachineResourceKeyValue*)keyValue
{
    if (machineController == nil)
        return;
    
    [machineController setIntResource:keyValue.resourceName value:keyValue.resourceValue];
}


- (void) setResourceOnceAfterDelay:(VICEMachineResourceKeyValue*)keyValue
{
    if (machineController == nil)
        return;

    if (resourceUpdateTimer != nil)
        return;
    
    resourceUpdateTimer = [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(setDelayedResource:) userInfo:keyValue repeats:NO];
}


- (void) setDelayedResource:(NSTimer*)timer
{
    VICEMachineResourceKeyValue* keyValue = (VICEMachineResourceKeyValue*)[timer userInfo];
    [machineController setIntResource:keyValue.resourceName value:keyValue.resourceValue];
    resourceUpdateTimer = nil;
}


-(void) selectModel:(NSNumber*)modelValue
{
    if (machineController == nil)
        return;
    
    [machineController selectModel:[modelValue intValue]];
}


// ---------- ViceMachineProtocol -------------------------------------------

// ----- Machine Controller -----

-(void)setMachineController:(VICEMachineController *)controller
{
    machineController = controller;
}

-(VICEMachineController *) machineController
{
    return machineController;
}

// ----- Machine Notifier -----

-(VICEMachineNotifier *)machineNotifier
{
    return machineNotifier;
}

@end

// ui_dispatch_events: call the run loop of the machine thread
void ui_dispatch_events()
{
    [theVICEMachine triggerRunLoop];
}

void ui_dispatch_next_event(void)
{
    [theVICEMachine triggerRunLoop];
}



@implementation VICEMachineResourceKeyValue


@synthesize resourceName = _resourceName;
@synthesize resourceValue = _resourceValue;


- (id) initWithResource:(NSString*)resource andValue:(NSNumber*)value;
{
    self = [super init];
    if (self)
    {
        _resourceName = resource;
        _resourceValue = value;
    }
    return self;
}



@end

