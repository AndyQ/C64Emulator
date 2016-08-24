//
//  C64ModelSettingsViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 13/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

class C64ModelSettingsViewController: UITableViewController {

    override func viewDidLoad() {
        super.viewDidLoad()

        // Uncomment the following line to preserve selection between presentations
        // self.clearsSelectionOnViewWillAppear = false

        // Uncomment the following line to display an Edit button in the navigation bar for this view controller.
        // self.navigationItem.rightBarButtonItem = self.editButtonItem()
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    // MARK: - Table view data source

    override func numberOfSections(in tableView: UITableView) -> Int {
        return 3
    }

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        switch (section)
        {
        case 0:
            return 3;
        case 1:
            return 3;
        case 2:
            return 1;
        default:
            return 0
        }
    }

    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        
        // Configure the cell...
        var reuseId = "C64Cell"
        switch (indexPath.section)
        {
        case 0:
            fallthrough
        case 1:
            switch (indexPath.row)
            {
            case 0:
                reuseId = "C64Cell"
            case 1:
                reuseId = "C64CCell"
            default:
                reuseId = "C64OldCell"
            }
        default:
            reuseId = "C64DreanCell"
        }

        let current_model = Int(theVICEMachine.machineController().getModel())
        let section = current_model / 3
        let row = current_model % 3
        let cell = tableView.dequeueReusableCell(withIdentifier: reuseId)
        if indexPath.row == row && indexPath.section == section {
            cell!.accessoryType = .checkmark
        } else {
            cell!.accessoryType = .none
        }
        
        return cell!
    }

    override func tableView(_ tableView: UITableView, titleForHeaderInSection section: Int) -> String? {
        switch (section)
        {
        case 0:
            return "PAL";
        case 1:
            return "NTSC";
        case 2:
            return "PAL-N";
        default:
            return ""
        }
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        let modelNumber = 3*indexPath.section+indexPath.row;

        
        let old_model = Int(theVICEMachine.machineController().getModel())
        let old_standard = old_model / 3
        if old_standard != indexPath.section
        {
            let ac = UIAlertController(title: "WARNING", message: "Switching TV standards causes emulator to restart", preferredStyle: .alert)
            ac.addAction(UIAlertAction(title: "OK", style: .default, handler: { [weak self] (action) in
                guard let `self` = self else { return }

                theVICEMachine.perform(#selector(VICEMachine.selectModel(_:)), on: theVICEMachine.machineThread(), with: modelNumber, waitUntilDone: true)
                
                theVICEMachine.restart(theVICEMachine.autoStartPath())
                _ = self.navigationController?.popViewController(animated: true)
                
            }))
            ac.addAction(UIAlertAction(title: "Cancel", style: .default, handler: { [weak self] (action) in
                guard let `self` = self else { return }

                let section = modelNumber / 3;
                let row = modelNumber % 3;
                
                
                self.tableView.deselectRow(at:IndexPath(row: row, section:section), animated: false )

            }))
            
            self.present(ac, animated: true, completion: nil)
        }
        else
        {
            theVICEMachine.perform(#selector(VICEMachine.selectModel(_:)), on: theVICEMachine.machineThread(), with: modelNumber, waitUntilDone: true)

            _ = self.navigationController?.popViewController(animated: true)
        }
    }

}
