//
//  EmulatorViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

/*
 #import "EmulationViewController.h"
 #import "SSZipArchive.h"
 #import "SSLoadingView.h"
 #import "CSDatabaseCache.h"
 #import "ReleaseViewController.h"
 #import "UIImage+GIF.h"
 #import <MediaPlayer/MediaPlayer.h>
 #import "libx64/vice/src/arch/unix/ios/cocoa_touch/vicemachine.h"
 #import "libx64/vice/src/arch/unix/ios/cocoa_touch/viceglview.h"
 #import "libx64/vice/src/arch/unix/ios/cocoa_touch/vicenotifications.h"
 extern "C" {
 #import "libx64/vice/src/arch/unix/ios/joy.h"
 }
 */

extension Array where Element : Equatable {
    
    // Remove first collection element that is equal to the given `object`:
    mutating func removeObject(object: Element) {
        if let index = self.index(of: object) {
            self.remove(at: index)
        }
    }
}



let JOYSTICK_DISABLED = 0
let JOYSTICK_PORT1 = 1
let JOYSTICK_PORT2 = 2
let JOYSTICK_MODE_COUNT = 3


func INTERFACE_IS_PAD() -> Bool {
    return UIDevice.current.userInterfaceIdiom == .pad
}

func INTERFACE_IS_PHONE() -> Bool {
    return UIDevice.current.userInterfaceIdiom == .phone
}


var sJoystickState : UInt8 = 0;


// ----------------------------------------------------------------------------
func sSetJoystickBit( bit : UInt8)
    // ----------------------------------------------------------------------------
{
    sJoystickState |= bit;
}


// ----------------------------------------------------------------------------
func sClearJoystickBit(bit : UInt8)
    // ----------------------------------------------------------------------------
{
    sJoystickState &= ~bit;
}


// ----------------------------------------------------------------------------
func sUpdateJoystickState( port : Int32)
    // ----------------------------------------------------------------------------
{
    joy_set_bits(port, sJoystickState);
}


class EmulatorViewController: UIViewController, VICEApplicationProtocol, UIToolbarDelegate {
    
    
    @IBOutlet var _viceView : VICEGLView!
    @IBOutlet var _toolBar : UIToolbar!
    @IBOutlet var _bottomToolBar : UIToolbar!
    @IBOutlet var _portraitBottomToolBarView : UIView!
    @IBOutlet var _titleLabel : UILabel!
    @IBOutlet var _warpButton : UIButton!
    @IBOutlet var _warpButton2 : UIButton!
    @IBOutlet var _playPauseButton : UIButton!
    @IBOutlet var _playPauseButton2 : UIButton!
    @IBOutlet var _joystickView : JoystickView!
    @IBOutlet var _fireButtonView : FireButtonView!
    @IBOutlet var _joystickModeButton : UIBarButtonItem!
    @IBOutlet var _joystickModeButton2 : UIBarButtonItem!
//    @IBOutlet var _volumeView : MPVolumeView!
    @IBOutlet var _driveLedView : UIImageView!
    @IBOutlet var _emulationBackgroundView : UIView!
    
    var _arguments = [String]()
    var _canTerminate = false
    var _supportedFiles : [String]?
    var _dataFilePath = ""
    
    var _emulationRunning = false
    var _warpEnabled = false
    var _keyboardOffset : CGFloat = 0
    var _viceViewScaled = false
    
    var _controlFadeTimer : Timer!
    var _controlsVisible = false
    
    var _downloadData : NSMutableData!
    var _downloadUrlConnection : NSURLConnection!
    var _downloadFilename : NSString!
    
//    var _loadingView : SSLoadingView
    
    var _activeLedColor : UIColor!
    var _inactiveLedColor : UIColor!

    let sControlsFadeDelay : Float = 5.0
    
    var dataFileURLString = ""
    var program = ""
    var releaseId = ""
    var joystickMode : Int = JOYSTICK_DISABLED
//    @property (nonatomic) ReleaseViewController* releaseViewController;
    var _keyboardVisible = false
    
