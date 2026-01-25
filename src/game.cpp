#include "game.h"
namespace game {
     binding_s g_bindings[] = {
   { "+scores",             -1,           -1,           -1,           -1           },
   { "+speed",              K_SHIFT,      -1,           -1,           -1           },
   { "+forward",            K_UPARROW,    -1,           -1,           -1           },
   { "+back",               K_DOWNARROW,  -1,           -1,           -1           },
   { "+moveleft",           ',',          -1,           -1,           -1           }, // 44 = ','
   { "+moveright",          '.',          -1,           -1,           -1           }, // 46 = '.'
   { "+moveup",             K_SPACE,      -1,           -1,           -1           },
   { "+movedown",           'c',          -1,           -1,           -1           }, // 99 = 'c'
   { "+left",               K_LEFTARROW,  -1,           -1,           -1           },
   { "+right",              K_RIGHTARROW, -1,           -1,           -1           },
   { "+strafe",             K_ALT,        -1,           -1,           -1           },
   { "+lookup",             K_PGDN,       -1,           -1,           -1           },
   { "+lookdown",           K_DEL,        -1,           -1,           -1           },
   { "+mlook",              '/',          -1,           -1,           -1           }, // 47 = '/'
   { "centerview",          K_END,        -1,           -1,           -1           },
   { "toggleads",           -1,           -1,           -1,           -1           },
   { "leaveads",            -1,           -1,           -1,           -1           },
   { "+binoculars",         -1,           -1,           -1,           -1           },
   { "+breath_binoculars",  -1,           -1,           -1,           -1           },
   { "+melee",              -1,           -1,           -1,           -1           },
   { "+holdbreath",         -1,           -1,           -1,           -1           },
   { "+melee_breath",       -1,           -1,           -1,           -1           },
   { "+prone",              -1,           -1,           -1,           -1           },
   { "+stance",             -1,           -1,           -1,           -1           },
   { "lowerstance",         -1,           -1,           -1,           -1           },
   { "raisestance",         -1,           -1,           -1,           -1           },
   { "togglecrouch",        -1,           -1,           -1,           -1           },
   { "toggleprone",         -1,           -1,           -1,           -1           },
   { "goprone",             -1,           -1,           -1,           -1           },
   { "gocrouch",            -1,           -1,           -1,           -1           },
   { "+gostand",            -1,           -1,           -1,           -1           },
   { "weaponslot primary",  '1',          -1,           -1,           -1           }, // 49 = '1'
   { "weaponslot primaryb", '2',          -1,           -1,           -1           }, // 50 = '2'
   { "+frag",               -1,           -1,           -1,           -1           },
   { "+smoke",              -1,           -1,           -1,           -1           },
   { "+attack",             K_CTRL,       -1,           -1,           -1           },
   { "weapprev",            K_MWHEELDOWN, -1,           -1,           -1           },
   { "weapnext",            K_MWHEELUP,   -1,           -1,           -1           },
   { "+activate",           -1,           -1,           -1,           -1           },
   { "+reload",             -1,           -1,           -1,           -1           },
   { "+leanleft",           -1,           -1,           -1,           -1           },
   { "+leanright",          -1,           -1,           -1,           -1           },
   { "+usereload",          -1,           -1,           -1,           -1           },
   { "screenshot",          -1,           -1,           -1,           -1           },
   { "screenshotJPEG",      -1,           -1,           -1,           -1           },
   // Add modded ones here
   { "+sprintbreath",      -1,           -1,           -1,           -1           },
   { "+sprint",      -1,           -1,           -1,           -1           }
    };

     binding_s* GetModdedBindings(uint32_t& current_size) {
         current_size = ARRAYSIZE(g_bindings);
         return g_bindings;
     }

}