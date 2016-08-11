//
//  DiskContentsViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class DiskContentsViewController: UIViewController, UITableViewDelegate, UITableViewDataSource {

    @IBOutlet var tableView : UITableView!
    
    var diskName : String = ""
    var diskContents : [Game]!
    
    var selectedProgram = ""
    
    override func viewDidLoad() {
        super.viewDidLoad()

        // Get contents of specified disk
        diskContents = DatabaseManager.sharedInstance.getGamesForDisk(key: diskName)

//        diskContents = diskimageContents(path:diskName)
        
//        print( "Contents of disk - \(diskContents)" )
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }
    
    override func prepare(for segue: UIStoryboardSegue, sender: AnyObject?) {
        if let vc = segue.destination as? EmulatorViewController {
            vc.dataFileURLString = diskName
            vc.program = selectedProgram
        }
    }
    
    
    func diskimageContents( path: String ) -> [D64Item]
    {
        let disk = D64Image()
        disk.readDiskDirectory(path)
        if let items = disk.items as NSArray as? [D64Item] {
            return items
        }
        return []
//        return convertImageContentsToArray(ic)
    }
    
    //MARK: UITableViewDataSource
    
    func numberOfSections(in tableView: UITableView) -> Int {
        return 1
    }
    
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return diskContents.count
    }
    
    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "Cell", for: indexPath) as UITableViewCell
        
        cell.textLabel?.text = diskContents[indexPath.row].name
        return cell
    }
    
    //MARK: UITableViewDelegate
    
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        selectedProgram = diskContents[indexPath.row].name
        self.performSegue(withIdentifier: "showEmulator", sender: self)
    }
}
