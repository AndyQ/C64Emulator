//
//  CodeEditorViewController.swift
//  C64Emulator
//
//  Created by Andy Qua on 26/12/2021.
//  Copyright © 2021 Andy Qua. All rights reserved.
//

import UIKit
import Sourceful
import GCDWebServers

extension UIViewController {
    func showError( _ message : String ) {
        let ac = UIAlertController(title: "Error", message: message, preferredStyle: .alert)
        ac.addAction(UIAlertAction.init(title: "OK", style: .default, handler: nil))
        // Check to see if the target viewController current is currently presenting a ViewController
        if self.presentedViewController == nil {
            self.present(ac, animated: true, completion: nil)
        }
    }
    
    func presentTextFieldAlert(title: String, message: String, textFieldPlaceholder: String, completion: @escaping (String?)->()) {
        let alertController = UIAlertController(title: title, message: message, preferredStyle: .alert)
        let saveAction = UIAlertAction(title: "OK", style: UIAlertAction.Style.default) { _ -> Void in
            let urlTextField = alertController.textFields![0] as UITextField
            completion(urlTextField.text)
        }
        
        let cancelAction = UIAlertAction(title: "Cancel", style: .default)
        alertController.addTextField { (textField: UITextField!) -> Void in
            textField.placeholder = textFieldPlaceholder
            completion(nil)
        }
        alertController.addAction(saveAction)
        alertController.addAction(cancelAction)
        self.present(alertController, animated: true, completion: nil)
    }
}

class CodeEditorViewController: UIViewController {
    let lexer = SwiftLexer()
    
    var projectName: String = ""
    var fileList : [URL] = []
    var selectedFile : URL!
    
    @IBOutlet weak var syntaxTextView: SyntaxTextView!
    @IBOutlet weak var fileListTV: UITableView!
    @IBOutlet weak var projectButton: UIButton!
    @IBOutlet weak var addFileButton: UIButton!

    var codeLogVC : CodeLogViewController!
    
    var webUploader = WebServer()

    override func viewDidLoad() {
        super.viewDidLoad()

        syntaxTextView.theme = DefaultSourceCodeTheme()
        syntaxTextView.delegate = self
        
        // Create menus
        projectButton.menu = createProjectMenu()
        projectButton.showsMenuAsPrimaryAction = true
        
        addFileButton.menu = createFileMenu()
        addFileButton.showsMenuAsPrimaryAction = true
        addFileButton.isEnabled = false
    }
    

    override func prepare(for segue: UIStoryboardSegue, sender: Any?) {
        if let vc = segue.destination as? CodeLogViewController {
            codeLogVC = vc
        }
        
        if let vc = segue.destination as? EmulatorViewController {
            vc.dataFileURLString = projectPath.appendingPathComponent("\(projectName).prg").path
        }
    }
    
    func createProjectMenu() -> UIMenu {
        let newProject = UIAction(title: "New project", image: UIImage(systemName: "folder.badge.plus")) { [unowned self] (_) in
            askForNewProject()
        }
        
        // Get list of Projects
        var projects = [UIMenuElement]()
        let fm = FileManager.default
        if let projectNames = try? fm.contentsOfDirectory(atPath: projectPath.path) {
            for name in projectNames {
                let p = UIAction(title: name ) { [unowned self] (_) in
                    self.openProject(name:name)
                }
                projects.append(p)
            }
        }
        let openProject = UIMenu(title: "Open project", image: UIImage(systemName: "folder"), children: projects)


        let menu = UIMenu(title: "Project", children: [newProject, openProject])
        return menu
    }

    func createFileMenu() -> UIMenu {
        let newFile = UIAction(title: "New file", image: UIImage(systemName: "doc")) { [unowned self] (_) in
            askForNewFile()
        }

        let fromFiles = UIAction(title: "Add file from Files app", image: UIImage(systemName: "filemenu.and.selection")) { (_) in
        }
        let fromWeb = UIAction(title: "Upload file", image: UIImage(systemName: "network")) { (_) in
            self.uploadFileFromWeb()
        }
        let addFile = UIMenu(title: "Add file", image: UIImage(systemName: "doc.badge.plus"), children: [fromFiles, fromWeb])

        let menu = UIMenu(title: "File", children: [newFile, addFile])
        return menu
    }
}

// MARK: view Functions
extension CodeEditorViewController {

