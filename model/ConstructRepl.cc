// Copyright 2012 Jason Aten <j.e.aten@gmail.com>
// Copyright 2011-2012 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "Construct.h"

#include <sys/stat.h>
#include <fstream>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <sstream>

#include "parser/Parser.h"
#include "parser/ParseError.h"
#include "parser/Toker.h"
#include "spug/check.h"
#include "builder/Builder.h"
#include "ext/Module.h"
#include "Context.h"
#include "GlobalNamespace.h"
#include "ModuleDef.h"
#include "StrConst.h"
#include "TypeDef.h"
#include "compiler/init.h"
#include "util/CacheFiles.h"
#include "util/SourceDigest.h"
#include "wisecrack/repl.h"
#include "builder/llvm/LLVMJitBuilder.h"
#include "builder/llvm/LLVMBuilder.h"
#include "model/OverloadDef.h"
#include "model/OrderedIdLog.h"

#include <llvm/LLVMContext.h>
#include <llvm/LinkAllPasses.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>  // link in the JIT
#include <llvm/Module.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Intrinsics.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/IRBuilder.h>

using namespace std;
using namespace model;
using namespace parser;
using namespace builder;
using namespace crack::ext;
using namespace llvm;

// helpers. at end of file.
bool continueOnSpecial(wisecrack::Repl& r, Context *context, Builder *bdr);

// cleanup ctrl-c or syntax error aborted input.
void cleanupUnfinishedInput(Builder *bdr, Context *ctx, ModuleDef *mod);


namespace {

void cleanupUnfinishedInput(Builder *bdr, Context *ctx, ModuleDef *mod) {
    if (ctx->repl && ctx->repl->debugLevel() > 0) {
        printf(" [cleaning up unfinished line]\n");
    }
    bdr->eraseSection(*ctx, mod);

    bdr->purgeUnterminatedFunctions(*ctx, mod);
}


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
 *    using repl.set_repl_cmd_start(), then whatever repl.get_repl_cmd_start()
 *    returns).
 */
