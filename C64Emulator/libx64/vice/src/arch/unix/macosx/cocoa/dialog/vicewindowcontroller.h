/*
 * vicewindowcontroller.h - generic base window controller
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


#import <UIKit/UIKit.h>

@interface VICEWindowController : NSWindowController
{
    BOOL showOnDefault;
    NSString *title;
}

-(id)initWithWindowNibName:(NSString *)name title:(NSString *)title showOnDefault:(BOOL)show;
-(id)initWithWindow:(NSWindow *)window showOnDefault:(BOOL)show;

- (void)toggleWindow:(id)sender;
- (void)checkMenuItem:(NSMenuItem *)item;

- (BOOL)setWindowVisibilityFromUserDefaults;
- (void)storeWindowVisibilityToUserDefaults;

@end

