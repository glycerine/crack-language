// Copyright 2012 Jason E. Aten
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _wisecrack_SpecialCmd_h_
#define _wisecrack_SpecialCmd_h_

#include "wisecrack/repl.h"

namespace model {
    class Context;
}

namespace builder {
    class Builder;
}

namespace wisecrack {

    /**
     *  SpecialCmdProcessor: summary: detect and handle the special repl
     *  commands that start with a dot (or prefix char.
     *
     *  continueOnSpecial(): a runRepl helper.
     *    returns true if we have a special repl command and should keep looping.
     *    See the help option at the end of this function for a full
     *    description of special repl commands.
     *
     *  Note that a side effect of this call may be to completely replace
     *    the current r.getLastReadLine() value. For example, the print command
     *    does this kind of substitution (the print command is
     *    '.' by itself on a line, or if it has been changed from the default
     *    using repl.setReplCmdStart(), then whatever repl.getReplCmdStart()
     *    returns).
     */

    class SpecialCmdProcessor {
    public:
        bool continueOnSpecial(wisecrack::Repl& r, 
                               model::Context *context, 
                               builder::Builder *bdr);
        
    };

}

#endif
