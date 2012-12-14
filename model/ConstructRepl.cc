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
bool continueOnSpecial(wisecrack::Repl& r, Context* context, Builder* bdr);

// cleanup ctrl-c or syntax error aborted input.
void cleanup_unfinished_input(Builder* bdr, Context* ctx, ModuleDef* mod);

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
//   I think the best way to handle redefinition is just to give the context a flag
//   to avoid the error.  The cleanest way to deal with it is if that flag is set,
//   do a delete on the underlying LLVM "rep" objects.  That needs to be something
//   passed in through the FuncDef object and actually implemented in the BFuncDef
//   object, though, and I don't think it's crucial.  For a repl I wouldn't be too
//   concerned about leakage.
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

int Construct::runRepl(Context* arg_ctx, ModuleDef* arg_modd, Builder* arg_bdr) {

    wisecrack::Repl r;

    // smart pointers
    ModuleDefPtr modDef;
    ContextPtr   context;
    BuilderPtr   builder;
    GlobalNamespacePtr local_compile_ns;

    // point to passed in arguments, unless none given, in which
    // case they will point to the smart pointers above.
    ModuleDef* mod = arg_modd;
    Context*   ctx = arg_ctx;
    Builder*   bdr = arg_bdr;

    // do we need to start or close a function before entering repl
    bool closeFunc = false;


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
        string canName = "wisecrack_";
        
        // create the builder and context for the repl.
        builder = rootBuilder->createChildBuilder();
        builderStack.push(builder);
        
        Context* prior = rootContext.get();
        
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
            ns_start_point = (ctx->ns.get())->markTransactionStart();
            
            // READ
            r.nextlineno();
            r.reset_prompt_to_default();
            r.prompt(stdout);
            r.read(stdin); // can throw
            if (r.getLastReadLineLen()==0) continue;

            if (continueOnSpecial(r,ctx,bdr)) continue;

            // EVAL

            // beginSection() must come *after* the continueOnSpecial() 
            // call. Otherwise we begin multiple times and leave
            // dangling half-finished sections lying around. :-(
            bdr->beginSection(*ctx,mod);
            sectionStarted = true;
            
            r.reset_src_to_empty();
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

            // PRINT: TODO. for now use dm or dump at repl. or -d at startup.

        } catch (const wisecrack::ExceptionCtrlC &ex) {
            printf(" [ctrl-c]\n");
            doCleanup = true;

        } catch (const spug::Exception &ex) {
            cerr << ex << endl;
            doCleanup = true;

        } catch (char const* msg) {
            cerr << msg << endl;
            printf("press ctrl-d to exit\n");
            doCleanup = true;

        } catch (...) {
            if (!uncaughtExceptionFunc)
                cerr << "Uncaught exception, no uncaught exception handler!" <<
                    endl;
            else if (!uncaughtExceptionFunc())
                cerr << "Unknown exception caught." << endl;
            doCleanup = true;
        }
        
        if (doCleanup) {
            if (sectionStarted) {
                cleanup_unfinished_input(bdr, ctx, mod);
            }
            (ctx->ns.get())->undoTransactionTo(ns_start_point);
        }

    } // end while

    if (ctx) { ctx->repl = 0; }
    builderStack.pop();
    rootBuilder->finishBuild(*ctx);
    if (rootBuilder->options->statsMode)
        stats->setState(ConstructStats::end);
    return 0;
}


/**
 *  continueOnSpecial(): a runRepl helper.
 *    returns true if we have a special repl command and should keep looping.
 *    Currently this implements just the two dump commands: 'dump' and 'dm'
 *    that display the global/local namespace; hence are proxies for PRINT.
 */
