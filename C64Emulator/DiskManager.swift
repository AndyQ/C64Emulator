//
//  DiskManager.swift
//  C64Emulator
//
//  Created by Andy Qua on 08/01/2017.
//  Copyright © 2017 Andy Qua. All rights reserved.
//

import UIKit

class DiskManager {
    class func renameDisk( from: String, to: String ) throws {
        
        // Rename file on disk
        let fm = FileManager.default
        let path = getUserGamesDirectory()
        try fm.moveItem(atPath: path.appendingPathComponent(from), toPath: path.appendingPathComponent(to))

        // Update database
        DatabaseManager.sharedInstance.renameDisk( from: from, to: to)
    }
}
