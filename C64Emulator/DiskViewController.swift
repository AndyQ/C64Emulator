//
//  DiskViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit
import GCDWebServer

class DiskViewController: UIViewController, UITableViewDelegate, UITableViewDataSource {
    
    @IBOutlet var tableView : UITableView!
    @IBOutlet var viewTypeSeg : UISegmentedControl!

    var webUploader : WebServer!

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
        
    }
    
    override func viewWillAppear(_ animated: Bool) {
        updateListOfDisks()
        self.tableView.reloadData()
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    func showDisk( diskPath: String ) {
        selectedDiskName = diskPath
        self.performSegue(withIdentifier: "showDiskContents", sender: self)
    }
    
    
    @IBAction func startPressed(_ sender: AnyObject) {
         selectedDiskName = ""
         selectedProgram = ""
         self.performSegue(withIdentifier: "showEmulator", sender: self)
    }
    
    @IBAction func uploadPressed(_ sender: AnyObject) {
/*
        selectedDiskName = ""
        selectedProgram = ""
        self.performSegue(withIdentifier: "showEmulator", sender: self)
*/
        

        webUploader = WebServer()
        webUploader.startServer()
        
        let ac = UIAlertController(title: "Upload started", message: "You can now upload disks by visiting: \(webUploader.getServerAddress()) in your browser", preferredStyle: .alert)
        
        ac.addAction(UIAlertAction(title: "Stop", style: .default, handler: { [weak self] (action) in
            guard let `self` = self else { return }
            self.webUploader.stopServer()
            self.tableView.reloadData()
        }))
            
        self.present(ac, animated: true, completion: nil)

    }

    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {
        if let vc = segue.destination as? EmulatorViewController {
            vc.dataFileURLString = selectedDiskName
            vc.program = selectedProgram
        } else if let vc = segue.destination as? DiskContentsViewController {
            vc.diskPath = selectedDiskName
        }
        
        selectedDiskName = ""
    }
    
    @IBAction func viewTypeChanged(_ sender: AnyObject) {
        self.tableView.reloadData()
    }

    func updateListOfDisks() {
        disks.removeAll()
        games.removeAll()
        
        disks = DatabaseManager.sharedInstance.getListOfDisks()
        let list = DatabaseManager.sharedInstance.getListOfGames()
        
        // Split games into sections (A-Z)
        for game in list {
            
            let c = String(game.name.characters.first!)
            if games[c] != nil {
                games[c]!.append(game)
            } else {
                games[c] = [game]
            }
        }
    }
    
    //MARK: UITableViewDataSource
    
    func numberOfSections(in tableView: UITableView) -> Int {
        if viewTypeSeg.selectedSegmentIndex == 0 {
            if games.count > 0 {
                self.tableView.backgroundView = nil;
                return sections.count
            } else {
                let message = "There are no programs currently cataloged"
                let messageLabel = UILabel(frame: CGRect(x:0, y:0, width:self.tableView.bounds.size.width,
                                                         height:self.tableView.bounds.size.height))
                messageLabel.text = message
                messageLabel.textColor = UIColor.black
                messageLabel.numberOfLines = 0;
                messageLabel.textAlignment = .center;
                messageLabel.font = UIFont(name: "TrebuchetMS", size: 15)
                messageLabel.sizeToFit()
                
                self.tableView.backgroundView = messageLabel;
                self.tableView.separatorStyle = .none;
                return 0
                
            }
        } else {
            if disks.count > 0 {
                self.tableView.backgroundView = nil;
                return 1
            } else {
                let message = "There are no disks available"
                let messageLabel = UILabel(frame: CGRect(x:0, y:0, width:self.tableView.bounds.size.width,
                                                         height:self.tableView.bounds.size.height))
                messageLabel.text = message
                messageLabel.textColor = UIColor.black
                messageLabel.numberOfLines = 0;
                messageLabel.textAlignment = .center;
                messageLabel.font = UIFont(name: "TrebuchetMS", size: 15)
                messageLabel.sizeToFit()
                
                self.tableView.backgroundView = messageLabel;
                self.tableView.separatorStyle = .none;
                return 0
            }

        }
    }
    
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        if viewTypeSeg.selectedSegmentIndex == 0 {
            let key = sections[section]
            if let arr = games[key] {
                return arr.count
            }
            return 0
        } else {
            return disks.count
        }
    }
    
    func sectionIndexTitles(for tableView: UITableView) -> [String]? {
        
        if viewTypeSeg.selectedSegmentIndex == 0 {
            return sections
        } else {
            return []
        }
    }
    
    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "Cell", for: indexPath) as! DiskCell

        if viewTypeSeg.selectedSegmentIndex == 0 {
            
            let key = sections[indexPath.section]
            let game = games[key]![indexPath.row]
            cell.name.text = game.name
            cell.diskName = game.diskName
            
        } else {
            
            cell.name.text = disks[indexPath.row].deletingPathExtension()
            cell.diskName = disks[indexPath.row]
        }
        
        cell.startDisk = { [weak self] (diskName) in
            guard let `self` = self else { return }
            
            self.selectedDiskName = getUserGamesDirectory() + "/" + diskName
            self.selectedProgram = ""
            self.performSegue(withIdentifier: "showEmulator", sender: self)
        }

        return cell
    }
    
    func configureCell(cell: UITableViewCell, forRowAtIndexPath: IndexPath) {
        
    }
    
    //MARK: UITableViewDelegate
    
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        
        if viewTypeSeg.selectedSegmentIndex == 0 {
            let key = sections[indexPath.section]
            let game = games[key]![indexPath.row]

            self.selectedDiskName = getUserGamesDirectory() + "/" + game.diskName
            self.selectedProgram = game.name
            self.performSegue(withIdentifier: "showEmulator", sender: self)
        } else {
            // Show disk contents
            print( "Showing disk contents...")
            
            selectedDiskName = getUserGamesDirectory() + "/" + disks[indexPath.row]
            
            self.performSegue(withIdentifier: "showDiskContents", sender: self)
        }
        
    }
}
