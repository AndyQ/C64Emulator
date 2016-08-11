//
//  D64.m
//  C64Emulator
//
//  Created by Andy Qua on 10/08/2016.
//  Copyright © 2016 Andy Qua. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "D64Image.h"
#import "diskimage.h"


// CBM directory file types
static char *ftype[] = {
    "DEL",
    "SEQ",
    "PRG",
    "USR",
    "DEL",
    "CBM",
    "DIR",
    "???"
};

@implementation D64Item

@end


@implementation D64Image

/*
- (void) addLine:(char *)line
{
    // fix encoding
    int i,l=strlen(line);
    for(i=0;i<l;i++) {
        unsigned char c = (unsigned char)line[i];
        // fix non-breaking space
        if(c==0xa0)
            line[i] = 0x20;
        // fix soft hyphen
        else if(c==0xad)
            line[i]=0xed;
    }
    
    // convert Latin1 to NSString
    NSString *lineString = [NSString stringWithCString:line encoding:NSISOLatin1StringEncoding];
    [self.lines addObject:lineString];
}
*/

- (void) readDiskDirectory:(NSString *)fileName
{
    char line[80];
    char name[17];
    
    self.items = [NSMutableArray array];
    
    // open disk image
    const char *fileNameRaw = [fileName cStringUsingEncoding:NSUTF8StringEncoding];
    DiskImage *di = di_load_image((char *)fileNameRaw);
    if(di==NULL) {
        self.error = @"ERROR OPENING DISK IMAGE!";
    } else {
        // read raw disk title
        unsigned char *raw_title = di_title(di);
        
        // fetch disk name
        di_name_from_rawname(name,raw_title);
        
        // fetch disk id
        char did[6];
        int i;
        for(i=0;i<5;i++) {
            did[i] = raw_title[18+i];
        }
        did[5] = 0;
        
        // print directory header
//        sprintf(line,"0 \"%-16s\" %s\n", name, did);
        self.diskName = [NSString stringWithCString:name encoding:NSUTF8StringEncoding];
        self.diskId = [NSString stringWithCString:did encoding:NSUTF8StringEncoding];
//        [self addLine:line];
    }
    
    // read dir contents
    if(di!=NULL) {
        ImageFile *dh = di_open(di, (unsigned char *)"$", T_PRG, "rb");
        if(dh==NULL) {
            self.error = @"ERROR OPENING $ FILE";
        } else {
            unsigned char buffer[254];
            int ok = 1;
            
            // read BAM
            if (di_read(dh, buffer, 254) != 254) {
                self.error = @"ERROR OPENING BAM";
                ok = 0;
            }
            
            // read dir blocks
            int num_entries = 0;
            while(ok && (di_read(dh, buffer, 254) == 254)) {
                int offset;
                for(offset = -2; offset < 254; offset += 32) {
                    if (buffer[offset+2]) {
                        di_name_from_rawname(name, buffer + offset + 5);
                        int type = buffer[offset + 2] & 7;
                        int closed = buffer[offset + 2] & 0x80;
                        int locked = buffer[offset + 2] & 0x40;
                        int size = buffer[offset + 31]<<8 | buffer[offset + 30];
                        // quote name
                        char quotename[19];
                        sprintf(quotename, "\"%s\"", name);
                        // print entry
                        sprintf(line,"%-4d%-18s%c%s%c\n",
                                size, quotename, closed ? ' ' : '*', ftype[type], locked ? '<' : ' ');
                        
                        D64Item *item = [D64Item new];
                        item.name = [NSString stringWithCString:name encoding:NSUTF8StringEncoding];
                        item.size = size;
                        item.type = [NSString stringWithCString:ftype[type] encoding:NSUTF8StringEncoding];
                        [self.items addObject:item];
                        num_entries++;
                        
                        // add ellipsis if too many entries in thumbnail mode
                        if(num_entries==145) {
                            self.error = @"... (TOO MANY!)\n";
                            ok = 0;
                            break;
                        }
                    }
                }
            }
            // print free blocks
            self.blocksFree = di->blocksfree;
        }
        // close dir file
        di_close(dh);
        // free image
        di_free_image(di);
    }

    
}

@end
