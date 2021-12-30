//
//  EmulatorViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit
import GameController


extension Array where Element : Equatable {
    
    // Remove first collection element that is equal to the given `object`:
    mutating func removeObject(object: Element) {
        if let index = self.firstIndex(of: object) {
            self.remove(at: index)
        }
    }
}


class AccessoryView : UIView {
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if #available(iOS 11.0, *) {
            if let window = self.window {
                self.bottomAnchor.constraint(lessThanOrEqualToSystemSpacingBelow: window.safeAreaLayoutGuide.bottomAnchor, multiplier: 1).isActive = true
            }
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

func sIsSetJoystickBit(bit : UInt8) -> Bool
    // ----------------------------------------------------------------------------
{
    return sJoystickState & bit == bit
}


// ----------------------------------------------------------------------------
func sUpdateJoystickState( port : Int32)
    // ----------------------------------------------------------------------------
{
    joy_set_bits(port, sJoystickState);
}


class EmulatorViewController: UIViewController, VICEApplicationProtocol, UIToolbarDelegate, UIKeyInput {
    
    
    @IBOutlet var _viceView : VICEMetalView!
    @IBOutlet var _toolBar : UIToolbar!
    @IBOutlet var _playPauseButton : UIButton!
    @IBOutlet var _warpButton : UIButton!
    @IBOutlet var _titleLabel : UILabel!
    @IBOutlet var _joystickModeButton : UIBarButtonItem!
    @IBOutlet var _monitorButton : UIBarButtonItem!
    @IBOutlet var _driveLedView : UIImageView!

    @IBOutlet var _bottomToolBar : UIToolbar!
    @IBOutlet var _joystickView : JoystickView!
    @IBOutlet var _fireButtonView : FireButtonView!
//    @IBOutlet var _emulationBackgroundView : UIView!

    @IBOutlet var monitorView : UIView!
    var monitorViewController : MonitorViewController!

    
    let sJoystickButtonImageNames = ["joystick","joystick_1","joystick_2"]
    
    var controller : GCController!
    private var _virtualController: Any?
    @available(iOS 15.0, *)
    public var virtualController: GCVirtualController? {
        get { return self._virtualController as? GCVirtualController }
        set { self._virtualController = newValue }
    }


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
    var inMonitorMode = false

