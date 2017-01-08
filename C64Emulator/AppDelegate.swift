//
//  AppDelegate.swift
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

import UIKit

let Notif_GamesUpdated = Notification.Name("Notif_GamesUpdated")

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    func application(_ app: UIApplication, open url: URL, options: [UIApplicationOpenURLOptionsKey : Any] = [:]) -> Bool {
        print( "Requested to open URL - \(url)")
        
        let diskName = url.lastPathComponent

        let destPath = getUserGamesDirectory().appendingPathComponent(diskName)

        // Move file to user games folder
        let fm = FileManager.default
        do {
            try fm.moveItem(at: url, to: URL(fileURLWithPath: destPath))
            let d64 = D64Image()
            d64.readDiskDirectory(destPath)
            
            DatabaseManager.sharedInstance.addDisk(diskName: diskName)
            
            NotificationCenter.default.post(name: Notif_GamesUpdated, object: nil)
        
            if let rnc = window?.rootViewController as? UINavigationController,
                let rvc = rnc.topViewController as? DiskViewController {
                rvc.showDisk( diskPath:destPath )
            }
        } catch let error {
            print( "Can't move file as it already exists - \(diskName) - \(error)")
            
            try? fm.removeItem(at: url)
            return false
        }
        
        return true
    }
    
    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplicationLaunchOptionsKey : Any]? = nil) -> Bool {
        
        removeAllGames()
        return true
    }

    func applicationWillResignActive(_ application: UIApplication) {
        // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
        // Use this method to pause ongoing tasks, disable timers, and invalidate graphics rendering callbacks. Games should use this method to pause the game.
    }

    func applicationDidEnterBackground(_ application: UIApplication) {
        // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
        // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
    }

    func applicationWillEnterForeground(_ application: UIApplication) {
        // Called as part of the transition from the background to the active state; here you can undo many of the changes made on entering the background.
    }

    func applicationDidBecomeActive(_ application: UIApplication) {
        // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
    }

    func applicationWillTerminate(_ application: UIApplication) {
        // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
    }

    
    func removeAllGames() {
        DatabaseManager.clearDatabase()
        
        //Remove contents of games folder
        let path = getUserGamesDirectory()
        FileManager.default.clearFolderAtPath(path: path)
    }

}

