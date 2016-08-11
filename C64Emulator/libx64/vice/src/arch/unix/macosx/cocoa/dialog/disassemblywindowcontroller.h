/*
 * disassemblywindowcontroller.h - disassembly window controller
 *
 * Written by
 *  Christian Vogelgsang <chris@vogelgsang.org>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#import "debuggerwindowcontroller.h"

#define MAX_SIZE        65536
#define LINES_PER_CALL  64

@interface DisassemblyWindowController : DebuggerWindowController
{
    IBOutlet NSTableView *memoryTable;

    int    rangeMinAddr;
    int    rangeMaxAddr;
    NSMutableArray *lines;
    
    NSMenuItem *setBreakpointItem;
    NSMenuItem *enableBreakpointItem;
}

-(id)initWithMemSpace:(int)memSpace;
-(void)realizeExtend:(NSNotification *)notification;

@end
