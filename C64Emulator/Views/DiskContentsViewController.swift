//
//  DiskContentsViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class DiskContentsViewController: UIViewController, UITableViewDelegate, UITableViewDataSource, UITextFieldDelegate {

    @IBOutlet var txtDiskName : UITextField!
    @IBOutlet var tableView : UITableView!
    @IBOutlet weak var btnUpdateDiskName: UIButton!
    
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
        
        self.txtDiskName.text = diskName.deletingPathExtension()
        
        
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
    
    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {
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
            if let _ = validPrograms.firstIndex(of: programName) {
                cell.validProgram.isOn = true
            }
            
            cell.setIsValidProgram = { [weak self] (program, isValid) in
                guard let `self` = self else { return }
                
                if let index = self.validPrograms.firstIndex(of: program) {
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
    
    @IBAction func btnUpdateDiskNamePressed(_ sender: Any) {
        txtDiskName.resignFirstResponder()
    }
    
    //MARK: UITableViewDelegate
    
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        
    }
    
    // MARK: UITextField delegate methods
    func textFieldDidBeginEditing(_ textField: UITextField) {
        btnUpdateDiskName.isHidden = false
        txtDiskName.selectAll(nil)
    }
    
    func textFieldDidEndEditing(_ textField: UITextField) {
        if let name = textField.text {
            var newName = name
            if !newName.hasSuffix(".d64") {
                newName += ".d64"
            }
            do {
                try DiskManager.renameDisk( from: diskName, to: newName )
                self.diskName = newName
                NotificationCenter.default.post(name: Notif_GamesUpdated, object: nil)
            } catch let error as NSError {
                print( "Error renaming disk - \(error)")
            }
        }
        
        btnUpdateDiskName.isHidden = true
    }
}
