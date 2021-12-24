//
//  EmulatorBackgroundView.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class EmulationBackgroundView : UIView
{
    @IBOutlet var _emulationViewController : EmulatorViewController!
    var _keyboardWasVisible = false
    
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        _keyboardWasVisible = _emulationViewController._keyboardVisible;
        
        if _emulationViewController._keyboardVisible
        {
            _emulationViewController.clickKeyboardButton(self)
            return;
        }
        
        for touch in touches
        {
            if touch.tapCount == 2
            {
//                _emulationViewController.cancelControlFade()
//                _emulationViewController.shrinkOrGrowViceView()
//                _emulationViewController.scheduleControlFadeOut(delay:0.0)
                return;
            }
        }
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        if (_keyboardWasVisible) {
            return;
        }
        
        for touch in touches
        {
            if touch.tapCount == 1
            {
//                if !_emulationViewController.controlsVisible() {
//                    _emulationViewController.scheduleControlFadeIn(delay:0.5)
//                } else {
//                    _emulationViewController.scheduleControlFadeOut(delay:0.5)
//                }
            }
        }
    }
}
