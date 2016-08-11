//
//  DiskViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class DiskViewController: UIViewController, UITableViewDelegate, UITableViewDataSource {
    
    @IBOutlet var tableView : UITableView!

    var gamesFolder : String!
    var disks = [String]()
    var games = [String:[Game]]()
    var selectedDiskName : String = ""
    var selectedProgram : String = ""
    let sections = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ".characters.map { String($0) }

    override func viewDidLoad() {
        super.viewDidLoad()

        gamesFolder = ""
        if let folder = Bundle.main.path(forResource: "games", ofType: "") {
            gamesFolder = folder
        }
        
        // Load disks
        updateListOfDisks()
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    
    @IBAction func startPressed(_ sender: AnyObject) {
        selectedDiskName = ""
        selectedProgram = ""
        self.performSegue(withIdentifier: "showEmulator", sender: self)
    }

    override func prepare(for segue: UIStoryboardSegue, sender: AnyObject?) {
        if let vc = segue.destination as? EmulatorViewController {
            vc.dataFileURLString = selectedDiskName
            vc.program = selectedProgram
        }
    }

    func updateListOfDisks() {
        disks.removeAll()
        
        DatabaseManager.sharedInstance.getListOfGames()
        
        let list = DatabaseManager.sharedInstance.games
        
        // Split games into sections (A-Z)
        for game in list {
            
            let c = String(game.name.characters.first!)
            if games[c] != nil {
                games[c]!.append(game)
            } else {
                games[c] = [game]
            }
        }
        
/*
        if let gamesFolder = Bundle.main.path(forResource: "games", ofType: "") {
            do {
                disks = try FileManager.default.contentsOfDirectory(atPath: gamesFolder )
            } catch let error {
                print( "Error getting disks - \(error)")
            }
        }
*/
    }
    
    //MARK: UITableViewDataSource
    
    func numberOfSections(in tableView: UITableView) -> Int {
        return sections.count
    }
    
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        let key = sections[section]
        if let arr = games[key] {
            return arr.count
        }
        return 0
    }
    
    func sectionIndexTitles(for tableView: UITableView) -> [String]? {
        return sections
    }
    
    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "Cell", for: indexPath) as UITableViewCell

        let key = sections[indexPath.section]
        cell.textLabel?.text = games[key]![indexPath.row].name
        return cell
    }
    
    func configureCell(cell: UITableViewCell, forRowAtIndexPath: IndexPath) {
        
    }
    
    //MARK: UITableViewDelegate
    
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        
//        selectedDiskName = (gamesFolder as NSString).appendingPathComponent(disks[indexPath.row])
        
        let key = sections[indexPath.section]
        let game = games[key]![indexPath.row]

        if let gameFilePath = Bundle.main.path(forResource: "games/\(game.diskName)", ofType: "D64") {
        
            selectedDiskName = gameFilePath
            selectedProgram = game.name
        }
        self.performSegue(withIdentifier: "showEmulator", sender: self)

//        self.performSegue(withIdentifier: "showDiskContents", sender: self)
    }
}