bool continueOnSpecial(wisecrack::Repl& r, Context *context, Builder *bdr) {

    // check for ctrl-d and nothing else
    if (!strcmp("\n",r.getLastReadLine())) {
        if (r.done()) {
            return true;
        }
    }

    // special commands?
    const char *rcmd = r.getTrimmedLastReadLine();
    const char *p    = r.repl_cmd(rcmd);
    const char *end  = r.getLastReadLine() + r.getLastReadLineLen();

    // the special command starter: typically "." or "\"
    const char *s = r.get_repl_cmd_start();

    if (!p) return false;

    // INVAR: we have a repl command, and p points to it.
    if (0 == strlen(p) // "." alone
        ||  (' ' == *p && strlen(p) > 1)) { // or with a named sym to print
        // . sym : print sym
        // .     : print last sym

        const char *sym = p + 1;
        std::string cmd;

        if (0==strlen(p)) {
            // just . by itself on a line: print the
            // last thing added to the namespace, if we can.

            VarDefPtr vcout = context->ns->lookUp("cout");
            if (!vcout) {
                cmd += "import crack.io cout; ";
            }
        
            TypeDef *tdef = 0;
            sym = context->ns->lastTxSymbol(&tdef);
            if (NULL == sym) {
                printf("no last symbol to display.\n");
                if (!vcout) {
                    // still bring in cout
                    r.set_next_line(cmd.c_str());
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
                    || 0==strncmp(y, "function", 8)
                    ) {
                    printf("cannot display '%s' of type '%s' at the moment.\n",sym, y);
                    if (cmd.size()) {
                        // allow import of cout no matter.
                        r.set_next_line(cmd.c_str());
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
        r.set_next_line(cmd.c_str());
        
        // return false because we *want* to execute the cout print now.
        return false;
    }


    if (!strcmp("histoff",p)) {
        // turn off logging history to .crkhist
        r.histoff();
        printf("command logging to file '.crkhist' now off.\n");
        return true;

    } else if (!strcmp("histon",p)) {
        // turn on logging history to .crkhist
        r.histon();
        printf("logging subsequent commands to file '.crkhist'.\n");
        return true;
    }
    
    // otherwise, add to .crkhist file if that is on.
    r.loghist(p);

    if (!strcmp("q",p) ||
        !strcmp("quit",p)) {
        // quitting time.
        r.setDone();
        return true;

    } else if (!strcmp("dump",p)) {
        // dump: do full global dump of all namespaces.
        context->dump();
        return true;

    } else if (!strcmp("debug",p)) {
        // up the debugging level
        int d = r.debugLevel();
        ++d;
        printf("debug level: %d\n",d);
        r.set_debugLevel(d);
        return true;
    } else if (!strcmp("undebug",p)) {
        // reduce debugging
        int d = r.debugLevel();
        --d;
        if (d < 0) { d=0; }
        printf("debug level: %d\n",d);
        r.set_debugLevel(d);
        return true;
    }
    else if (!strcmp("dn",p) ||
             !strcmp("ls",p)) {
        // dn: dump namespace, local only
        context->short_dump();
        return true;

    } else if (!strcmp("dc",p)) {
        // dc: dump bit-code
        bdr->dump();
        return true;

    } else if (0==strncmp("dc ", p, 3) && strlen(p) > 3) {
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

    } else if (0==strncmp("rm ", p, 3) && strlen(p) > 3) {
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

    } else if (0==strncmp("!", p, 1)) {
        const char *cmd = p + 1;
        int res = system(cmd);
        return true;

    } else if (0==strncmp(".", p, 1)) {
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
            bzero(buf,b);
            m += fread(buf, 1, b-1, f);
            r.src << buf;
        }

        if (r.debugLevel() > 0) {
            printf("sourced %ld bytes from '%s'\n", m, sourceme);
        }

        r.set_next_line("");
        return false; // run from r.src.
    }
    else
    if (!strcmp("history",p)) {
        if (r.history.size() == 1) return true; // first cmd.

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
    else if (0==strncmp("prefix ", p, 7) && strlen(p) > 7) {
        const char *pre = p + 7;
        printf("setting repl command prefix to '%s'\n", pre);
        r.set_repl_cmd_start(pre);
        return true;
    }

    if (!strcmp("help",p)) {

        printf("wisecrack repl help: ['%s' prefix starts repl commands]\n"
               "  %shelp    = show this hint page\n"
               "  %sdn      = dump wisecrack namespace (also %sls)\n"
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
    return false;
}

} // end anonymous namespace for continueOnSpecial and cleanupUnfinishedInput


// Status: the repl is now quite usable. Multiline input works well.
//         Cleanup after syntax error in functions or expressions is there.
//         The remaining big items are outlined below.
//
// XXX TODO list 12 Dec 2012
//
//   _ cleanup aborted (due to syntax error or other error) class definitions.
//       possibly with a temp namespace that is merge upon correctness, or
//       discarded upon error.
//
// But if you're strongly motivated to do correct cleanup, I wonder if we could
// make use of a temporary repl ModuleDef object and then transfer its
// definitions to a longer lived one?  Then we could drive the rollback
// functionality from ModuleDef -> BModuleDef -> LLVM.  -- mmuller
//
//   _ allow re-defining of functions/variables/classes at the repl
//
//    Update (14 Dec 2012): Works but leaks. Redefinition at the repl just
//      clears the symbol from the namespace; it doesn't do proper 
//      cleanup like Michael describes below, but it works for now
//      (albiet with the accompanying leak of llvm rep objects).
//
//    Michael's thoughts: (7 Dec 2012)
//
// I think the best way to handle redefinition is just to give the context a
// flag to avoid the error.  The cleanest way to deal with it is if that 
// flag is set, do a delete on the underlying LLVM "rep" objects.
// That needs to be something passed in through the FuncDef object and 
// actually implemented in the BFuncDef object, though, and I don't think
// it's crucial.  For a repl I wouldn't be too concerned about leakage.
//
//   _ .crackrc startup in cwd or ~/.crackd : automatically run these scripts
//      upon startup unless suppressed with a startup flag.

/**
 * runRepl(): experimental jit-based interpreter. Project name: wisecrack.
 *
 * @param ctx : if non-null, use this context
 * @param mod : if non-null, use this module
 * @param bdr : if non-null, use this builder
 *
 * To work well, either all three parameters should be supplied at
 *  once (e.g. after runScript), or they should all be null.
 */

int Construct::runRepl(Context *arg_ctx, ModuleDef *arg_modd, Builder *arg_bdr) {

    wisecrack::Repl r;

    // smart pointers
    ModuleDefPtr modDef;
    ContextPtr   context;
    BuilderPtr   builder;
    GlobalNamespacePtr local_compile_ns;

    // point to passed in arguments, unless none given, in which
    // case they will point to the smart pointers above.
    ModuleDef *mod = arg_modd;
    Context *ctx = arg_ctx;
    Builder *bdr = arg_bdr;

    // are we starting fresh, or dropping into a previously defined
    //  ctx, modd, bdr triple?
    if (arg_ctx) {
        if (0==arg_modd || 0==arg_bdr) {
            cerr << "internal error in Construct::runRepl: all three"
                " {ctx,modd,bdr} must be set or must all be null." 
                 << endl;
            assert(0);
            return 1;
        }
 
        // already closed the last function and ran it.
        // so we just need to start a new section inside the main loop,
        // without issuing another closeModule (as closeSection() would)
        // which would run the script a second time.

        ctx->repl = &r;

    } else {

        // starting fresh context/module/builder

        // canonical name for module representing input from repl
        string canName = ".repl";
        
        // create the builder and context for the repl.
        builder = rootBuilder->createChildBuilder();
        builderStack.push(builder);
        
        Context *prior = rootContext.get();
        
        local_compile_ns = new GlobalNamespace(prior->ns.get(),canName);
        
        context = new Context(*builder,
                              Context::module, 
                              prior,
                              local_compile_ns.get(),
                              0,
                              &r);
        context->toplevel = true;
        
        bool cached = false;

#if 0 // we don't have cacheMode yet in 0.7.1, that comes later.
        if (rootContext->construct->cacheMode)
            builder::initCacheDirectory(rootBuilder->options.get(), *this);
#endif

        modDef = context->createModule(canName, canName);

        // set the pointers we will use below
        mod = modDef.get();
        ctx = context.get();
        bdr = builder.get();

        r.set_builder(bdr);

        // close :main - now we open a new section after receiving
        // each individual command.
        bdr->closeSection(*ctx,mod);

    } // end else start fresh context/module/builder

    bool sectionStarted = false;
    bool doCleanup = false;
    Namespace::Txmark ns_start_point;

    printf("wisecrack read-eval-print-loop [type .help for hints]\n");
    
    //
    // main Read-Eval-Print loop
    //
    while(!r.done()) {

        try {
            doCleanup = false;
            sectionStarted = false;

            // track cleanups, so we don't removeFromParent() twice and crash.
            r.goneSet.clear(); 

            // READ
            r.reset_src_to_empty();
            r.nextlineno();
            r.reset_prompt_to_default();
            r.prompt(stdout);
            r.read(stdin); // can throw
            if (r.getLastReadLineLen()==0) continue;

            if (continueOnSpecial(r, ctx, bdr)) continue;

            // EVAL

            // beginSection() must come *after* the continueOnSpecial() 
            // call. Otherwise we begin multiple times and leave
            // dangling half-finished sections lying around. :-(

            bdr->beginSection(*ctx,mod);
            ns_start_point = (ctx->ns.get())->markTransactionStart();
            
            sectionStarted = true;
            
            r.src << r.getLastReadLine();

            std::string path = r.getPrompt();

            Toker toker(r.src, path.c_str());
            Parser parser(toker, ctx);
            parser.setAtRepl(r);
            parser.parse();

            // stats collection
            StatState sState(ctx, ConstructStats::parser, mod);
            if (sState.statsEnabled()) {
                stats->incParsed();
            }

            // We can't split up the close and the exec,
            // because import statements themselves will
            // try to closeModule, and they won't know
            // that the exec is missing; i.e. they require
            // the closeModule() to do a run() on their own 
            // module function's main function
            // to finish the import.

            // closeSection() finishes the module and runs 
            // the last function constructed in it.

            bdr->closeSection(*ctx,mod);

            // PRINT: provided as a short-cut special command '.' currently.

        } catch (const wisecrack::ExceptionCtrlC &ex) {
            printf(" [ctrl-c]\n");
            doCleanup = true;

        } catch (const spug::Exception &ex) {
            cerr << ex << endl;
            doCleanup = true;

        } catch (char const *msg) {
            cerr << msg << endl;
            printf("press ctrl-d to exit\n");
            doCleanup = true;

            /*        } catch (...) {
            if (!uncaughtExceptionFunc)
                cerr << "Uncaught exception, no uncaught exception handler!" <<
                    endl;
            else if (!uncaughtExceptionFunc())
                cerr << "Unknown exception caught." << endl;
            doCleanup = true;
            */
        }

        if (doCleanup) {
            if (sectionStarted) {
                cleanupUnfinishedInput(bdr, ctx, mod);
            }
            (ctx->ns.get())->undoTransactionTo(ns_start_point, &r);
        }

    } // end while

    if (ctx) { ctx->repl = 0; }
    builderStack.pop();
    rootBuilder->finishBuild(*ctx);
    if (rootBuilder->options->statsMode)
        stats->setState(ConstructStats::end);
    return 0;
}