    var accessoryView : UIView?

    
    override var inputAccessoryView: UIView? {
    
        if ( accessoryView == nil ) {
            accessoryView = UIView(frame: CGRect(x: 0, y: 0, width: 320, height: 77))
            accessoryView!.backgroundColor = UIColor.red
            
            accessoryView?.layoutIfNeeded()
            
            // Add function buttons
            var x = 10
            for i in 0 ..< 8 {
                let button = UIButton(frame: CGRect(x: x, y: 5, width: 40, height: 30))
                button.setTitle("F\(i+1)", for: .normal)
                button.layer.borderColor = UIColor.white.cgColor
                button.layer.borderWidth = 1
                button.layer.cornerRadius = 5
                button.addTarget(self, action: #selector(EmulatorViewController.accessoryButtonPressed(_:)), for: .touchUpInside)
                accessoryView?.addSubview(button);
                
                x += 45
            }
            
            var buttonText = ["RunStop", "Restore", "Comm", "Clr"]
            x = 10
            for i in buttonText {
                let button = UIButton(frame: CGRect(x: x, y: 40, width: 80, height: 30))
                button.setTitle(i, for: .normal)
                button.layer.borderColor = UIColor.white.cgColor
                button.layer.borderWidth = 1
                button.layer.cornerRadius = 5
                button.addTarget(self, action: #selector(EmulatorViewController.accessoryButtonPressed(_:)), for: .touchUpInside)
                accessoryView?.addSubview(button);
                
                x += 85
            }

        }
        return accessoryView
    }
    
    func accessoryButtonPressed( _ sender: UIButton ) {
        if let button = sender.titleLabel?.text {
            print( "Button pressed : \(button)")
            
            self.viceView().insertText("##\(button)")
        }
    }

    // ----------------------------------------------------------------------------
    override func viewDidLoad()
    // ----------------------------------------------------------------------------
    {
        super.viewDidLoad()
    
        _controlFadeTimer = nil
        _controlsVisible = true
        
        _supportedFiles = nil
        
        _emulationRunning = false
        _keyboardVisible = false
        _keyboardOffset = 0.0
        _viceViewScaled = false
        
        controlsFadeIn()
        
/*
        if (_loadingView == nil)
        {
            CGSize size = self.view.frame.size;
            _loadingView = [[SSLoadingView alloc] initWithFrame:CGRectMake(0.0f, 0.0f, size.width, size.height)];
            [self.view addSubview:_loadingView];
        }
*/
        
        registerForNotifications()
        
        _titleLabel.text = self.title
        
        joystickMode = JOYSTICK_DISABLED
        _joystickView.alpha = 0.0
        _fireButtonView.alpha = 0.0
        
        _driveLedView.isHidden = true
        
        _toolBar.delegate = self
        
        setNeedsStatusBarAppearanceUpdate()
        
        NotificationCenter.default.addObserver(self, selector: #selector(EmulatorViewController.warpStatusNotification(_:)), name: "WarpStatus" as NSNotification.Name, object: nil)
        
        startDataDownload()
    }
    
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        
        self.navigationController?.isNavigationBarHidden = true

        if INTERFACE_IS_PHONE()
        {
            if UIInterfaceOrientationIsPortrait(UIApplication.shared.statusBarOrientation) {
                _bottomToolBar.isHidden = true
                _portraitBottomToolBarView.isHidden = false
            } else if UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation) {
                _bottomToolBar.isHidden = false
                _portraitBottomToolBarView.isHidden = true
            }
        } else {
            _portraitBottomToolBarView.isHidden = true
        }
    }
    
    
    // ----------------------------------------------------------------------------
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
    
        self.navigationController?.isNavigationBarHidden = false

        cancelControlFade()
    }
    
    
    // ----------------------------------------------------------------------------
    func position(for bar: UIBarPositioning) -> UIBarPosition {

        if (bar !== _toolBar as UIBarPositioning) {
            return .topAttached
        } else {
            return .any
        }
    }
    
    
    override var shouldAutorotate: Bool {
        return true
    }
    
