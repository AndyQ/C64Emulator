//
//  WebServer.swift
//  C64Emulator
//
//  Created by Andy Qua on 11/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import GCDWebServer

func getDocumentsDirectory() -> String {
    if let documentsDirectory = NSSearchPathForDirectoriesInDomains(.documentDirectory, .userDomainMask, true).first {
        return documentsDirectory
    }
    
    return ""
}

func getUserGamesDirectory() -> String {
    
    let path = getDocumentsDirectory() + "/games"
    if !FileManager.default.fileExists(atPath: path) {
        do {
            try FileManager.default.createDirectory(atPath: path, withIntermediateDirectories: false, attributes: nil)
        } catch let error {
            print( "Failed to create folder - \(error.localizedDescription)" )
        }
    }
    return path
}



class WebServer : NSObject, GCDWebUploaderDelegate {
    var webServer : GCDWebUploader!

    static let sharedInstance : WebServer = WebServer()

    override init() {
        super.init()
        GCDWebUploader.setLogLevel(5)
        webServer = GCDWebUploader(uploadDirectory: getUserGamesDirectory())
        
        webServer.delegate = self
        webServer.allowHiddenItems = false
    }
    
    func startServer() {
        UIApplication.shared.isIdleTimerDisabled = true
        self.webServer.start()
    }
    
    func stopServer() {
        UIApplication.shared.isIdleTimerDisabled = false
        self.webServer.stop()
    }

    func getServerAddress() -> String {
        return webServer.serverURL!.absoluteString
    }
    
    func bonjourServerURL() -> URL {
        return self.webServer.bonjourServerURL!
    }

    //MARK - Web Server Delegate
    func webUploader(_ uploader: GCDWebUploader, didUploadFileAtPath path: String) {
        print( "[UPLOAD] \(path)")
        
        // Scan disk
        let d64 = D64Image()
        d64.readDiskDirectory(path)
        
//        print( "DiskName: \(d64.diskName!)" )
        
        DatabaseManager.sharedInstance.addDisk(diskName: (path as NSString).lastPathComponent )
//        print( "   Items: \((d64.items).map { ($0 as! D64Item).name! })" )
    }
    func webUploader(_ uploader: GCDWebUploader, didMoveItemFromPath fromPath: String, toPath: String) {
        print( "[MOVE] \(fromPath) -> \(toPath)")
    }
    func webUploader(_ uploader: GCDWebUploader, didDeleteItemAtPath path: String) {
        print( "[DELETE] \(path)")
        DatabaseManager.sharedInstance.removeDisk(diskName: (path as NSString).lastPathComponent )
    }
    func webUploader(_ uploader: GCDWebUploader, didDownloadFileAtPath path: String) {
        print( "[DOWNLOAD] \(path)")
    }
    func webUploader(_ uploader: GCDWebUploader, didCreateDirectoryAtPath path: String) {
        print( "[CREATE] \(path)")
    }

}