bool continueOnSpecial(wisecrack::Repl& r, Context* context, Builder* bdr) {

    // check for ctrl-d and nothing else
    if (0==strcmp("\n",r.getLastReadLine())) {
        if (r.done()) {
            return true;
        }
    }

    // special commands

    const char* p   = r.getTrimmedLastReadLine();
    const char* end = r.getLastReadLine() + r.getLastReadLineLen();

    if (0==strcmp(".q",p) ||
        0==strcmp(".quit",p)) {
        // quitting time.
        r.setDone();
        return true;

    } else if (0==strcmp(".dump",p)) {
        // dump: do full global dump of all namespaces.
        context->dump();
        return true;
    } else if (0==strcmp(".debug",p)) {
        // up the debugging level
        int d = r.debuglevel();
        ++d;
        printf("debug level: %d\n",d);
        r.set_debuglevel(d);
        return true;
    } else if (0==strcmp(".undebug",p)) {
        // reduce debugging
        int d = r.debuglevel();
        --d;
        if (d < 0) { d=0; }
        printf("debug level: %d\n",d);
        r.set_debuglevel(d);
        return true;
    }
    else if (0==strcmp(".dn",p) ||
             0==strcmp(".ls",p)) {
        // dn: dump namespace, local only
        context->short_dump();
        return true;

    } else if (0==strcmp(".dc",p)) {
        // dc: dump bit-code
        bdr->dump();
        return true;

    } else if (0==strncmp(".rm ", p, 4) && strlen(p) > 4) {
        // rm sym : remove symbol sym from namespace

        const char* sym = p + 4;

        while(isspace(*sym) && sym < end) { ++sym;  }

        // confirm we have an argument sym to delete
        if (sym >= end) {
            printf("error using .rm: no symbol-to-delete specified.\n");
            return true;
        }

        VarDefPtr var = context->ns->lookUp(sym);

        if (!var) {
            printf("error using .rm: could not locate symbol '%s' to delete.\n", sym);
            return true;
        }

        printf(".rm deleting symbol '%s' at 0x%lx.\n", sym, (unsigned long)var.get());

        context->ns->removeDef(var.get());

        return true;

    } else if (0==strcmp(".", p) || 
               (0==strncmp(". ", p, 2)) && strlen(p) > 2) {
        // . sym : print sym
        // .     : print last sym

        const char* sym = p + 2;

        if (0==strcmp(".", p)) {
            // just . by itself on a line: print the
            // last thing added to the namespace, if we can.
          
            sym = context->ns->lastTxSymbol();
            if (NULL == sym) {
                printf("no last symbol to display.\n");
                return true;
            }
        }

        VarDefPtr vcout = context->ns->lookUp("cout");

        std::string cmd;
        if (!vcout) {
            //printf("warn on .p without cout: could not find cout, doing: import crack.io cout;\n");
            cmd += "import crack.io cout; ";
        }
        
        cmd += "cout `";
        cmd += sym;
        cmd += " = $(";
        cmd += sym;
        cmd += ")\n`";
        r.set_next_line(cmd.c_str());
        
        // return false because we *want* to execute the cout print now.
        return false;
    }

    if (0==strcmp(".history",p)) {
        if (r.history.size() == 1) return true; // first cmd.

        wisecrack::Repl::histlist::iterator it = r.history.begin();
        wisecrack::Repl::histlist::iterator en = r.history.end();
        --en; // don't print the last .history (duh).
        long line = 1;
        for(; it != en; ++it, ++line) {
            //printf("%ld: %s\n", line, it->c_str());

            // easier to copy and paste *without* line numbers!
            printf("%s\n", it->c_str());
        }
        return true;
    }
    else
    if (0==strcmp(".help",p)) {
        printf("wisecrack repl help:\n"
               "\n"
               "  .help    = show this hint page\n"
               "  .dn      = dump wisecrack namespace (also .ls)\n"
               "  .dump    = dump global namespace (everything)\n"
               "  .dc      = display code (LLVM bitcode)\n"
               "  .debug   = increase the debug messages\n"
               "  .undebug = reduce debug messages (0 => off)\n"
               "  .quit    = quit repl (also .q)\n"
               "  ctrl-d   = EOF also quits\n"
               "  ctrl-c   = interrupt line and return to the repl\n"
               "  .rm sym  = remove symbol sym from namespace\n"
               "  . sym    = print sym on cout. Does 'import crack.io cout;' if necessary.\n"
               "  .        = print last symbol made (skips internals with ':' prefix)\n"
               "  .history = display command line history\n"
               "\n"
               );
              
        return true;
    }
    return false;
}


void cleanup_unfinished_input(Builder* bdr, Context* ctx, ModuleDef* mod) {
    if (ctx->repl && ctx->repl->debuglevel() > 0) {
        printf(" [cleaning up unfinished line]\n");
    }
    bdr->eraseSection(*ctx,mod);
}