    override var inputAccessoryView: UIView? {
        guard !inMonitorMode else { return nil }

        accessoryView = nil
        if ( accessoryView == nil ) {
            accessoryView = AccessoryView(frame: CGRect(x: 0, y: 0, width: self.view.bounds.width, height: 77))
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
    
    @objc func accessoryButtonPressed( _ sender: UIButton ) {
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
                for s in textToSend {
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
        
//        controlsFadeIn()
                
        registerForNotifications()
        
        _titleLabel.text = self.title
        
        joystickMode = JOYSTICK_DISABLED
        _joystickView.alpha = 0.0
        _fireButtonView.alpha = 0.0
        
        _driveLedView.isHidden = true
        
        _toolBar.delegate = self
                
        setNeedsStatusBarAppearanceUpdate()
        
        lookForGameControllers()

        self.monitorView.frame = CGRect( x: view.bounds.width-800, y:view.bounds.height-600, width: 800, height: 465)
        let gr = UIPanGestureRecognizer(target: self, action: #selector(viewDragged(_:)))
        self.monitorView.addGestureRecognizer(gr)
        
        NotificationCenter.default.addObserver(forName: NSNotification.Name("GameTogglePauseNotification"), object: nil, queue: OperationQueue.main, using: { [weak self] (notification) in
            guard let `self` = self else { return }
            self.clickPlayPauseButton(self)
        })
        
        self.startVICEThread(files:[dataFileURLString])
    }
    
    var lastPos : CGPoint = .zero
    var resizing = false
    @IBOutlet weak var monitorLeading : NSLayoutConstraint!
    @IBOutlet weak var monitorBottom : NSLayoutConstraint!
    @objc func viewDragged( _ gr : UIPanGestureRecognizer ) {
        let p = gr.location(in: self.view)
        if gr.state == .began {
            let f = monitorView.frame
            resizing =  p.x > f.maxX - 50 && p.y > f.maxY - 50
        } else {
            let dx = p.x - lastPos.x
            let dy = p.y - lastPos.y

            var f = monitorView.frame
            if resizing {
                // Resize
                f.size.width += dx
                f.size.height += dy
                
                if f.size.width < 400 {
                    f.size.width = 400
                }
                if f.size.height < 200 {
                    f.size.height = 200
                }
            } else {
                // Move
                f.origin.x += dx
                f.origin.y += dy
            }
            monitorView.frame = f
        }
        lastPos = p
        
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        
        self.navigationController?.isNavigationBarHidden = true

        //_bottomToolBar.isHidden = false
        
        if GCController.controllers().count == 1 {
//            self.toggleHardwareController(useHardware: true)
        }
    }
    
    // ----------------------------------------------------------------------------
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
    
        if (_emulationRunning) {
            theVICEMachine.stopMachine()
        }

        self.navigationController?.isNavigationBarHidden = false
    }
    
    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {
        if let vc = segue.destination as? MonitorViewController {
            self.monitorViewController = vc
        }
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
        if files[0].lowercased().hasSuffix("prg") {
            program = (files[0] as NSString).lastPathComponent
        }

        _dataFilePath = files[0]
        if self.program != "" {
            //_dataFilePath = _dataFilePath + ":\(self.program.lowercased())"
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
        
        DispatchQueue.main.asyncAfter(deadline: .now()+0.25, execute: {
            viceMachine.toggleWarpMode()
        })
        DispatchQueue.main.asyncAfter(deadline: .now()+0.5, execute: {
            viceMachine.toggleWarpMode()
        })

        
        if self.program == "" && self._dataFilePath != "" {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5 ) { [weak self] in
                guard let `self` = self else { return }
                theVICEMachine.machineController().attachDiskImage(8, path: self._dataFilePath)
            }
        } else {
//            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5 ) { [weak self] in
//                guard let `self` = self else { return }
//                theVICEMachine.machineController().smartAttachImage(self._dataFilePath, withProgNum: 0, andRun: true)
//            }

        }

        Thread.detachNewThreadSelector(#selector(VICEMachine.start(_:)), toTarget: viceMachine, with: self)
        
        _emulationRunning = true
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
        DispatchQueue.main.async {
            if (!self.isBeingDismissed) {
                self.navigationController?.popViewController(animated: true)
                //self.dismiss(animated: true, completion: nil)
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
    
    
    
    @IBAction func clickKeyboardButton( _ sender: AnyObject) {
        if self.isFirstResponder {
            self.resignFirstResponder()
        } else {
            self.becomeFirstResponder()
        }
    }
    
    @IBAction func clickMonitorButton( _ sender: AnyObject) {
        if monitorView.isHidden {
            monitorView.isHidden = false
            monitorViewController.startMonitor()
            _monitorButton.tintColor = .red
        } else {
            monitorViewController.stopMonitor()
            monitorView.isHidden = true
            _monitorButton.tintColor = .darkGray
        }

    }
 
    var firstView = true
    override var canBecomeFirstResponder: Bool {
        if firstView {
            firstView = false
            return false
        }
        return true
    }

    func insertText(_ text: String) {
        _viceView.insertText(text)
    }

    var hasText: Bool { return true }
    
    func deleteBackward() {
        _viceView.deleteBackward()

    }

    

    
    // MARK: On screen joystick handling
    

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
        
        if self.controller == nil {
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
    }
    
    
    // ----------------------------------------------------------------------------
    @IBAction func clickPlayPauseButton( _ sender: AnyObject)
    // ----------------------------------------------------------------------------
    {
        theVICEMachine.togglePause()
        let image = theVICEMachine.isPaused() ? UIImage(named:"hud_play") : UIImage(named:"hud_pause")
        _playPauseButton.setImage(image, for: .normal)
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
    }
    
    
    // ----------------------------------------------------------------------------
    @objc func warpStatusNotification( _ notification :NSNotification )
    // ----------------------------------------------------------------------------
    {
        guard let info = notification.userInfo else { return }
        if let warp = (info["warp_enabled"] as? NSNumber)?.boolValue {
            if (warp != _warpEnabled)
            {
                DispatchQueue.main.async { [unowned self] in
                    let image = warp ? UIImage(named:"hud_warp_highlighted") : UIImage(named:"hud_warp")
                    self._warpButton.setImage(image, for: .normal)
                    self._warpEnabled = warp;
                }
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
    
    
    // ----------------------------------------------------------------------------
    func registerForNotifications()
    // ----------------------------------------------------------------------------
    {
        NotificationCenter.default.addObserver(self, selector: #selector(enableDriveStatus(_:)), name: NSNotification.Name(rawValue: VICEEnableDriveStatusNotification), object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(displayLed(_:)), name: NSNotification.Name(rawValue: VICEDisplayDriveLedNotification), object: nil)
    }
    

    // ----------------------------------------------------------------------------
    func titleLabel() -> UILabel
    // ----------------------------------------------------------------------------
    {
        return _titleLabel;
    }
    
    
    // ----------------------------------------------------------------------------
    @objc func enableDriveStatus( _ notification : NSNotification )
    // ----------------------------------------------------------------------------
    {
        // setup drive views
        guard let info = notification.userInfo else { return }
        if let val = (info["enabled_drives"] as? NSNumber)?.int32Value {
            _driveLedView.isHidden = !(val == 1);
        }
    }
    
    
    // ----------------------------------------------------------------------------
    @objc func displayLed( _ notification : NSNotification )
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
    
    func viceView() -> VICEMetalView! {
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
    }
    
    func reconfigureCanvas(_ canvasPtr: Data!) {
    }
    
//    func beginLineInput(withPrompt prompt: String!) {
//    }
    
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


// MARK: Game Controller support
extension EmulatorViewController {
    func lookForGameControllers() {
        
        NotificationCenter.default.addObserver(self, selector: #selector(connectControllers), name: NSNotification.Name.GCControllerDidConnect, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(disconnectControllers), name: NSNotification.Name.GCControllerDidDisconnect, object: nil)
        
/*
        let virtualConfiguration = GCVirtualController.Configuration()
        virtualConfiguration.elements = [GCInputDirectionalDpad]
        virtualController = GCVirtualController(configuration: virtualConfiguration)
        
        // Connect to the virtual controller if no physical controllers are available.
        if GCController.controllers().isEmpty {
            print( "Virtual controller connected")
            virtualController?.connect()
        }
*/
        guard let controller = GCController.controllers().first else {
            return
        }
        
//        controller.showVirtualController()

//        registerGameController(controller)
    }
    
    @objc func connectControllers() {
        //Unpause the Game if it is currently paused
        if theVICEMachine.isPaused() {
            theVICEMachine.togglePause()
        }
        
        //Used to register the Nimbus Controllers to a specific Player Number
        var indexNumber = 0
        // Run through each controller currently connected to the system
        for controller in GCController.controllers() {
            //Check to see whether it is an extended Game Controller (Such as a Nimbus)
            if controller.extendedGamepad != nil {
                controller.playerIndex = GCControllerPlayerIndex.init(rawValue: indexNumber)!
                indexNumber += 1
                setupControllerControls(controller: controller)
            }
        }
    }
    
    @objc func disconnectControllers() {
        // Pause the Game if a controller is disconnected ~ This is mandated by Apple
        if !theVICEMachine.isPaused() {
            theVICEMachine.togglePause()
        }
    }
    
    func setupControllerControls(controller: GCController) {
        //Function that check the controller when anything is moved or pressed on it
        controller.extendedGamepad?.valueChangedHandler = {
            (gamepad: GCExtendedGamepad, element: GCControllerElement) in
            // Add movement in here for sprites of the controllers
            self.controllerInputDetected(gamepad: gamepad, element: element, index: controller.playerIndex.rawValue)
        }
    }
    
    func controllerInputDetected(gamepad: GCExtendedGamepad, element: GCControllerElement, index: Int) {
        if (gamepad.leftThumbstick == element)
        {
            if (gamepad.leftThumbstick.xAxis.value != 0)
            {
                print("Controller: \(index), LeftThumbstickXAxis: \(gamepad.leftThumbstick.xAxis)")
            }
            else if (gamepad.leftThumbstick.xAxis.value == 0)
            {
                // YOU CAN PUT CODE HERE TO STOP YOUR PLAYER FROM MOVING
            }
        }
        // Right Thumbstick
        if (gamepad.rightThumbstick == element)
        {
            if (gamepad.rightThumbstick.xAxis.value != 0)
            {
                print("Controller: \(index), rightThumbstickXAxis: \(gamepad.rightThumbstick.xAxis)")
            }
        }
        // D-Pad
        else if (gamepad.dpad == element)
        {
            if (gamepad.dpad.xAxis.value != 0)
            {
                print("Controller: \(index), D-PadXAxis: \(gamepad.rightThumbstick.xAxis)")
            }
            else if (gamepad.dpad.xAxis.value == 0)
            {
                // YOU CAN PUT CODE HERE TO STOP YOUR PLAYER FROM MOVING
            }
        }
        // A-Button
        else if (gamepad.buttonA == element)
        {
            if (gamepad.buttonA.value != 0)
            {
                print("Controller: \(index), A-Button Pressed!")
            }
        }
        // B-Button
        else if (gamepad.buttonB == element)
        {
            if (gamepad.buttonB.value != 0)
            {
                print("Controller: \(index), B-Button Pressed!")
            }
        }
        else if (gamepad.buttonY == element)
        {
            if (gamepad.buttonY.value != 0)
            {
                print("Controller: \(index), Y-Button Pressed!")
            }
        }
        else if (gamepad.buttonX == element)
        {
            if (gamepad.buttonX.value != 0)
            {
                print("Controller: \(index), X-Button Pressed!")
            }
        }
    }
    
    
    // MARK: GameController handling
    func toggleHardwareController( useHardware: Bool ) {
        print( "Found joystick - \(useHardware)")
        
        var useHardware = false
        if useHardware {
            _joystickView.alpha = 0.0
            _fireButtonView.alpha = 0.0
            
            self.controller = GCController.controllers()[0]
            self.controller.playerIndex = .index1
            if self.controller.gamepad != nil {
                
                self.controller.gamepad?.rightShoulder.valueChangedHandler = { [weak self] (button, value, pressed) in
                    guard let `self` = self else { return }
                    if pressed == false {
                        // Toggle joystick
                        if self.self.joystickMode == JOYSTICK_DISABLED {
                            self.joystickMode = JOYSTICK_PORT1
                        } else if self.joystickMode == JOYSTICK_PORT1 {
                            self.joystickMode = JOYSTICK_PORT2
                        } else if self.joystickMode == JOYSTICK_PORT2 {
                            self.joystickMode = JOYSTICK_DISABLED
                        }
                        self._joystickModeButton.image = UIImage(named:self.sJoystickButtonImageNames[self.joystickMode])
                    }
                }
                
                self.controller.gamepad?.buttonA.valueChangedHandler = { [weak self] (button, value, pressed) in
                    guard let `self` = self else { return }
                    if pressed {
                        sSetJoystickBit(bit:16)
                    } else {
                        sClearJoystickBit(bit:16)
                        
                    }
                    sUpdateJoystickState(port:Int32(self.joystickMode - 1));
                }
                
                self.controller.gamepad?.dpad.valueChangedHandler = { [weak self] (dpad, xAxis, yAxis) in
                    guard let `self` = self else { return }
                    
                    // Handle Up
                    if sIsSetJoystickBit(bit: 1) && yAxis <= 0 {
                        sClearJoystickBit(bit: 1)
                    } else if yAxis > 0.1 {
                        sSetJoystickBit(bit: 1)
                    }
                    // Handle Down
                    if sIsSetJoystickBit(bit: 2) && yAxis >= 0 {
                        sClearJoystickBit(bit: 2)
                    } else if yAxis < -0.1 {
                        sSetJoystickBit(bit: 2)
                    }
                    // Handle Left
                    if sIsSetJoystickBit(bit: 4) && xAxis >= 0 {
                        sClearJoystickBit(bit: 4)
                    } else if xAxis < -0.1 {
                        sSetJoystickBit(bit: 4)
                    }
                    // Handle Right
                    if sIsSetJoystickBit(bit: 8) && xAxis <= 0 {
                        sClearJoystickBit(bit: 8)
                    } else if xAxis > 0.1 {
                        sSetJoystickBit(bit: 8)
                    }
                    sUpdateJoystickState(port:Int32(self.joystickMode - 1));
                }
                //Add controller pause handler here
            }
        } else {
            //7
            _joystickView.alpha = 1
            _fireButtonView.alpha = 1
            self.controller = nil;
        }
    }

}

// MARK: Monitor callbacks
extension EmulatorViewController {
    @objc func openMonitor() {
        print( "Open monitor called" )
        inMonitorMode = true
    }
    
    // machine thread leaves monitor mode:
    @objc func closeMonitor() {
        print( "Close monitor called" )
        inMonitorMode = false
    }
    
    // machine thread suspends monitor UI inputs (e.g. before a single step)
    @objc func suspendMonitor() {
        print( "Suspend monitor called" )
//        inMonitor = NO;
//        [self postMonitorStateNotification:VICEMonitorStateSuspend];
    }
    
    // machine thread resumes monitor UI inputs (e.g. after a single step)
    @objc func resumeMonitor() {
        print( "Resume monitor called" )
//        inMonitor = YES;
//        [self postMonitorStateNotification:VICEMonitorStateResume];
    }
    
    // some monitor values have changed. update views (e.g. mem window)
    @objc func updateMonitor() {
        print( "Update monitor called" )
//        [self postMonitorUpdateNotification];
    }
    
    // print something in the monitor view
    @objc func printMonitorMessage( _ msg:String) {
        print( "Print monitor message called with [\(msg)]" )
        DispatchQueue.main.async {
            self.monitorViewController.setOutput(msg)
        }
    }
    
    // maching thread wants some input:
    @objc func beginLineInputWithPrompt( _ prompt:String) {
        print( "beginLineInputWithPrompt called with [\(prompt)]" )
        DispatchQueue.main.async {
            self.monitorViewController.showPrompt(prompt)
        }
    }
}
