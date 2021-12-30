//
//  AppDelegate.swift
//  C64Emulator
//
//  Created by Andy Qua on 26/12/2021.
//

import UIKit

let Notif_GamesUpdated = Notification.Name("Notif_GamesUpdated")

@main
class AppDelegate: UIResponder, UIApplicationDelegate {

    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {

        let appearance = UINavigationBarAppearance()
        appearance.configureWithDefaultBackground()
        //        appearance.titleTextAttributes = @{NSForegroundColorAttributeName : UIColor.whiteColor};
        UINavigationBar.appearance().standardAppearance = appearance
        UINavigationBar.appearance().scrollEdgeAppearance = appearance
        
        return true
    }

    // MARK: UISceneSession Lifecycle

    func application(_ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        // Called when a new scene session is being created.
        // Use this method to select a configuration to create the new scene with.
        return UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
    }

    func application(_ application: UIApplication, didDiscardSceneSessions sceneSessions: Set<UISceneSession>) {
        // Called when the user discards a scene session.
        // If any sessions were discarded while the application was not running, this will be called shortly after application:didFinishLaunchingWithOptions.
        // Use this method to release any resources that were specific to the discarded scenes, as they will not return.
    }

    

    func removeAllGames() {
        DatabaseManager.clearDatabase()
        
        //Remove contents of games folder
        let path = getUserGamesDirectory()
        FileManager.default.clearFolderAtPath(path: path)
    }

}

