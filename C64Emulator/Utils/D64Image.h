//
//  D64.h
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

#import <UIKit/UIKit.h>


@interface D64Item : NSObject

@property (nonatomic, strong) NSString *name;
@property (nonatomic, assign) int size;
@property (nonatomic, strong) NSString *type;

@end

@interface D64Image : NSObject

@property (nonatomic, strong) NSString *error;
@property (nonatomic, strong) NSString *diskName;
@property (nonatomic, strong) NSString *diskId;
@property (nonatomic, assign) int blocksFree;
@property (nonatomic, strong) NSMutableArray *items;

- (void) readDiskDirectory:(NSString *)fileName;
@end
