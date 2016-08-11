#include "vice.h"

#include "types.h"

#ifndef MACOSX_COCOA
#if defined(GP2X) || defined(WIZ)
#include "gp2x/uimenu.h"
#else
#ifdef USE_GNOMEUI
#include "x11/gnome/uimenu.h"
#else
#ifdef VMS
#include "xawuimenu.h"
#else
#include "x11/xaw/uimenu.h"
#endif
#endif
#endif
#endif
