//
//  DiskCell.swift
//  C64Emulator
//
//  Created by Andy Qua on 12/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class DiskCell: UITableViewCell {

    @IBOutlet weak var name: UILabel!
    @IBOutlet weak var startBtn: UIButton!

    var diskName: String!

    var startDisk : ((_ diskName : String ) -> ())?

    override func awakeFromNib() {
        super.awakeFromNib()
        // Initialization code
    }

    override func setSelected(_ selected: Bool, animated: Bool) {
        super.setSelected(selected, animated: animated)

        // Configure the view for the selected state
    }

    @IBAction func startPressed(_ sender: AnyObject) {
        startDisk?( diskName)
    }
}
