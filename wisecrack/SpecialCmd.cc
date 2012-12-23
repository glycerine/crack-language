// Copyright 2012 Jason E. Aten
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "model/Construct.h"

#include <stdlib.h>
#include <sstream>
#include "parser/Parser.h"
#include "parser/ParseError.h"
#include "parser/Toker.h"
#include "spug/check.h"
#include "builder/Builder.h"
#include "ext/Module.h"
#include "model/Context.h"
#include "model/GlobalNamespace.h"
#include "model/ModuleDef.h"
#include "model/StrConst.h"
#include "model/TypeDef.h"
#include "model/VarDef.h"
#include "wisecrack/repl.h"
#include "wisecrack/SpecialCmd.h"
#include "builder/llvm/LLVMJitBuilder.h"
#include "builder/llvm/LLVMBuilder.h"
#include "model/OverloadDef.h"
#include "model/OrderedIdLog.h"

#include <llvm/LinkAllPasses.h> // enable .dc cast and display code for symbol

using namespace std;
using namespace model;
using namespace parser;
using namespace builder;
using namespace crack::ext;
using namespace crack::util;

namespace wisecrack {

/**
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
bool SpecialCmdProcessor::continueOnSpecial(wisecrack::Repl& r, 
                                            model::Context *context, 
                                            builder::Builder *bdr) {

    // check for ctrl-d and nothing else
    if (!strcmp("\n", r.getLastReadLine())) {
        if (r.done()) {
            return true;
        }
    }

    // special commands?
    const char *rcmd = r.getTrimmedLastReadLine();
    const char *p    = r.replCmd(rcmd);
    const char *end  = r.getLastReadLine() + r.getLastReadLineLen();

    // the special command starter: typically "." or "\"
    const char *s = r.getReplCmdStart();

    if (!p) return false;

    // INVAR: we have a repl command, and p points to it.
    if (0 == strlen(p) // "." alone
        ||  (' ' == *p && strlen(p) > 1)) { // or with sym to print
        // . sym : print sym
        // .     : print last sym

        const char *sym = p + 1;
        std::string cmd;

        VarDefPtr vcout = context->ns->lookUp("cout");
        if (!vcout) {
            cmd += "import crack.io cout; ";
        }
        
        if (!strlen(p)) {
            // just . (or getReplCmdStart()) by itself on a line: print the
            // last thing added to the namespace, if we can.

            TypeDef *tdef = 0;
            sym = context->ns->lastTxSymbol(&tdef);
            if (NULL == sym) {
                printf("no last symbol to display.\n");
                if (!vcout) {
                    // still bring in cout
                    r.setNextLine(cmd.c_str());
                    return false;
                }
                return true;
            }

            // We can't display classes or operators at the moment.
            // Try to avoid crashing by detecting
            // unhandled cases here.
            if (tdef) {
                std::string x = tdef->name;
                const char *y = x.c_str();
                size_t pxl = x.size();
                if (!strcmp(y + pxl - 4,"meta")
                    || !strncmp(y, "function", 8)
                    ) {
                    printf("cannot display '%s' of type '%s' at the moment.\n", sym, y);
                    if (cmd.size()) {
                        // allow import of cout no matter.
                        r.setNextLine(cmd.c_str());
                        return false;
                    }
                    return true;
                }
            }
        }

        cmd += "cout `";
        cmd += sym;
        cmd += " = $(";
        cmd += sym;
        cmd += ")\n`;";
        r.setNextLine(cmd.c_str());
        
        // return false because we *want* to execute the cout print now.
        return false;
    }


    if (!strcmp("histoff", p)) {
        // turn off logging history to .crkhist
        r.histoff();
        printf("command logging to file '.crkhist' now off.\n");
        return true;

    } else if (!strcmp("histon", p)) {
        // turn on logging history to .crkhist
        r.histon();
        if (r.hist()) printf("logging subsequent commands to file '.crkhist'.\n");
        return true;
    }
    
    if (!strcmp("q", p) ||
        !strcmp("quit", p)) {
        // quitting time.
        r.setDone();
        return true;

    } else if (!strcmp("dump", p)) {
        // dump: do full global dump of all namespaces.
        context->dump();
        return true;

    } else if (!strcmp("debug", p)) {
        // up the debugging level
        int d = r.debugLevel();
        ++d;
        printf("debug level: %d\n", d);
        r.set_debugLevel(d);
        return true;
    } else if (!strcmp("undebug", p)) {
        // reduce debugging
        int d = r.debugLevel();
        --d;
        if (d < 0) { d=0; }
        printf("debug level: %d\n", d);
        r.set_debugLevel(d);
        return true;
    }
    else if (!strcmp("dn", p) ||
             !strcmp("ls", p)) {
        // dn: dump namespace, local only
        context->shortDump();
        return true;

    } else if (!strcmp("dc", p)) {
        // dc: dump bit-code
        bdr->dump();
        return true;

    } else if (!strncmp("dc ", p, 3) && strlen(p) > 3) {
        // dc: dump llvm-code for a symbol

        const char *sym = p + 3;
        while(isspace(*sym) && sym < end) { ++sym;  }
        
        // confirm we have an argument
        if (sym >= end) {
            printf("error using %sdc: no symbol-to-display-code for specified.\n", s);
            return true;
        }

        VarDefPtr var = context->ns->lookUp(sym);

        if (!var) {
            printf("error using %sdc: could not locate symbol '%s' to display code for.\n", s, sym);
            return true;
        }

        // functions
        model::OverloadDef *odef     = dynamic_cast<model::OverloadDef*>(var.get());

        // classes
        builder::mvll::BTypeDef *btd = dynamic_cast<builder::mvll::BTypeDef*>(var.get());

        // unimplemented (as of yet) type
        builder::mvll::BTypeDef *tp  = dynamic_cast<builder::mvll::BTypeDef*>(var->type.get());

        if (odef) {
            printf("display code for '%s' function(s):\n", sym);

            model::OverloadDef::FuncList::iterator it = odef->beginTopFuncs();
            model::OverloadDef::FuncList::iterator en = odef->endTopFuncs();
            for (; it != en; ++it) {

                FuncDefPtr fdp  = *it;
                FuncDef *  func = fdp.get();

                builder::mvll::LLVMBuilder *llvm_bdr = dynamic_cast<builder::mvll::LLVMBuilder*>(bdr);
                
                if (func && llvm_bdr) {
                    
                    builder::mvll::LLVMBuilder::ModFuncMap::iterator it = llvm_bdr->moduleFuncs.find(func);
                    
                    if (it != llvm_bdr->moduleFuncs.end()) {
                        
                        // display the llvm code for just one function.
                        llvm::Function *f = it->second;
                        llvm::outs() << static_cast<llvm::Value&>(*f);
                        
                    } else {
                        printf("%sdc error: could not find FuncDef -> llvm::Function* "
                               "mapping for '%s' in builder.\n", s, sym);                        
                    }
                }
            }

        } else if (btd) {
            printf("%sdc '%s' error: displaying classes not yet implemented.\n", s, sym);

        } else {
            printf("%sdc '%s' error: I don't know how to dump code for type '%s' yet.\n",
                   s, sym, tp ? tp->name.c_str() : "*unknown type*");

        }

        return true;

    } else if (!strncmp("rm ", p, 3) && strlen(p) > 3) {
        // rm sym : remove symbol sym from namespace

        const char *sym = p + 3;
        while(isspace(*sym) && sym < end) { ++sym;  }

        // confirm we have an argument sym to delete
        if (sym >= end) {
            printf("error using %srm: no symbol-to-delete specified.\n", s);
            return true;
        }

        VarDefPtr var = context->ns->lookUp(sym);

        if (!var) {
            printf("error using %srm: could not locate symbol '%s' to delete.\n", s, sym);
            return true;
        }

        printf("rm deleting symbol '%s' at 0x%lx.\n", sym, (unsigned long)var.get());

        context->ns->removeDefAllowOverload(var.get());
        return true;

    } else if (!strncmp("!", p, 1)) {
        const char *cmd = p + 1;
        int res = system(cmd);
        return true;

    } else if (!strncmp(".", p, 1)) {
        // source a file
        const char *sourceme = p + 1;
        while(isspace(*sourceme) && sourceme < end) { ++sourceme;  }

        // validate file
        FILE *f = fopen(sourceme,"r");
        if (!f) {
            printf("error in %s. source file: could not open file '%s'\n",
                   s, sourceme);
            return true;
        }

        // shovel data into r.src stream
        const int b = 4096;
        char buf[b];
        size_t m = 0;
        while(!feof(f)) {
            bzero(buf, b);
            m += fread(buf, 1, b-1, f);
            r.src << buf;
        }

        if (r.debugLevel() > 0) {
            printf("sourced %ld bytes from '%s'\n", m, sourceme);
        }

        r.setNextLine("");
        return false; // run from r.src.
    }
    else
    if (!strcmp("history", p)) {
        if (r.history.size() <= 1) return true; // first cmd.

        wisecrack::Repl::histlist::iterator it = r.history.begin();
        wisecrack::Repl::histlist::iterator en = r.history.end();
        --en; // don't print the last .history (duh).
        long line = 1;
        for(; it != en; ++it, ++line) {
            //printf("%ld: %s\n", line, it->c_str());

            // easier to copy and paste *without *line numbers!
            printf("%s\n", it->c_str());
        }
        return true;
    }
    else if (!strncmp("prefix ", p, 7) && strlen(p) > 7) {
        const char *pre = p + 7;
        printf("setting repl command prefix to '%s'\n", pre);
        r.setReplCmdStart(pre);
        return true;
    }

    if (!strcmp("help", p)) {

        printf("wisecrack repl help: ['%s' prefix starts repl commands]\n"
               "  %shelp    = show this hint page\n"
               "  %sls      = dump repl namespace (also %sdn)\n"
               "  %sdump    = dump global namespace (everything)\n"
               "  %sdc      = display code (LLVM bitcode)\n"
               "  %sdebug   = increase the debug messages\n"
               "  %sundebug = reduce debug messages (0 => off)\n"
               "  %squit    = quit repl (also %sq)\n"
               "  ctrl-d   = EOF also quits\n"
               "  ctrl-c   = interrupt line and return to the repl\n"
               "  %srm sym  = remove symbol sym from namespace\n"
               "  %s sym    = print sym on cout. Does 'import crack.io cout;' if necessary.\n"
               "  %s        = print last symbol made (skips internals with ':' prefix)\n"
               "  %shistory = display command line history\n"
               "  %s!cmd    = call system(cmd), executing cmd in a shell.\n"
               "  %s. file  = read and execute commands from file.\n"
               "  %sprefix c = set repl command prefix to c ['%s' now].\n"
               "  %shiston  = save history to .crkhist file (vs %shistoff) [%s now]\n",
               s,s,s,s,
               s,s,s,s,
               s,s,s,s,
               s,s,s,s,

               s,s,s,s,

               r.hist() ? "ON" :  "OFF"
               );
              
        return true;
    }

    printf("unrecognized special repl command '%s%s'."
           " Type %shelp for hints.\n", s, p, s);
    return true;
}

} // end namespace wisecrack

