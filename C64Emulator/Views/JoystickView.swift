//
//  JoystickImageView.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class JoystickView : UIImageView
{
    @IBOutlet var  _emulationViewController : EmulatorViewController!
    
    var _upButtonTouches = [UITouch]()
    var _downButtonTouches = [UITouch]()
    var _leftButtonTouches = [UITouch]()
    var _rightButtonTouches = [UITouch]()
    
    
    let sUpButtonRect = CGRect(x:0.0, y:0.0, width:180.0, height:64.0)
    let sDownButtonRect = CGRect(x:0.0, y:116.0, width:180.0, height:64.0)
    let sLeftButtonRect = CGRect(x:0.0, y:0.0, width:64.0, height:180.0)
    let sRightButtonRect = CGRect(x:116.0, y:0.0, width:64.0, height:180.0)
    
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches
        {
            let location = touch.location(in: self)
            if sUpButtonRect.contains(location)
            {
                sSetJoystickBit(bit:1);
                if !_upButtonTouches.contains(touch) {
                    _upButtonTouches.append(touch)
                }
            }
            if sDownButtonRect.contains(location)
            {
                sSetJoystickBit(bit:2);
                if !_downButtonTouches.contains(touch) {
                    _downButtonTouches.append(touch)
                }
            }
            if sLeftButtonRect.contains(location)
            {
                sSetJoystickBit(bit:4);
                if !_leftButtonTouches.contains(touch) {
                    _leftButtonTouches.append(touch)
                }
            }
            if sRightButtonRect.contains(location)
            {
                sSetJoystickBit(bit:8);
                if !_rightButtonTouches.contains(touch) {
                    _rightButtonTouches.append(touch)
                }
            }
        }
        
        sUpdateJoystickState(port:Int32(_emulationViewController.joystickMode - 1));
    }
    
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches
        {
            // Remove touch from whatever list it is in now
            if _upButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:1);
                _upButtonTouches.removeObject(object: touch)
            }
            if _downButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:2);
                _downButtonTouches.removeObject(object: touch)
            }
            if _leftButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:4);
                _leftButtonTouches.removeObject(object: touch)
            }
            if _rightButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:8);
                _rightButtonTouches.removeObject(object: touch)
            }
            
            // Add to new (or the same) list
            let location = touch.location(in: self)
            if sUpButtonRect.contains(location)
            {
                sSetJoystickBit(bit:1);
                if !_upButtonTouches.contains(touch) {
                    _upButtonTouches.append(touch)
                }
            }
            if sDownButtonRect.contains(location)
            {
                sSetJoystickBit(bit:2);
                if !_downButtonTouches.contains(touch) {
                    _downButtonTouches.append(touch)
                }
            }
            if sLeftButtonRect.contains(location)
            {
                sSetJoystickBit(bit:4);
                if !_leftButtonTouches.contains(touch) {
                    _leftButtonTouches.append(touch)
                }
            }
            if sRightButtonRect.contains(location)
            {
                sSetJoystickBit(bit:8);
                if !_rightButtonTouches.contains(touch) {
                    _rightButtonTouches.append(touch)
                }
            }
        }
        
        sUpdateJoystickState(port:Int32(_emulationViewController.joystickMode - 1));
    }
    
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches
        {
            // Remove touch from whatever list it is in now
            if _upButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:1);
                _upButtonTouches.removeObject(object: touch)
            }
            if _downButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:2);
                _downButtonTouches.removeObject(object: touch)
            }
            if _leftButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:4);
                _leftButtonTouches.removeObject(object: touch)
            }
            if _rightButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:8);
                _rightButtonTouches.removeObject(object: touch)
            }
        }
        
        sUpdateJoystickState(port:Int32(_emulationViewController.joystickMode - 1));
    }
}
