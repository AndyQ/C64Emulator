//
//  DiskItemCell.swift
//  C64Emulator
//
//  Created by Andy Qua on 12/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class DiskItemCell: UITableViewCell {

    @IBOutlet var programName : UILabel!
    @IBOutlet var validProgram : UISwitch!

    var setIsValidProgram : ((_ program : String, _ isValid : Bool) -> ())?
    override func awakeFromNib() {
        super.awakeFromNib()
        // Initialization code
    }

    override func setSelected(_ selected: Bool, animated: Bool) {
        super.setSelected(selected, animated: animated)

        // Configure the view for the selected state
    }

    @IBAction func validProgramChanged(_ sender: UISwitch) {
        guard let name = programName.text else { validProgram.isOn = false; return }
        
        setIsValidProgram?(name, sender.isOn)
    }
}
