//
//  CodeLogViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 26/12/2021.
//

import UIKit

class CodeLogViewController: UIViewController {

    @IBOutlet weak var logView : UITextView!
    
    override func viewDidLoad() {
        super.viewDidLoad()

        logView.isEditable = false
        logView.text = ""
    }
    
    
    func setLog( logLines : [String] ) {
        let text = logLines.joined( separator: "\n")
        logView.text = text
    }
}
