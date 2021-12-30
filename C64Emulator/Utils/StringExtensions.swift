//
//  StringExtensions.swift
//  MyVault
//
//  Created by Andy Qua on 14/10/2016.
//  Copyright © 2016 British Airways. All rights reserved.
//

import Foundation

extension String {
    func appendingPathComponent(_ string: String) -> String {
        return (self as NSString).appendingPathComponent(string)
    }

    func lastPathComponent() -> String {
        return (self as NSString).lastPathComponent
    }
    
    func deletingPathExtension() -> String {
        return (self as NSString).deletingPathExtension
    }
    
    func pathExtension() -> String {
        return (self as NSString).pathExtension
    }
}

extension FileManager {
    func clearFolderAtPath(path: String) -> Void {
        
        let fileManager = FileManager.default
        do {
            let filePaths = try fileManager.contentsOfDirectory(atPath: path)
            for filePath in filePaths {
                do {
                    try fileManager.removeItem(atPath: path.appendingPathComponent(filePath))
                } catch let error as NSError {
                    print("Could not remove file - \(filePath): \(error.debugDescription)")
                }
            }
        } catch let error as NSError {
            print("Could not clear temp folder: \(error.debugDescription)")
        }
    }
}
