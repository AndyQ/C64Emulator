//
//  EmulatorViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

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
    @IBOutlet var _titleLabel : UILabel!
    @IBOutlet var _warpButton : UIButton!
    @IBOutlet var _playPauseButton : UIButton!
    @IBOutlet var _joystickView : JoystickView!
    @IBOutlet var _fireButtonView : FireButtonView!
    @IBOutlet var _joystickModeButton : UIBarButtonItem!
    @IBOutlet var _driveLedView : UIImageView!
    @IBOutlet var _emulationBackgroundView : UIView!
    
    var _arguments = [String]()
    var _canTerminate = false
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
    
    var _activeLedColor : UIColor!
    var _inactiveLedColor : UIColor!

    let sControlsFadeDelay : Float = 5.0
    
    var dataFileURLString = ""
    var program = ""
    var releaseId = ""
    var joystickMode : Int = JOYSTICK_DISABLED
    var _keyboardVisible = false
    
    var accessoryView : UIView?

    
    override var inputAccessoryView: UIView? {
    
        accessoryView = nil
        if ( accessoryView == nil ) {
            accessoryView = UIView(frame: CGRect(x: 0, y: 0, width: self.view.bounds.width, height: 77))
            accessoryView!.backgroundColor = UIColor.lightGray
            
            accessoryView?.layoutIfNeeded()
            
            let sv = UIScrollView(frame: accessoryView!.bounds)
            sv.contentSize = CGSize( width:self.view.bounds.width * 3, height:77 )
            sv.isPagingEnabled = true
            sv.contentOffset = CGPoint( x:self.view.bounds.width, y:0)
            accessoryView?.addSubview(sv)
            
            // Add function buttons
            var x : CGFloat = 10
            sv.addSubview(createAccessoryButton(pos: CGRect(x: x, y: 5, width: 80, height: 30), title: "List Dir"));
            sv.addSubview(createAccessoryButton(pos: CGRect(x: x + 85, y: 5, width: 80, height: 30), title: "Load *"));
            
            
            
            x = self.view.bounds.width + 10
            for i in 0 ..< 8 {
                sv.addSubview(createAccessoryButton(pos: CGRect(x: x, y: 5, width: 40, height: 30), title: "F\(i+1)"));
                
                x += 45
            }
            
            let buttonText = ["RunStop", "Restore", "Comm", "Clr"]
            x = self.view.bounds.width + 10
            for i in buttonText {
                sv.addSubview(createAccessoryButton(pos: CGRect(x: x, y: 40, width: 80, height: 30), title: i));
                
                x += 85
            }
            
            x = self.view.bounds.width * 2
            let center = x + (self.view.bounds.width/2)
            sv.addSubview(createAccessoryButton(pos: CGRect(x: center - 30, y: 5, width: 60, height: 30), title: "Up"))
            sv.addSubview(createAccessoryButton(pos: CGRect(x: center - 95, y: 40, width: 60, height: 30), title: "Left"))
            sv.addSubview(createAccessoryButton(pos: CGRect(x: center - 30, y: 40, width: 60, height: 30), title: "Down"))
            sv.addSubview(createAccessoryButton(pos: CGRect(x: center + 35, y: 40, width: 60, height: 30), title: "Right"))

            sv.flashScrollIndicators()
        }
        
        return accessoryView
    }
    
    func createAccessoryButton( pos : CGRect, title : String ) -> UIButton {
        let button = UIButton(frame: pos )
        button.setTitle(title, for: .normal)
        button.setTitleColor(UIColor.black, for: .highlighted)
        button.layer.borderColor = UIColor.white.cgColor
        button.layer.borderWidth = 1
        button.layer.cornerRadius = 5
        button.addTarget(self, action: #selector(EmulatorViewController.accessoryButtonPressed(_:)), for: .touchUpInside)

        return button
    }
    
    func accessoryButtonPressed( _ sender: UIButton ) {
        if let button = sender.titleLabel?.text {
            print( "Button pressed : \(button)")
            
            var textToSend = ""
            if button == "List Dir" {
                textToSend = "load \"$\",8 \n"
            } else if button == "Load *" {
                textToSend = "load \"*\",8,1\n"
            }
            
            if textToSend != "" {
                var i = 0.0
                for s in textToSend.characters {
                    DispatchQueue.main.asyncAfter(deadline: .now() + i, execute: {
                        self.viceView().insertText(String(s))
                    })
                    i += 0.05
                }
            } else {
                self.viceView().insertText("##\(button)")
            }
        }
    }

    override var prefersStatusBarHidden : Bool {
        return true
    }

    // ----------------------------------------------------------------------------
    override func viewDidLoad()
    // ----------------------------------------------------------------------------
    {
        super.viewDidLoad()
    
        _controlFadeTimer = nil
        _controlsVisible = true
        
        _emulationRunning = false
        _keyboardVisible = false
        _keyboardOffset = 0.0
        _viceViewScaled = false
        
        controlsFadeIn()
                
        registerForNotifications()
        
        _titleLabel.text = self.title
        
        joystickMode = JOYSTICK_DISABLED
        _joystickView.alpha = 0.0
        _fireButtonView.alpha = 0.0
        
        _driveLedView.isHidden = true
        
        _toolBar.delegate = self
        
        setNeedsStatusBarAppearanceUpdate()
        
        NotificationCenter.default.addObserver(self, selector: #selector(EmulatorViewController.warpStatusNotification(_:)), name: NSNotification.Name.init("WarpStatus"), object: nil )
        
        self.startVICEThread(files:[dataFileURLString])
    }
    
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        
        self.navigationController?.isNavigationBarHidden = true

        _bottomToolBar.isHidden = false
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
            _arguments = [rootPath, "-8", _dataFilePath, "-autostart", _dataFilePath]
        } else {
            _arguments = [rootPath]
        }

        let documentsPath = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true)[0]
        let documentsPathCString = documentsPath.cString(using: String.Encoding.utf8)
        setenv("HOME", documentsPathCString, 1);
        
                
        let viceMachine = VICEMachine.sharedInstance()!
        viceMachine.setMediaFiles(files)
        viceMachine.setAutoStartPath(_dataFilePath)
        
        if self.program == "" && self._dataFilePath != "" {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5 ) { [weak self] in
                guard let `self` = self else { return }
                theVICEMachine.machineController().attachDiskImage(8, path: self._dataFilePath)
            }
        }

        Thread.detachNewThreadSelector(#selector(VICEMachine.start(_:)), toTarget: viceMachine, with: self)
        
        _emulationRunning = true
        
        scheduleControlFadeOut( delay:0 )
    }
    
    
    
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
        if (_emulationRunning) {
            self.performSegue(withIdentifier: "ShowSettings", sender: sender)
        }
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
        scheduleControlFadeOut( delay:0 )
    }
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickRestartButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        theVICEMachine.machineController().resetMachine(false)
//        theVICEMachine.restart(_dataFilePath)
//        scheduleControlFadeOut( delay:0 )
    }
    
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickWarpButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        _warpEnabled = theVICEMachine.toggleWarpMode()
        let image = _warpEnabled ? UIImage(named:"hud_warp_highlighted") : UIImage(named:"hud_warp")
        _warpButton.setImage(image, for: .normal)
        scheduleControlFadeOut( delay:0 )
    }
    
    
    // ----------------------------------------------------------------------------
    func warpStatusNotification( _ notification :NSNotification )
    // ----------------------------------------------------------------------------
    {
        guard let info = notification.userInfo else { return }
        if let warp = (info["warp_enabled"] as? NSNumber)?.boolValue {
            if (warp != _warpEnabled)
            {
                let image = warp ? UIImage(named:"hud_warp_highlighted") : UIImage(named:"hud_warp")
                _warpButton.setImage(image, for: .normal)
                _warpEnabled = warp;
            }
        }
    }
    
    
    override var preferredStatusBarStyle: UIStatusBarStyle {
        return .default
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
        let baseToolbarPosition : CGFloat = _bottomToolBar.frame.origin.y
        
        ypos = baseToolbarPosition - (INTERFACE_IS_PHONE() ? 168.0 : 180.0);
        
        joystickFrame.origin.y = ypos;
        fireButtonFrame.origin.y = ypos;
        
        
        UIView.beginAnimations("FadeIn", context: nil)
        UIView.setAnimationDuration(0.2)
        UIView.setAnimationCurve(.linear)
        
        _toolBar.alpha = sVisibleAlpha;
        _bottomToolBar.alpha = sVisibleAlpha;
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
            _bottomToolBar.isHidden = false
        }
        else if (size.width < size.height)
        {
            
            self.setPortraitViceViewFrame( duration:0.1, animCurve:.linear, canvasSize:_viceView.textureSize() )
            _bottomToolBar.isHidden = false
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
            allowShrinkGrow = UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation);
        }
        
        if (allowShrinkGrow && !_keyboardVisible)
        {
            _viceViewScaled = !_viceViewScaled;
            
            
            if (UIInterfaceOrientationIsPortrait(UIApplication.shared.statusBarOrientation))
            {
                self.setPortraitViceViewFrame( duration:0.1, animCurve:.linear, canvasSize:_viceView.textureSize() )
            }
            else if (UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation))
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
        //NSLog(@"setPortrait with orientation: %ld", (long)UIApplication.shared.statusBarOrientation);
        
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
        
        let baseToolbarPosition = _bottomToolBar.frame.origin.y
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
        //    NSLog(@"setLandscape with orientation: %ld", (long)UIApplication.shared.statusBarOrientation);
        
        let superViewWidth :CGFloat = self.view.bounds.size.width;
        let superViewHeight :CGFloat = self.view.bounds.size.height;
        
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
//                frame.size.height = _viceViewScaled ? min(superViewWidth, superViewHeight) : canvasSize.height;
                frame.size.height = min(superViewWidth, superViewHeight)
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
        guard let info = notification.userInfo else { return }
        print( "Info - \(info)" )

        var keyboard_size = CGSize()
        
        let frameVal = info[UIKeyboardFrameBeginUserInfoKey] as? NSValue
        if let val = frameVal?.cgRectValue {
            keyboard_size = val.size // [[info objectForKey:UIKeyboardFrameBeginUserInfoKey] CGRectValue].size;
        }
        _keyboardOffset = keyboard_size.height;

        var duration : CGFloat = 0.0
        let durationVal = info[UIKeyboardAnimationDurationUserInfoKey] as? NSNumber
        if let val = durationVal?.doubleValue {
            duration = CGFloat(val)
        }
        
        var curve = UIViewAnimationCurve.linear
        let curveVal = info[UIKeyboardAnimationCurveUserInfoKey] as? NSNumber
        if let val = curveVal?.intValue  {
            curve = UIViewAnimationCurve(rawValue: val )!
        }
        
        if (UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation)) {
            self.setLandscapeViceViewFrame(duration: duration, animCurve: curve, canvasSize: _viceView.textureSize())
        }
        else if (UIInterfaceOrientationIsPortrait(UIApplication.shared.statusBarOrientation)) {
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
        
        guard let info = notification.userInfo else { return }
        
        var duration : CGFloat = 0.0
        let durationVal = info[UIKeyboardAnimationDurationUserInfoKey] as? NSNumber
        if let val = durationVal?.doubleValue {
            duration = CGFloat(val)
        }
        
        var curve = UIViewAnimationCurve.linear
        let curveVal = info[UIKeyboardAnimationCurveUserInfoKey] as? NSNumber
        if let val = curveVal?.intValue {
            curve = UIViewAnimationCurve(rawValue: val)!
        }
        
        if (UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation)) {
            self.setLandscapeViceViewFrame(duration: duration, animCurve: curve, canvasSize: _viceView.textureSize())
        }
        else if (UIInterfaceOrientationIsPortrait(UIApplication.shared.statusBarOrientation)) {
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
        guard let info = notification.userInfo else { return }
        if let val = (info["enabled_drives"] as? NSNumber)?.int32Value {
            _driveLedView.isHidden = !(val == 1);
        }
    }
    
    
    // ----------------------------------------------------------------------------
    func displayLed( _ notification : NSNotification )
    // ----------------------------------------------------------------------------
    {
        guard let info = notification.userInfo else { return }
        if let drive = (info["drive"] as? NSNumber)?.int32Value {
            if drive == 0 {
                if let pwm = (info["pwm"] as? NSNumber)?.int32Value {
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
    func arguments() -> [Any]! {
        return _arguments
    }
    
    func viceView() -> VICEGLView! {
        return _viceView
    }
    
    
    func setMachine(_ aMachineObject: Any!) {
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
            if (UIInterfaceOrientationIsPortrait(UIApplication.shared.statusBarOrientation)) {
                self.setPortraitViceViewFrame(duration: 0.1, animCurve: .linear, canvasSize: size)
            } else if (UIInterfaceOrientationIsLandscape(UIApplication.shared.statusBarOrientation)) {
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
        self.clickDoneButton(self)
        
        return 0;
    }
    
    
    func runExtendImageDialog() -> Bool {
        return false;
    }
    
    
    func getOpenFileName(_ title: String!, types: [Any]!) -> String! {
        return nil;
    }
    
}


