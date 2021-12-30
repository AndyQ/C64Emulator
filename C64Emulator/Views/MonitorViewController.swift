//
//  MonitorViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 28/12/2021.
//

import UIKit
extension UITextView {
    func scrollToBottom() {
        let textCount: Int = text.count
        guard textCount >= 1 else { return }
        scrollRangeToVisible(NSRange(location: textCount - 1, length: 1))
    }
}


class MonitorViewController: UIViewController {
    @IBOutlet weak var txtOutput : UITextView!
    @IBOutlet weak var txtCommand : UITextField!


    override func viewDidLoad() {
        super.viewDidLoad()
        
        txtOutput.font = UIFont.monospacedSystemFont(ofSize: 16, weight: .regular)
        txtOutput.text = ""
        txtOutput.isEditable = false
    }
    
    
    @IBAction func activateMonitorPressed( _ sender : Any ) {
        theVICEMachine.machineController().activateMonitor()
        txtOutput.text = ""
    }

    @IBAction func sendCommandPressed( _ sender : Any ) {
        guard let line = txtCommand.text else { return }
        theVICEMachine.submitLineInput(line)
        setOutput( "\(line)\n" )
        
        (self.parent as!EmulatorViewController).viceView().becomeFirstResponder()
    }
    
    func startMonitor() {
        theVICEMachine.machineController().activateMonitor()
        txtCommand.becomeFirstResponder()
    }
    
    func stopMonitor() {
        theVICEMachine.submitLineInput("x")
        txtCommand.resignFirstResponder()
    }
    
    func showPrompt( _ prompt : String ) {
        if txtOutput.text != "" {
            setOutput( "\n" )
        }
        setOutput( "\(prompt)" )
        txtCommand.becomeFirstResponder()
    }
    
    func setOutput( _ text: String ) {
        txtOutput.text += text
        txtOutput.scrollToBottom()
    }
    
}

extension MonitorViewController : UITextFieldDelegate {
    func textFieldDidBeginEditing(_ textField: UITextField) {
        txtCommand.selectAll(nil)
    }
    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        sendCommandPressed( textField )
        return false
    }
}
