//
//  FireButtonView.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class FireButtonView : UIImageView
{
    @IBOutlet var  _emulationViewController : EmulatorViewController!
    
    var _fireButtonTouches = [UITouch]()
    
    
    let sFireButtonRect = CGRect(x:25.0, y:25.0, width:110.0, height:110.0)
    
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        
        var has_handled_touch = false
        
        for touch in touches
        {
            let location = touch.location(in: self)
            if sFireButtonRect.contains(location)
            {
                sSetJoystickBit(bit:16);
                if !_fireButtonTouches.contains(touch)
                {
                    _fireButtonTouches.append(touch)
                }
                has_handled_touch = true
                break
            }
        }
        
        sUpdateJoystickState(port:Int32(_emulationViewController.joystickMode - 1));
        
        if !has_handled_touch {
            super.touchesBegan(touches, with: event)
        }
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        var has_handled_touch = false
        
        for touch in touches
        {
            if _fireButtonTouches.contains(touch)
            {
                sClearJoystickBit(bit:16);
                _fireButtonTouches.removeObject(object: touch)
                has_handled_touch = true
                break
            }
        }
        
        sUpdateJoystickState(port:Int32(_emulationViewController.joystickMode - 1));
        
        if !has_handled_touch {
            super.touchesEnded(touches, with: event)
        }
    }
}