    // ----------------------------------------------------------------------------
    func startEmulator()
    // ----------------------------------------------------------------------------
    {
        UIApplication.shared.setStatusBarStyle(.default, animated: true)
    
        self.startVICEThread(files:_supportedFiles)
    }
    
    
    // ----------------------------------------------------------------------------
    func startVICEThread( files:[String]?)
    // ----------------------------------------------------------------------------
    {
        guard let files = files else { return }
        if files.count == 0 {
            return
        }
        
        _canTerminate = false;
        
        // Just pick the first one for now, later on, we might do some better guess by looking
        // at the end of the filenames for _0 or _A or something.
        
        let rootPath = Bundle.main.resourcePath!.appending("/x64")

        _dataFilePath = files[0]
        if self.program != "" {
            _dataFilePath = _dataFilePath + ":\(self.program.lowercased())"
            _arguments = [rootPath, "-autostart", _dataFilePath]
        } else {
            _arguments = [rootPath, "-8", _dataFilePath]
        }

        let documentsPath = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
        let documentsPathCString = documentsPath.cString(using: String.Encoding.utf8)
        setenv("HOME", documentsPathCString, 1);
        
                
        let viceMachine = VICEMachine.sharedInstance()!
        viceMachine.setMediaFiles(files)
        viceMachine.setAutoStartPath(_dataFilePath)
        
        Thread.detachNewThreadSelector(#selector(VICEMachine.start(_:)), toTarget: viceMachine, with: self)
        
        _emulationRunning = true
        
        scheduleControlFadeOut( delay:0 )
    }
    
    
    // ----------------------------------------------------------------------------
    func startDataDownload()
    // ----------------------------------------------------------------------------
    {
        _supportedFiles = [dataFileURLString]
        self.startEmulator()
/*
        NSURL* url = [NSURL fileURLWithPath:_dataFileURLString];
        if (url.isFileURL)
        {
            _supportedFiles = [NSMutableArray arrayWithObject:url.path];
            [_loadingView removeFromSuperview];
            _loadingView = nil;
            
            [self startEmulator];
            return;
            
        }
        
        
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestUseProtocolCachePolicy timeoutInterval:60.0];
        _downloadData = [NSMutableData data];
        _downloadUrlConnection = [[NSURLConnection alloc] initWithRequest:request delegate:self];
*/
    }
    
/*
    // ----------------------------------------------------------------------------
    - (void) connection:(NSURLConnection*)connection didReceiveResponse:(NSURLResponse*)response
    // ----------------------------------------------------------------------------
    {
    if (connection == _downloadUrlConnection)
    {
    [_downloadData setLength:0];
    _downloadFilename = [response suggestedFilename];
    return;
    }
    }
    
    
    // ----------------------------------------------------------------------------
    - (void) connection:(NSURLConnection*)connection didReceiveData:(NSData*)data
    // ----------------------------------------------------------------------------
    {
    if (connection == _downloadUrlConnection)
    {
    [_downloadData appendData:data];
    return;
    }
    
    }
    
    
    // ----------------------------------------------------------------------------
    - (void) connection:(NSURLConnection*)connection didFailWithError:(NSError*)error
    // ----------------------------------------------------------------------------
    {
    //NSLog(@"download connection failed\n");
    
    NSArray* cachedDownloadFiles = [[CSDatabaseCache sharedInstance] readDownloadFilesForReleaseId:_releaseId];
    if (cachedDownloadFiles != nil)
    {
    NSString* releasePath = [[CSDatabaseCache sharedInstance] releasePathForReleaseId:_releaseId];
    _supportedFiles = [[NSMutableArray alloc] initWithCapacity:cachedDownloadFiles.count];
    
    BOOL all_files_exist = YES;
    
    for (NSString* cachedDownloadFile in cachedDownloadFiles)
    {
    NSString* dataFilePath = [releasePath stringByAppendingPathComponent:cachedDownloadFile];
    BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:dataFilePath isDirectory:NULL];
    if (!exists)
    {
    all_files_exist = NO;
    break;
    }
    [_supportedFiles addObject:dataFilePath];
    }
    
    [_loadingView removeFromSuperview];
    _loadingView = nil;
    
    if (all_files_exist)
    [self startEmulator];
    else
    {
    //NSLog(@"Incomplete downloads!");
    [self dismiss];
    }
    }
    else
    [[CSDatabaseCache sharedInstance] showNetworkError];
    }
    
    
    // ----------------------------------------------------------------------------
    - (void) connectionDidFinishLoading:(NSURLConnection*)connection
    // ----------------------------------------------------------------------------
    {
    if (connection == _downloadUrlConnection && [_downloadData length] > 0)
    {
    //NSLog(@"Request for download finished\n");
    
    NSString* releasePath = [[CSDatabaseCache sharedInstance] releasePathForReleaseId:_releaseId];
    NSString* downloadFilePath = [releasePath stringByAppendingPathComponent:_downloadFilename];
    
    [_downloadData writeToFile:downloadFilePath atomically:YES];
    
    if ([[_downloadFilename pathExtension] caseInsensitiveCompare:@"zip"] == NSOrderedSame)
    {
    NSString* folderName = [_downloadFilename stringByDeletingPathExtension];
    NSString* extractedFolderPath = [releasePath stringByAppendingPathComponent:folderName];
    [[NSFileManager defaultManager] createDirectoryAtPath:extractedFolderPath withIntermediateDirectories:YES attributes:nil error:NULL];
    
    BOOL success = [SSZipArchive unzipFileAtPath:downloadFilePath toDestination:extractedFolderPath];
    if (success)
    {
    _supportedFiles = [[NSMutableArray alloc] init];
    [self findAllSupportedFilesinFolder:extractedFolderPath into:_supportedFiles];
    [[NSFileManager defaultManager] removeItemAtPath:downloadFilePath error:NULL];
    }
    }
    else
    _supportedFiles = [NSMutableArray arrayWithObject:downloadFilePath];
    
    
    [_loadingView removeFromSuperview];
    _loadingView = nil;
    
    if (_supportedFiles.count > 0)
    {
    [[CSDatabaseCache sharedInstance] writeDownloadFiles:_supportedFiles forReleaseId:_releaseId];
    [self startEmulator];
    }
    
    
    }
    }
 
    
    // ----------------------------------------------------------------------------
    func findAllSupportedFilesinFolder:(NSString*)extractedFolderPath into:(NSMutableArray*)pathList
    // ----------------------------------------------------------------------------
    {
    NSArray* supportedExtensions = [NSArray arrayWithObjects:@"d64", @"t64", @"p00", @"prg", @"d71", @"d81", nil];
    
    NSArray* extractedFiles = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:extractedFolderPath error:NULL];
    extractedFiles = [extractedFiles sortedArrayUsingSelector:@selector(compare:)];
    
    for (NSString* extractedFile in extractedFiles)
    {
    NSString* extractedFilePath = [extractedFolderPath stringByAppendingPathComponent:extractedFile];
    
    BOOL is_directory = NO;
    [[NSFileManager defaultManager] fileExistsAtPath:extractedFilePath isDirectory:&is_directory];
    if (is_directory)
    [self findAllSupportedFilesinFolder:extractedFilePath into:pathList];
    
    NSString* extension = [extractedFile pathExtension];
    
    for (NSString* supportedExtension in supportedExtensions)
    {
    if ([supportedExtension caseInsensitiveCompare:extension] == NSOrderedSame)
    {
    //NSLog(@"Found supported file: %@", extractedFilePath);
    [pathList addObject:extractedFilePath];
    break;
    }
    }
    }
    }
     */
    
    
    // ----------------------------------------------------------------------------
    func isRunning() -> Bool
    // ----------------------------------------------------------------------------
    {
        return _emulationRunning;
    }
    
    
    // ----------------------------------------------------------------------------
    func dismiss()
    // ----------------------------------------------------------------------------
    {
        if (!self.isBeingDismissed) {
            DispatchQueue.main.async {
                self.dismiss(animated: true, completion: nil)
            }
        }
    }
    
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickDoneButton(_ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
/*
        if (_settingsPopOver != nil && _settingsPopOver.popoverVisible)
        {
            [_settingsPopOver dismissPopoverAnimated:YES];
            _settingsPopOver = nil;
        }
*/
        if _canTerminate || !_emulationRunning
        {
            dismiss()
            return
        }
        
        if (_emulationRunning) {
            theVICEMachine.stopMachine()
        }
    }
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickSettingsButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
/*
        if (_settingsPopOver != nil && _settingsPopOver.popoverVisible)
        {
            [_settingsPopOver dismissPopoverAnimated:YES];
            _settingsPopOver = nil;
        }
        else
        if (_emulationRunning) {
            self.performSegue(withIdentifier: "SettingsPopOver", sender: sender)
        }
 */
    }
    
    
    // ----------------------------------------------------------------------------
    override func prepare(for segue: UIStoryboardSegue, sender: AnyObject?) {
//        if segue.identifier == "SettingsPopOver" {
//            _settingsPopOver = ((UIStoryboardPopoverSegue*)segue).popoverController;
//        }
/*
        if (!INTERFACE_IS_PHONE)
        {
            if ([segue.identifier isEqualToString:@"SettingsPopOver"]) {
                _settingsPopOver = ((UIStoryboardPopoverSegue*)segue).popoverController;
            }
        }
 */
    }
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickKeyboardButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        if _viceView.isFirstResponder {
            _viceView.resignFirstResponder()
        } else {
            _viceView.becomeFirstResponder()
        }
    }
    
    
    
    let sJoystickButtonImageNames = ["joystick","joystick_1","joystick_2"]
    
    // ----------------------------------------------------------------------------
    @IBAction func clickJoystickButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        switch joystickMode {
        case JOYSTICK_DISABLED:
            joystickMode = JOYSTICK_PORT1
        case JOYSTICK_PORT1:
            joystickMode = JOYSTICK_PORT2
        case JOYSTICK_PORT2:
            joystickMode = JOYSTICK_DISABLED
        default:
            joystickMode = JOYSTICK_DISABLED
        }
        
        _joystickModeButton.image = UIImage(named:sJoystickButtonImageNames[joystickMode])
        _joystickModeButton2.image = UIImage(named:sJoystickButtonImageNames[joystickMode])
        
        if joystickMode != JOYSTICK_DISABLED
        {
            UIView.beginAnimations("FadeJoystickIn", context: nil)
            UIView.setAnimationDuration(0.1)
            UIView.setAnimationCurve(.linear)

            _joystickView.alpha = 1.0
            _fireButtonView.alpha = 1.0
            
            UIView.commitAnimations()
        }
        else
        {
            UIView.beginAnimations("FadeJoystickOut", context: nil)
            UIView.setAnimationDuration(0.1)
            UIView.setAnimationCurve(.linear)
            
            _joystickView.alpha = 0.0
            _fireButtonView.alpha = 0.0

            UIView.commitAnimations()
        }
    }
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickPlayPauseButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        theVICEMachine.togglePause()
        let image = theVICEMachine.isPaused() ? UIImage(named:"hud_play") : UIImage(named:"hud_pause")
        _playPauseButton.setImage(image, for: .normal)
        _playPauseButton2.setImage(image, for: .normal)
        scheduleControlFadeOut( delay:0 )
    }
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickRestartButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        theVICEMachine.restart(_dataFilePath)
        scheduleControlFadeOut( delay:0 )
    }
    
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickWarpButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        _warpEnabled = theVICEMachine.toggleWarpMode()
        let image = _warpEnabled ? UIImage(named:"hud_warp_highlighted") : UIImage(named:"hud_warp")
        _warpButton.setImage(image, for: .normal)
        _warpButton2.setImage(image, for: .normal)
        scheduleControlFadeOut( delay:0 )
    }
    
    
    // ----------------------------------------------------------------------------
    func warpStatusNotification( _ notification :NSNotification )
    // ----------------------------------------------------------------------------
    {
        if let warp = notification.userInfo?["warp_enabled"]?.boolValue {
            if (warp != _warpEnabled)
            {
                let image = warp ? UIImage(named:"hud_warp_highlighted") : UIImage(named:"hud_warp")
                _warpButton.setImage(image, for: .normal)
                _warpButton2.setImage(image, for: .normal)
                _warpEnabled = warp;
            }
        }
    }
    
    
    override var preferredStatusBarStyle: UIStatusBarStyle {
        return .default
    }
    

    override var prefersStatusBarHidden: Bool {
        return !_controlsVisible
    }
    
    
    // ----------------------------------------------------------------------------
    func controlsVisible() -> Bool
    // ----------------------------------------------------------------------------
    {
        return _controlsVisible;
    }
    
    
    let sVisibleAlpha : CGFloat = 1.0
    
    
    // ----------------------------------------------------------------------------
    func controlsFadeIn()
    // ----------------------------------------------------------------------------
    {
        var joystickFrame = _joystickView.frame
        var fireButtonFrame = _fireButtonView.frame
        
        var ypos : CGFloat = 0.0
        var baseToolbarPosition : CGFloat = 0.0
        
        if UIInterfaceOrientationIsPortrait(UIApplication.shared.statusBarOrientation) {
            baseToolbarPosition = INTERFACE_IS_PHONE() ? _portraitBottomToolBarView.frame.origin.y : _bottomToolBar.frame.origin.y;
        } else if UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation) {
            baseToolbarPosition = _bottomToolBar.frame.origin.y;
        }
        
        ypos = baseToolbarPosition - (INTERFACE_IS_PHONE() ? 168.0 : 180.0);
        
        joystickFrame.origin.y = ypos;
        fireButtonFrame.origin.y = ypos;
        
        
        UIView.beginAnimations("FadeIn", context: nil)
        UIView.setAnimationDuration(0.2)
        UIView.setAnimationCurve(.linear)
        
        _toolBar.alpha = sVisibleAlpha;
        _bottomToolBar.alpha = sVisibleAlpha;
        _portraitBottomToolBarView.alpha = sVisibleAlpha;
        _titleLabel.alpha = sVisibleAlpha;
        _driveLedView.alpha = sVisibleAlpha;
        
        _joystickView.frame = joystickFrame;
        _fireButtonView.frame = fireButtonFrame;
        
        UIView.commitAnimations()
        
        _controlsVisible = true
        
        scheduleControlFadeOut(delay:0)
        
        self.setNeedsStatusBarAppearanceUpdate()
    }
    
    
    // ----------------------------------------------------------------------------
    func controlsFadeOut()
    // ----------------------------------------------------------------------------
    {
        // Don't fade out if popover is visible
/*
        if (_settingsPopOver != nil && _settingsPopOver.popoverVisible)
        {
            [self scheduleControlFadeOut:0.0f];
            return;
        }
*/
        var joystickFrame = _joystickView.frame;
        var fireButtonFrame = _fireButtonView.frame;
        
        let ypos = self.view.bounds.size.height - (INTERFACE_IS_PHONE() ? 168.0 : 180.0);
        
        joystickFrame.origin.y = ypos;
        fireButtonFrame.origin.y = ypos;
        
        
        UIView.beginAnimations("FadeOut", context: nil)
        UIView.setAnimationDuration(0.2)
        UIView.setAnimationCurve(.linear)
        
        _toolBar.alpha = 0
        _bottomToolBar.alpha = 0
        _portraitBottomToolBarView.alpha = 0
        _titleLabel.alpha = 0
        _driveLedView.alpha = 0;
        
        _joystickView.frame = joystickFrame;
        _fireButtonView.frame = fireButtonFrame;
        
        UIView.commitAnimations()
        
        _controlsVisible = false
        self.setNeedsStatusBarAppearanceUpdate()
    }
    
    
    // ----------------------------------------------------------------------------
    func scheduleControlFadeIn(delay:Float)
    // ----------------------------------------------------------------------------
    {
        // Schedule fade out after 5 seconds
        if (_controlFadeTimer != nil) {
            _controlFadeTimer.invalidate()
        }
        
        _controlFadeTimer = Timer.scheduledTimer(timeInterval: (TimeInterval(delay == 0.0 ? sControlsFadeDelay : delay)), target: self, selector: #selector(controlsFadeIn), userInfo: nil, repeats: false)
    }
    
    
    // ----------------------------------------------------------------------------
    func scheduleControlFadeOut(delay:Float)
    // ----------------------------------------------------------------------------
    {
        // Schedule fade out after 5 seconds
        if (_controlFadeTimer != nil) {
            _controlFadeTimer.invalidate()
        }
        
        _controlFadeTimer = Timer.scheduledTimer(timeInterval: (TimeInterval(delay == 0.0 ? sControlsFadeDelay : delay)), target: self, selector: #selector(controlsFadeOut), userInfo: nil, repeats: false)
    }
    
    
    // ----------------------------------------------------------------------------
    func cancelControlFade()
    // ----------------------------------------------------------------------------
    {
        if (_controlFadeTimer != nil) {
            _controlFadeTimer.invalidate()
        }
    }
    
    
    override func viewWillTransition(to size: CGSize, with coordinator: UIViewControllerTransitionCoordinator) {
        super.viewWillTransition(to: size, with: coordinator)
        
        // Code here will execute before the rotation begins.
        // Equivalent to placing it in the deprecated method -[willRotateToInterfaceOrientation:duration:]
        
        if (size.width > size.height)
        {
            self.setLandscapeViceViewFrame( duration:0.1, animCurve:.linear, canvasSize:_viceView.textureSize() )
//            [self setLandscapeViceViewFrame:0.1f animCurve:UIViewAnimationCurveLinear canvasSize:_viceView.textureSize];
            if INTERFACE_IS_PHONE()
            {
                _bottomToolBar.isHidden = false
                _portraitBottomToolBarView.isHidden = true
            }
        }
        else if (size.width < size.height)
        {
            
            self.setPortraitViceViewFrame( duration:0.1, animCurve:.linear, canvasSize:_viceView.textureSize() )
//            [self setPortraitViceViewFrame:0.1f animCurve:UIViewAnimationCurveLinear canvasSize:_viceView.textureSize];
            if INTERFACE_IS_PHONE()
            {
                _bottomToolBar.isHidden = true
                _portraitBottomToolBarView.isHidden = false
            }
        }
        
        coordinator.animate(alongsideTransition: { (context) in
            // Place code here to perform animations during the rotation.
            // You can pass nil or leave this block empty if not necessary.
            }, completion: { (context ) in
                // Code here will execute after the rotation has finished.
                // Equivalent to placing it in the deprecated method -[didRotateFromInterfaceOrientation:]
        })
    }
    
    
    
    // ----------------------------------------------------------------------------
    func shrinkOrGrowViceView()
    // ----------------------------------------------------------------------------
    {
        var allowShrinkGrow = false
        if INTERFACE_IS_PHONE()
        {
            allowShrinkGrow = true
        }
        else
        {
            allowShrinkGrow = UIInterfaceOrientationIsLandscape(self.interfaceOrientation);
        }
        
        if (allowShrinkGrow && !_keyboardVisible)
        {
            _viceViewScaled = !_viceViewScaled;
            
            if (UIInterfaceOrientationIsPortrait(self.interfaceOrientation))
            {
                self.setPortraitViceViewFrame( duration:0.1, animCurve:.linear, canvasSize:_viceView.textureSize() )
            }
            else if (UIInterfaceOrientationIsLandscape(self.interfaceOrientation))
            {
                self.setLandscapeViceViewFrame( duration:0.1, animCurve:.linear, canvasSize:_viceView.textureSize() )
            }
        }
    }
    
    
    // UIDeviceOrientationUnknown                0
    // UIDeviceOrientationPortrait               1
    // UIDeviceOrientationPortraitUpsideDown     2
    // UIDeviceOrientationLandscapeLeft          3
    // UIDeviceOrientationLandscapeRight         4
    
    
    // ----------------------------------------------------------------------------
    func setPortraitViceViewFrame(duration: CGFloat, animCurve:UIViewAnimationCurve, canvasSize:CGSize )
    // ----------------------------------------------------------------------------
    {
        //NSLog(@"setPortrait with orientation: %ld", (long)self.interfaceOrientation);
        
        let superViewWidth :CGFloat = self.view.bounds.size.width;
        let superViewHeight :CGFloat = self.view.bounds.size.height;
        
        var frame = _viceView.frame;
        
        if INTERFACE_IS_PHONE()
        {
            let aspectRatio = canvasSize.width / canvasSize.height;
            let width = min(superViewWidth, superViewHeight);
            
            if width < canvasSize.width {
                frame.size.width = width;         // on iPhone 4/5 screen is too small to show full res in portrait, so no scaling
            } else {
                frame.size.width = _viceViewScaled ? max(width, canvasSize.width) : min(width, canvasSize.width); // on iPhone 6 the screen shows 384 by default and can be scaled to full width
            }
            
            frame.size.height = frame.size.width / aspectRatio;
            
            let ypos = (superViewHeight - frame.size.height) / 2.0
            let bottom = ypos + frame.size.height
            let keyboardTop :CGFloat = superViewHeight - _keyboardOffset
            
            frame.origin.x = (superViewWidth - frame.size.width) / 2.0
            
            if (keyboardTop < bottom) {
                frame.origin.y = ypos - (bottom - keyboardTop)
            } else {
                frame.origin.y = ypos
            }
        }
        else
        {
            if (_keyboardOffset > 0.0)
            {
                let ypos = (superViewHeight - frame.size.height) / 2.0
                let bottom = ypos + frame.size.height
                let keyboardTop = superViewHeight - _keyboardOffset
                
                frame.size.width = canvasSize.width * 2.0
                frame.size.height = canvasSize.height * 2.0
                frame.origin.x = (superViewWidth - frame.size.width) / 2.0
                
                if (keyboardTop < bottom) {
                    frame.origin.y = ypos - (bottom - keyboardTop);
                } else {
                    frame.origin.y = ypos;
                }
            }
            else
            {
                frame.size.width = canvasSize.width * 2.0
                frame.size.height = canvasSize.height * 2.0
                frame.origin.x = (superViewWidth - frame.size.width) / 2.0
                frame.origin.y = (superViewHeight - frame.size.height) / 2.0
            }
        }
        
        //NSLog(@"portrait superview: %f/%f, vice frame: %@", superViewWidth, superViewHeight, NSStringFromCGRect(frame));
        
        var joystickFrame = _joystickView.frame;
        var fireButtonFrame = _fireButtonView.frame;
        
        let baseToolbarPosition = INTERFACE_IS_PHONE() ? _portraitBottomToolBarView.frame.origin.y : _bottomToolBar.frame.origin.y;
        let ypos = (_controlsVisible ? baseToolbarPosition : self.view.bounds.size.height) - (INTERFACE_IS_PHONE() ? 168.0 : 180.0);
        joystickFrame.origin.y = ypos;
        fireButtonFrame.origin.y = ypos;
        
        UIView.beginAnimations("Resize", context: nil)
        UIView.setAnimationDuration(TimeInterval(duration))
        UIView.setAnimationCurve(animCurve)
        
        _viceView.frame = frame;
        _joystickView.frame = joystickFrame;
        _fireButtonView.frame = fireButtonFrame;
        
        UIView.commitAnimations()
    }
    
    
    // ----------------------------------------------------------------------------
    func setLandscapeViceViewFrame(duration: CGFloat, animCurve:UIViewAnimationCurve, canvasSize:CGSize )
    // ----------------------------------------------------------------------------
    {
        //    NSLog(@"setLandscape with orientation: %ld", (long)self.interfaceOrientation);
        
        var superViewWidth :CGFloat = self.view.bounds.size.width;
        var superViewHeight :CGFloat = self.view.bounds.size.height;
        
        var frame = _viceView.frame;
        let aspectRatio = canvasSize.width / canvasSize.height;
        
        if INTERFACE_IS_PHONE()
        {
            if _keyboardOffset > 0.0
            {
                frame.size.height = superViewHeight - _keyboardOffset;
                frame.size.width = frame.size.height * aspectRatio;
                frame.origin.x = (superViewWidth - frame.size.width) / 2.0
                frame.origin.y = 0.0
            }
            else
            {
                frame.size.height = _viceViewScaled ? min(superViewWidth, superViewHeight) : canvasSize.height;
                frame.size.width = frame.size.height * aspectRatio;
                frame.origin.x = (superViewWidth - frame.size.width) / 2.0
                frame.origin.y = (superViewHeight - frame.size.height) / 2.0
            }
        }
        else
        {
            if _keyboardOffset > 0.0
            {
                frame.size.height = 768.0 - _keyboardOffset
                frame.size.width = frame.size.height * aspectRatio
                frame.origin.x = (superViewWidth - frame.size.width) / 2.0
                frame.origin.y = 0.0
            }
            else
            {
                let scaleFactor : CGFloat = _viceViewScaled ? 1024.0/768.0 : 1.0
                
                let doubledCanvasSize = CGSize(width:canvasSize.width * 2.0, height:canvasSize.height * 2.0);
                frame.size.width = doubledCanvasSize.width * scaleFactor
                frame.size.height = doubledCanvasSize.height * scaleFactor
                frame.origin.x = (superViewWidth - frame.size.width) / 2.0
                frame.origin.y = (superViewHeight - frame.size.height) / 2.0
            }
        }
        
//        NSLog(@"landscape superview: %@, vice frame: %@", NSStringFromCGRect([self.view bounds]), NSStringFromCGRect(frame));
        
        var joystickFrame = _joystickView.frame;
        var fireButtonFrame = _fireButtonView.frame;
        
        let ypos = (_controlsVisible ? _bottomToolBar.frame.origin.y : self.view.bounds.size.height) - (INTERFACE_IS_PHONE() ? 168.0 : 180.0);
        joystickFrame.origin.y = ypos
        fireButtonFrame.origin.y = ypos
        
        UIView.beginAnimations("Resize", context: nil)
        UIView.setAnimationDuration(0.1)
        UIView.setAnimationCurve(animCurve)
        
        _viceView.frame = frame;
        _joystickView.frame = joystickFrame;
        _fireButtonView.frame = fireButtonFrame;
        
        UIView.commitAnimations()
        
        //NSLog(@"frame: %@, center: %@", NSStringFromCGRect(frame), NSStringFromCGPoint(_viceView.center));
    }
    
    
    // ----------------------------------------------------------------------------
    func registerForNotifications()
    // ----------------------------------------------------------------------------
    {
        NotificationCenter.default.addObserver(self, selector: #selector(keyboardWillBeShown(_:)), name: NSNotification.Name.UIKeyboardWillShow, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(keyboardWillBeHidden(_:)), name: NSNotification.Name.UIKeyboardWillHide, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(enableDriveStatus(_:)), name: NSNotification.Name(rawValue: VICEEnableDriveStatusNotification), object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(displayLed(_:)), name: NSNotification.Name(rawValue: VICEDisplayDriveLedNotification), object: nil)
    }
    
    
    // ----------------------------------------------------------------------------
    func keyboardWillBeShown( _ notification: NSNotification )
    // ----------------------------------------------------------------------------
    {
        _keyboardVisible = true
        let info = notification.userInfo
        var keyboard_size = CGSize()
        
        if let val = info?[UIKeyboardFrameBeginUserInfoKey]?.cgRectValue {
            keyboard_size = val.size // [[info objectForKey:UIKeyboardFrameBeginUserInfoKey] CGRectValue].size;
        }
        _keyboardOffset = keyboard_size.height;

        var duration : CGFloat = 0.0
        if let val = info?[UIKeyboardAnimationDurationUserInfoKey]?.doubleValue {
            duration = CGFloat(val)
        }
        
        var curve = UIViewAnimationCurve.linear
        if let val = info?[UIKeyboardAnimationDurationUserInfoKey]?.integerValue {
            curve = UIViewAnimationCurve(rawValue: val)!
        }
        
        if (UIInterfaceOrientationIsLandscape(self.interfaceOrientation)) {
            self.setLandscapeViceViewFrame(duration: duration, animCurve: curve, canvasSize: _viceView.textureSize())
        }
        else if (UIInterfaceOrientationIsPortrait(self.interfaceOrientation)) {
            self.setPortraitViceViewFrame(duration: duration, animCurve: curve, canvasSize: _viceView.textureSize())
        }
        
        self.controlsFadeOut()
    }
    
    
    // ----------------------------------------------------------------------------
    func keyboardWillBeHidden( _ notification : NSNotification )
    // ----------------------------------------------------------------------------
    {
        _keyboardVisible = false
        _keyboardOffset = 0.0
        
        let info = notification.userInfo
        
        var duration : CGFloat = 0.0
        if let val = info?[UIKeyboardAnimationDurationUserInfoKey]?.doubleValue {
            duration = CGFloat(val)
        }
        var curve = UIViewAnimationCurve.linear
        if let val = info?[UIKeyboardAnimationDurationUserInfoKey]?.integerValue {
            curve = UIViewAnimationCurve(rawValue: val)!
        }
        
        if (UIInterfaceOrientationIsLandscape(self.interfaceOrientation)) {
            self.setLandscapeViceViewFrame(duration: duration, animCurve: curve, canvasSize: _viceView.textureSize())
        }
        else if (UIInterfaceOrientationIsPortrait(self.interfaceOrientation)) {
            self.setPortraitViceViewFrame(duration: duration, animCurve: curve, canvasSize: _viceView.textureSize())
        }
    }
    
    
    // ----------------------------------------------------------------------------
    func titleLabel() -> UILabel
    // ----------------------------------------------------------------------------
    {
        return _titleLabel;
    }
    
    
    // ----------------------------------------------------------------------------
    func enableDriveStatus( _ notification : NSNotification )
    // ----------------------------------------------------------------------------
    {
        // setup drive views
        let info = notification.userInfo
        if let val = info?["enabled_drives"]?.int32Value {
            _driveLedView.isHidden = !(val == 1);
        }
    }
    
    
    // ----------------------------------------------------------------------------
    func displayLed( _ notification : NSNotification )
    // ----------------------------------------------------------------------------
    {
        let info = notification.userInfo
        if let drive = info?["drive"]?.int32Value {
            if drive == 0 {
                if let pwm = info?["pwm"]?.int32Value {
                    let strength : Float = Float(pwm)/Float(1000)
                    
                    let color = UIColor(red: CGFloat(0.4 + strength * 0.5), green: 0.0, blue: 0.0, alpha: 1.0)
                    _driveLedView.image = imageWithColor(color:color)
                }
            }
        }
    }
    
    func imageWithColor( color: UIColor) -> UIImage
    {
        let rect = CGRect(x: 0, y: 0, width: 1, height: 1)
        UIGraphicsBeginImageContext(rect.size);
        
        if let ctx = UIGraphicsGetCurrentContext() {
            ctx.setFillColor(color.cgColor)
            ctx.fill(rect)
        }
        
        let image = UIGraphicsGetImageFromCurrentImageContext()
        UIGraphicsEndImageContext();
        
        return image!
    }

    // ----------------------------------------------------------------------------
    func setIsInBackground( isInBackground : Bool )
    // ----------------------------------------------------------------------------
    {
        _viceView.setIsRenderingAllowed(!isInBackground)
    }
    
    
    //MARK VICE Application Protocol implementation
    // ----------------------------------------------------------------------------
    func arguments() -> [AnyObject]! {
        return _arguments
    }
    
    func viceView() -> VICEGLView! {
        return _viceView
    }
    
    
    func setMachine(_ aMachineObject: AnyObject!) {
    }
    
    
    func machineDidStop() {
        _canTerminate = true
        _emulationRunning = false
        
        self.dismiss()
    }
    
    
    func createCanvas(_ canvasPtr: Data!, with size: CGSize) {
    }
    
    func destroyCanvas(_ canvasPtr: Data!) {
    }

    
    func resizeCanvas(_ canvasPtr: Data!, with size: CGSize) {
         if INTERFACE_IS_PHONE()
         {
            if (UIInterfaceOrientationIsPortrait(self.interfaceOrientation)) {
                self.setPortraitViceViewFrame(duration: 0.1, animCurve: .linear, canvasSize: size)
            } else if (UIInterfaceOrientationIsLandscape(self.interfaceOrientation)) {
                self.setLandscapeViceViewFrame(duration: 0.1, animCurve: .linear, canvasSize: size)
            }
        }
    }
    
    func reconfigureCanvas(_ canvasPtr: Data!) {
    }
    
    func beginLineInput(withPrompt prompt: String!) {
    }
    
    func endLineInput() {
    }
    
    func postRemoteNotification(_ array: [AnyObject]!) {
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
        self.clickDoneButton(self)
        
        return 0;
    }
    
    
    func runExtendImageDialog() -> Bool {
        return false;
    }
    
    
    func getOpenFileName(_ title: String!, types: [AnyObject]!) -> String! {
        return nil;
    }
    
}