    @IBAction func compilePressed( _ sender : Any ) {
        let output = projectPath.appendingPathComponent("\(projectName).prg")
        
        do {
            // Get first file in project that ends in .a
            let fm = FileManager.default
            let files = try fm.contentsOfDirectory(atPath: projectPath.path)
            var sourceFile : URL?
            for f in files {
                if f.pathExtension() == "a" {
                    sourceFile = projectPath.appendingPathComponent(f)
                    break
                }
            }
            guard let sourceFile = sourceFile else {
                codeLogVC.setLog( logLines: ["Unable to find .a file to compile"] )
                return
            }

            try saveFile()
            let args : [String] = ["", "-v5", "-I", projectPath.path, "--outfile", output.path, "--format", "cbm", sourceFile.path]
            var cargs = args.map { strdup($0) }
            
            let _ = acme_compile(Int32(args.count), &cargs)
            if let log = acme_getLogItems() as? [String] {
                codeLogVC.setLog( logLines: log )
            }
            for ptr in cargs { free(ptr) }
        } catch {
            showError("Error compiling\n\n\(error)")
        }
    }

    @IBAction func runPressed( _ sender : Any ) {
        self.performSegue(withIdentifier: "showEmulator", sender: self)

    }
}

// MARK: Project functionality
extension CodeEditorViewController {
    var projectPath : URL {
        let projectPath = getDocumentsURLPath().appendingPathComponent("projects").appendingPathComponent(projectName)
        return projectPath
    }

    func askForNewProject() {
        // Ask for project name
        self.presentTextFieldAlert(title: "New project", message: "Enter name of project", textFieldPlaceholder: "Project name") { [unowned self] value in
            guard let value = value else { return }
            do {
                projectName = value
                try createNewProject(name: projectName)
                try refreshFileList()
                fileListTV.reloadData()
                self.addFileButton.isEnabled = true
                self.syntaxTextView.text = ""
            } catch {
                showError( "Error creating project:\n\(error)")
            }
        }
    }
    
    func createNewProject( name: String ) throws {
        let fm = FileManager.default
        
        let path = self.projectPath

        if !fm.fileExists(atPath: path.path) {
            try fm.createDirectory(at: path, withIntermediateDirectories: true)
        }
    }
    
    func openProject( name: String ) {
        do {
            projectName = name
            try refreshFileList()
            fileListTV.reloadData()
            self.addFileButton.isEnabled = true
            self.syntaxTextView.text = ""
        } catch {
            showError( "Error creating project:\n\(error)")
        }
    }

}

// MARK: File functionality
extension CodeEditorViewController {
    func askForNewFile( ) {
        // Ask for file name
        self.presentTextFieldAlert(title: "New file", message: "Enter name of file", textFieldPlaceholder: "File name") { [unowned self] value in
            guard let value = value else { return }
            do {
                let filePath = self.projectPath.appendingPathComponent(value)
                try "".write(to: filePath, atomically: false, encoding: .utf8)
                try refreshFileList()
                fileListTV.reloadData()
            } catch {
                showError( "Error creating project:\n\(error)")
            }
            
        }
    }
    
    func uploadFileFromWeb() {
        webUploader = WebServer()
        webUploader.startServer(uploadFolder: projectPath.path)
        
        let ac = UIAlertController(title: "Upload started", message: "You can now upload disks by visiting: \(webUploader.getServerAddress()) in your browser", preferredStyle: .alert)
        
        ac.addAction(UIAlertAction(title: "Stop", style: .default, handler: { [weak self] (action) in
            guard let `self` = self else { return }
            self.webUploader.stopServer()
            try? self.refreshFileList()
            self.fileListTV.reloadData()
        }))
        
        self.present(ac, animated: true, completion: nil)
    }

    func refreshFileList() throws {
        let path = self.projectPath
        
        let fm = FileManager.default
        fileList = try fm.contentsOfDirectory(at: path, includingPropertiesForKeys: nil, options:[])
    }
    
    func saveFile() throws {
        if let f = selectedFile {
            let text = syntaxTextView.text
            try text.write(to: f, atomically: true, encoding: .utf8)
        }
    }
}

// MARK: EditorDelegate
extension CodeEditorViewController: SyntaxTextViewDelegate {
    
    func didChangeText(_ syntaxTextView: SyntaxTextView) {
        print( "Text Changes")
    }
    
    func didChangeSelectedRange(_ syntaxTextView: SyntaxTextView, selectedRange: NSRange) {
        print( "sel text Changes")
    }
    
    func lexerForSource(_ source: String) -> Lexer {
        return lexer
    }
}


// MARK: File TableView datasource
extension CodeEditorViewController : UITableViewDataSource {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return fileList.count
    }
    
    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "FileCell") as! FileItemCell
        
        let name = fileList[indexPath.row].lastPathComponent
        cell.fileName.text = name
        return cell
    }
    
    func tableView(_ tableView: UITableView, titleForHeaderInSection section: Int) -> String? {
        return projectName
    }
}


// MARK: File TableView delegate
extension CodeEditorViewController : UITableViewDelegate {
    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        let file = fileList[indexPath.row]
        
        do {
            selectedFile = file
            let contents = try String(contentsOf: file)
            syntaxTextView.text = contents
        } catch {
            showError("Error loading file\n\(error)")
        }
    }
}
