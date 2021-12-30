//
//  FileItemCell.swift
//  C64Emulator
//
//  Created by Andy Qua on 26/12/2021.
//

import UIKit

class FileItemCell: UITableViewCell {
    @IBOutlet weak var fileName : UILabel!

    override func awakeFromNib() {
        super.awakeFromNib()
        // Initialization code
    }

    override func setSelected(_ selected: Bool, animated: Bool) {
        super.setSelected(selected, animated: animated)

        // Configure the view for the selected state
    }

}
