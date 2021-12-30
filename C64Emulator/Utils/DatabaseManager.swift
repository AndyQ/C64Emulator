//
//  DatabaseManager.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import Foundation
import FMDB

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
    
    private override init() {
        
        super.init()
        
        createDatabase()
    }
    
    class func clearDatabase() {
        let dbFile = (getDocumentsDirectory() as NSString).appendingPathComponent("games.db")
        try? FileManager.default.removeItem(atPath: dbFile)
    }
    
    func resetDatabase() {
        if let db = openDatabase() {
            try? db.executeUpdate("drop table userDisks", values:nil)
            try? db.executeUpdate("drop table userPrograms", values:nil)
            
            db.close()
        }
        createDatabase()
    }
    
    func createDatabase() {
        if let db = openDatabase() {
            try? db.executeUpdate("create table userDisks (diskName text, comments text) ", values:nil)
            try? db.executeUpdate("create table userPrograms (programName text, diskName text) ", values:nil)
            
            db.close()
        }
    }
    
    func openDatabase() -> FMDatabase? {
        
        guard let bundleDBFile = Bundle.main.path(forResource: "games", ofType: "db") else { return nil }

        let dbFile = (getDocumentsDirectory() as NSString).appendingPathComponent("games.db")
//        if !FileManager.default.fileExists(atPath: dbFile) {
//            do {
//                try FileManager.default.copyItem(atPath: bundleDBFile, toPath: dbFile)
//            } catch let error {
//                print( "Failed to copy database to documents folder - \(error.localizedDescription)" )
//            }
//            
//        }
        
        print( "DB File - \(dbFile)" )
        let db = FMDatabase(path: dbFile)
        db.logsErrors = false
        
        guard db.open() else {
            print("Unable to open database")
            return nil
        }

        return db
    }

    

    func getListOfGames() -> [Game] {
        
        guard let db = openDatabase() else { return [] }
        
        var games = [Game]()
        // open database
        do {
            let rs = try db.executeQuery("select programName, diskName from userPrograms order by programName", values: nil)
            while rs.next() {
                if let programName = rs.string(forColumn: "programName"),
                    let diskName = rs.string(forColumn: "diskName") {
                    
                    let game = Game()
                    game.name = programName
                    game.diskName = diskName
                    
                    games.append(game)
                }
            }
        } catch let error as NSError {
            print("failed: \(error.localizedDescription)")
        }
        
        db.close()
    
        games.sort { (game1, game2) -> Bool in
            return game1.name < game2.name
        }
        
        return games
    }
    
    func addDisk( diskName: String ) {
        guard let db = openDatabase() else { return }
        
        do {
            try db.executeUpdate("insert into userDisks (diskName) values (?)", values: [diskName])
            
        } catch {
            print("failed: \(error.localizedDescription)")
        }
        
        
        db.close()
    }
    
    func removeDisk( diskName : String ) {
        guard let db = openDatabase() else { return }
        
        do {
            try db.executeUpdate("delete from userDisks where diskName = ?", values: [diskName])
            try db.executeUpdate("delete from userPrograms where diskName = ?", values: [diskName])
            
        } catch {
            print("failed: \(error.localizedDescription)")
        }
        
        
        db.close()
    }
    
    func renameDisk( from: String, to: String ) {
        guard let db = openDatabase() else { return }
        
        do {
            try db.executeUpdate("update userDisks set diskName = ? where diskName = ?", values: [to,from])
            try db.executeUpdate("update userPrograms set diskName = ? where diskName = ?", values: [to,from])
            
        } catch {
            print("failed: \(error.localizedDescription)")
        }
        
        
        db.close()
    }

    
    func getListOfDisks() -> [String] {
        var disks = [String]()
        
        guard let db = openDatabase() else { return [] }

        do {
            let rs = try db.executeQuery("select diskName from userDisks order by diskName", values: nil)
            while rs.next() {
                if let diskName = rs.string(forColumn: "diskName") {
                    disks.append( diskName )
                }
            }
        } catch {
            print("failed: \(error.localizedDescription)")
        }

        db.close()

        return disks.sorted()
    }
  
    func getPrograms( diskName : String ) -> [String] {
        var programs = [String]()
        
        guard let db = openDatabase() else { return [] }
        
        do {
            let rs = try db.executeQuery("select programName from userPrograms where diskName = ?", values: [diskName])
            while rs.next() {
                if let programName = rs.string(forColumn: "programName") {
                    programs.append( programName )
                }
            }
        } catch {
            print("failed: \(error.localizedDescription)")
        }
        
        db.close()
        
        return programs.sorted()

    }
    
    func updatePrograms( diskName : String, programs : [String] ) {
        guard let db = openDatabase() else { return }
        
        do {
            try db.executeUpdate("delete from userPrograms where diskName = ?", values: [diskName])

            let sql = "Insert into userPrograms ( diskName, programName ) VALUES ( ?, ? )"
            db.beginTransaction()
            for program in programs {
                try db.executeUpdate(sql, values: [diskName, program] )
            }
            db.commit()
        } catch {
            print("failed: \(error.localizedDescription)")
        }
        
        db.close()

    }
/*
    func getGamesForDisk( key : String ) -> [Game] {
        guard let disk = disks[key] else { return [] }
        
        return disk.files
    }
*/
}
