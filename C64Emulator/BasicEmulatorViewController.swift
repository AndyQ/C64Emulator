//
//  BasicEmulatorViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class BasicEmulatorViewController: UIViewController, VICEApplicationProtocol {

    @IBOutlet var viceEmuView: VICEGLView!
    
    var diskFile = ""
    var program = ""
    var viceArguments = [String]()

    override func viewDidLoad() {
        super.viewDidLoad()

        startEmulator()
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    func startEmulator()
        // ----------------------------------------------------------------------------
    {
//        [[UIApplication sharedApplication] setStatusBarStyle:UIStatusBarStyleDefault animated:YES];
        
        let path = "\(diskFile):\(program.lowercased())"
//        if let file = Bundle.main.path(forResource: "games/D0001B", ofType: "D64") {
            startVICEThread(file: path )
//        }
    }
    
    
    // ----------------------------------------------------------------------------
    func startVICEThread( file: String )
        // ----------------------------------------------------------------------------
    {
//        _canTerminate = NO;
        
        // Just pick the first one for now, later on, we might do some better guess by looking
        // at the end of the filenames for _0 or _A or something.
//        dataFilePath = file
        
        let documentsPath = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
        let documentsPathCString = documentsPath.cString(using: String.Encoding.utf8)
        setenv("HOME", documentsPathCString, 1);
        
        let rootPath = Bundle.main.resourcePath!.appending("/x64")
    
        viceArguments = [rootPath, "-autostart", file]
        //_arguments = [NSArray arrayWithObjects:rootPath, nil];
        
        let viceMachine = VICEMachine.sharedInstance()!
        viceMachine.setMediaFiles([file])
        viceMachine.setAutoStartPath(file)
        
        Thread.detachNewThreadSelector(#selector(VICEMachine.start(_:)), toTarget: viceMachine, with: self)
        
//        _emulationRunning = YES;
        
//        [self scheduleControlFadeOut:0.0f];
    }
    
    
    func dismiss()
    {
/*
        if (![self isBeingDismissed]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self dismissViewControllerAnimated:YES completion:^{
                    }];
                });
        }
        
        if (_releaseViewController != nil)
        _releaseViewController.isPresentingEmulationView = NO;
        [ReleaseViewController attemptRotationToDeviceOrientation];
*/
    }
    
    
    @IBAction func clickDoneButton( sender:AnyObject!)
    // ----------------------------------------------------------------------------
    {
/*
        if (_settingsPopOver != nil && _settingsPopOver.popoverVisible)
        {
            [_settingsPopOver dismissPopoverAnimated:YES];
            _settingsPopOver = nil;
        }
        
        if (_canTerminate || !_emulationRunning)
        {
            [self dismiss];
            return;
        }
        
        if (_emulationRunning) {
            [theVICEMachine stopMachine];
        }
*/
    }
    


    // MARK: VICE Application Protocol implementation
    
    
    // ----------------------------------------------------------------------------
    func arguments() -> [Any]! {
        return viceArguments
    }

    func viceView() -> VICEGLView! {
        return viceEmuView
    }
    

    func setMachine(_ aMachineObject: Any!) {
    }
    

    func machineDidStop() {
//        _canTerminate = YES;
//        _emulationRunning = NO;
    
        self.dismiss()
    }
    
    
    func createCanvas(_ canvasPtr: Data!, with size: CGSize) {
    }

    func destroyCanvas(_ canvasPtr: Data!) {
    }

    func resizeCanvas(_ canvasPtr: Data!, with size: CGSize) {
/*
        if (INTERFACE_IS_PHONE)
        {
            if (UIInterfaceOrientationIsPortrait(self.interfaceOrientation)) {
                setP
                [self setPortraitViceViewFrame:0.1f animCurve:UIViewAnimationCurveLinear canvasSize:size];
            } else if (UIInterfaceOrientationIsLandscape(self.interfaceOrientation)) {
                [self setLandscapeViceViewFrame:0.1f animCurve:UIViewAnimationCurveLinear canvasSize:size];
            }
        }
*/
    }
    
    func reconfigureCanvas(_ canvasPtr: Data!) {
    }

    func beginLineInput(withPrompt prompt: String!) {
    }

    func endLineInput() {
    }
    
    func postRemoteNotification(_ array: [Any]!) {
        let notificationName = array[0] as! String
        let userInfo = array[1] as! [NSObject:AnyObject]
        
        // post notification in our UI thread's default notification center
        NotificationCenter.default.post(name: NSNotification.Name(notificationName), object: self, userInfo: userInfo)
    }
    
    
    func runErrorMessage(_ message: String!) {
        //NSLog(@"ERROR: %@", message);
    }
    
    
    func runWarningMessage(_ message: String!) {
        //NSLog(@"WARNING: %@", message);
    }
    
    
    func runCPUJamDialog(_ message: String!) -> Int32 {
        self.clickDoneButton(sender: nil)
    
        return 0;
    }
    
    
    func runExtendImageDialog() -> Bool {
        return false;
    }
    
    
    func getOpenFileName(_ title: String!, types: [Any]!) -> String! {
        return nil;
    }
    

}

