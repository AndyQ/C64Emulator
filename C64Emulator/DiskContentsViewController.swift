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
    
    var diskPath : String = ""
    var diskName : String = ""
    var diskContents : [D64Item]!
    var validPrograms = [String]()
    
    var selectedProgram = ""
    
    override func viewDidLoad() {
        super.viewDidLoad()

        // Get contents of specified disk
        diskName = (diskPath as NSString).lastPathComponent
        diskContents = diskimageContents(path:diskPath)
        
        // Get existing marked programs
        validPrograms = DatabaseManager.sharedInstance.getPrograms( diskName : diskName )
        
//        print( "Contents of disk - \(diskContents)" )
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        // Update the valid programs
        DatabaseManager.sharedInstance.updatePrograms( diskName: diskName, programs : validPrograms )
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
            var ret = items
            if let diskName = disk.diskName {
                let disk = D64Item()
                disk.name = "<\(diskName)>"
                ret.insert(disk, at: 0)
            } else {
            }
            return ret
        }
        return []
    }
    
    //MARK: UITableViewDataSource
    
    func numberOfSections(in tableView: UITableView) -> Int {
        return 1
    }
    
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return diskContents.count
    }
    
    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "Cell", for: indexPath) as! DiskItemCell
        
        if let programName = diskContents[indexPath.row].name {

            cell.programName.text = programName
        
            cell.validProgram.isOn = false
            if let _ = validPrograms.index(of: programName) {
                cell.validProgram.isOn = true
            }
            
            cell.setIsValidProgram = { [weak self] (program, isValid) in
                guard let `self` = self else { return }
                
                if let index = self.validPrograms.index(of: program) {
                    // If we found the program, then if it isn't marked as valid remove it
                    if !isValid {
                        self.validPrograms.remove(at: index)
                    }
                } else {
                    // If we didn't find the program then if it is marked as valid add it
                    if isValid {
                        self.validPrograms.append(program)
                    }
                }
            }
        }
        return cell
    }
    
    //MARK: UITableViewDelegate
    
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        
    }
}
