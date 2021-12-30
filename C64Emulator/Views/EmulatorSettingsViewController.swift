//
//  EmulatorSettingsViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 13/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit


class EmulatorScreenSettingsCell : UITableViewCell
{
    @IBOutlet var saturationSlider : UISlider!
    @IBOutlet var contrastSlider : UISlider!
    @IBOutlet var brightnessSlider : UISlider!
    @IBOutlet var gammaSlider : UISlider!
    @IBOutlet var blurSlider : UISlider!
    @IBOutlet var scanlineShadeSlider : UISlider!
    @IBOutlet var oddLinePhaseSlider : UISlider!
    @IBOutlet var oddLineOffsetSlider : UISlider!
    
}


class EmulatorSettingsViewController: UITableViewController {


    override func viewDidLoad() {
        super.viewDidLoad()

        // Uncomment the following line to preserve selection between presentations
        // self.clearsSelectionOnViewWillAppear = false

        // Uncomment the following line to display an Edit button in the navigation bar for this view controller.
        // self.navigationItem.rightBarButtonItem = self.editButtonItem()
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear( animated )
        
        self.tableView.reloadData()
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear( animated )

        theVICEMachine.machineController().saveResources(nil)
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {
        if let vc = segue.destination as? DiskImageViewController {
            vc.currentDiskName = theVICEMachine.driveImageName()
        }
    }

    // MARK: - Table view data source

    override func numberOfSections(in tableView: UITableView) -> Int {
        return 4
    }

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        switch (section)
        {
        case 0:
            return 1;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 1;
        default:
            return 0
        }
    }

    let sDetailStringForModel = ["PAL C64", "PAL C64C", "PAL C64 (old)", "NTSC C64", "NTSC C64C", "NTSC C64 (old)", "Drean C64"]

    
    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {

        var cell : UITableViewCell!
        
        switch (indexPath.section)
        {
        case 0:
            cell = tableView.dequeueReusableCell(withIdentifier :"C64ModelCell")
            let model :Int = Int(theVICEMachine.machineController().getModel())
            cell.detailTextLabel?.text = sDetailStringForModel[model];
            break;
        case 1:
            cell = tableView.dequeueReusableCell(withIdentifier :"DiskTapeCell")
            cell.detailTextLabel?.text = theVICEMachine.driveImageName()
            break;
        case 2:
            
            switch (indexPath.row)
            {
            case 0:
                let c = tableView.dequeueReusableCell(withIdentifier :"EmulatorScreenSettingsCell") as! EmulatorScreenSettingsCell
                
                c.saturationSlider.value = theVICEMachine.machineController().getIntResource("VICIIColorSaturation").floatValue
                c.contrastSlider.value = theVICEMachine.machineController().getIntResource("VICIIColorContrast").floatValue
                c.brightnessSlider.value = theVICEMachine.machineController().getIntResource("VICIIColorBrightness").floatValue
                c.gammaSlider.value = theVICEMachine.machineController().getIntResource("VICIIColorGamma").floatValue
                c.blurSlider.value = theVICEMachine.machineController().getIntResource("VICIIPALBlur").floatValue
//                c.scanlineShadeSlider.value = theVICEMachine.machineController().getIntResource("VICIIPALScanLineShade").floatValue
//                c.oddLinePhaseSlider.value = theVICEMachine.machineController().getIntResource("VICIIPALOddLinePhase").floatValue
//                c.oddLineOffsetSlider.value = theVICEMachine.machineController().getIntResource("VICIIPALOddLineOffset").floatValue
                cell = c
                break;
            case 1:
                cell = tableView.dequeueReusableCell(withIdentifier :"ResetScreenCell")
                break;
            default:
                cell = nil
            }
            break;
        case 3:
            cell = tableView.dequeueReusableCell(withIdentifier :"ResetMachineCell")
            break;
        default:
            cell = nil
        }
        
        return cell;
    }

    
    // Override to support conditional editing of the table view.
    override func tableView(_ tableView: UITableView, canEditRowAt indexPath: IndexPath) -> Bool {
        // Return false if you do not want the specified item to be editable.
        return false
    }
    
    override func tableView(_ tableView: UITableView, heightForRowAt indexPath: IndexPath) -> CGFloat {
        
        if indexPath.section == 2 && indexPath.row == 0 {
            return 265.0
        }
        
        return 45.0
    }
    
    override func tableView(_ tableView: UITableView, willSelectRowAt indexPath: IndexPath) -> IndexPath? {
        if indexPath.section == 2 {
            return nil
        }
        
        return indexPath

    }
    
    /*
    // MARK: - Navigation

    // In a storyboard-based application, you will often want to do a little preparation before navigation
    override func prepare(for segue: UIStoryboardSegue, sender: AnyObject?) {
        // Get the new view controller using segue.destinationViewController.
        // Pass the selected object to the new view controller.
    }
    */

    @IBAction func changeSaturation(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIColorSaturation", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeContrast(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIColorContrast", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeBrightness(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIColorBrightness", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeGamma(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIColorGamma", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeBlur(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIPALBlur", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeScanlineShade(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIPALScanLineShade", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeOddLinePhase(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIPALOddLinePhase", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func changeOddLineOffset(_ sender : UISlider) {
        let value = sender.value
        let keyValue = VICEMachineResourceKeyValue(resource: "VICIIPALOddLineOffset", andValue: value as NSNumber)
        theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: false)
    }
    
    @IBAction func resetScreenToDefaults(_ sender : UISlider) {
        
        let values :[Float]   = [1000, 1000, 1000, 2200, 0, 800, 1000, 1000]
        let keys : [String] = ["VICIIColorSaturation", "VICIIColorContrast", "VICIIColorBrightness", "VICIIColorGamma",
                                    "VICIIPALBlur", "VICIIPALScanLineShade" , "VICIIPALOddLinePhase", "VICIIPALOddLineOffset"]
        
        for i in 0 ..< keys.count {
            let keyValue = VICEMachineResourceKeyValue(resource: keys[i], andValue: values[i] as NSNumber)
            theVICEMachine.perform(#selector(VICEMachine.setResourceOnceAfterDelay(_:)), on: theVICEMachine.machineThread(), with: keyValue, waitUntilDone: true)
        }

        self.tableView.reloadRows(at: [IndexPath(row: 0, section: 2)], with: .none)

    }
    
    @IBAction func resetMachine(_ sender : AnyObject) {
        let ac = UIAlertController(title: "WARNING", message: "What sort of reset?", preferredStyle: .alert)
        ac.addAction(UIAlertAction(title: "Hard Reset", style: .default, handler: { (action) in
            theVICEMachine.machineController().resetMachine(true)
        }))
        ac.addAction(UIAlertAction(title: "Soft Reset", style: .default, handler: { (action) in
            theVICEMachine.machineController().resetMachine(false)
        }))
        ac.addAction(UIAlertAction(title: "Cancel", style: .default, handler: nil))
        
        self.present(ac, animated: true, completion: nil)
    }
    

    
    @IBAction func dismiss(_ sender : AnyObject) {
        self.dismiss(animated: true, completion: nil)
    }

}
