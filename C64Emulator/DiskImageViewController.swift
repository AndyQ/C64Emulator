//
//  DiskImageViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 13/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class DiskImageViewController: UITableViewController {

    var mediaFiles : [String]!
    var currentDiskName : String!

    override func viewDidLoad() {
        super.viewDidLoad()

        mediaFiles = DatabaseManager.sharedInstance.getListOfDisks()
        
        if let pos = mediaFiles.index(of: currentDiskName) {
            DispatchQueue.main.async {
                self.tableView.scrollToRow(at: IndexPath(row:pos, section:0), at: .middle, animated: true)
            }
        }
    }
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    // MARK: - Table view data source

    override func numberOfSections(in tableView: UITableView) -> Int {
        // #warning Incomplete implementation, return the number of sections
        return 1
    }

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        // #warning Incomplete implementation, return the number of rows
        return mediaFiles.count == 0 ? 1 : mediaFiles.count
    }


    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "Cell", for: indexPath)

        if mediaFiles.count == 0 {
            cell.textLabel?.text = "No Disk/Tape Media to Select"
        } else {
            cell.textLabel?.text = mediaFiles[indexPath.row]
            if mediaFiles[indexPath.row] == currentDiskName {
                cell.accessoryType = .checkmark
            } else {
                cell.accessoryType = .none;
            }
        }

        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        let path = getUserGamesDirectory() + "/\(mediaFiles[indexPath.row])"
        theVICEMachine.machineController().attachDiskImage(8, path: path)
        
        _ = self.navigationController?.popViewController(animated: true)

    }
}
