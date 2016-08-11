//
//  DatabaseManager.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import Foundation

class Disk {
    var name = ""
    var filename = ""
    var files = [Game]()
    var section = ""

}

class Game {
    var name = ""
    var diskName = ""
}

class DatabaseManager: NSObject {

    static let sharedInstance : DatabaseManager = DatabaseManager()
    
    var disks = [String:Disk]()
    var games = [Game]()
    
    func getListOfGames()  {
        
        guard let file = Bundle.main.path(forResource: "games", ofType: "db") else { return }
        
        // open database
        
        var db: OpaquePointer? = nil
        if sqlite3_open(file, &db) != SQLITE_OK {
            print("error opening database")
        }
        
        var statement: OpaquePointer? = nil
        if sqlite3_prepare_v2(db, "select name, section, disk, side from games where section = 'old' order by disk, side", -1, &statement, nil) != SQLITE_OK {
            let errmsg = String(cString: sqlite3_errmsg(db))
            print("error preparing select: \(errmsg)")
        }
        
        while sqlite3_step(statement) == SQLITE_ROW {
            
            if let name = sqlite3_column_text(statement, 0),
                let section = sqlite3_column_text(statement, 1),
                let disk = sqlite3_column_text(statement, 2),
                let side = sqlite3_column_text(statement, 3) {
                
                let nameStr = String(cString: UnsafePointer<Int8>(name))
                let sectionStr = String(cString: UnsafePointer<Int8>(section))
                let diskStr = String(cString: UnsafePointer<Int8>(disk))
                let sideStr = String(cString: UnsafePointer<Int8>(side))

                let diskName = "\(diskStr)\(sideStr)"

                let game = Game()
                game.name = nameStr
                game.diskName = diskName

                
                // find disk
                if let disk = disks[diskName] {
                    disk.files.append( game )
                } else {
                    let disk = Disk()
                    disk.name = diskName
                    disk.section = sectionStr
                    disk.filename = ""
                    disk.files.append(game)
                    
                    disks[diskName] = disk
                }
                
                games.append(game)
                
            } else {
                print("name not found")
            }
        }
        
        games.sort { (game1, game2) -> Bool in
            return game1.name < game2.name
        }
    }
    
    func getListOfDisks() -> [String] {
        return disks.keys.sorted()
    }
    
    func getGamesForDisk( key : String ) -> [Game] {
        guard let disk = disks[key] else { return [] }
        
        return disk.files
    }
}
