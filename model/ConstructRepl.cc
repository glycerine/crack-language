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

// cleanup ctrl-c aborted input.
void cleanup_unfinished_input(Builder* bdr, Context* ctx, ModuleDef* mod);


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
 
        // already closed the last function and ran it
        // so we just need to start a new section here,
        // without issuing another closeModule (as closeSection() would)
        // which would run the script a second time.
        // bdr->beginSection(*ctx,mod);


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
                              local_compile_ns.get());
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

            // must come *after* the continueOnSpecial() call.
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
    if (0==strcmp(".q",r.getTrimmedLastReadLine()) ||
        0==strcmp(".quit",r.getTrimmedLastReadLine())) {
        // quitting time.
        r.setDone();
        return true;

    } else if (0==strcmp(".dump",r.getTrimmedLastReadLine())) {
        // dump: do full global dump of all namespaces.
        context->dump();
        return true;
    }
    else if (0==strcmp(".dn",r.getTrimmedLastReadLine())) {
        // dn: dump namespace, local only
        context->short_dump();
        return true;

    } else if (0==strcmp(".dc",r.getTrimmedLastReadLine())) {
        // dc: dump bit-code
        bdr->dump();
        return true;
    }
    return false;
}


void cleanup_unfinished_input(Builder* bdr, Context* ctx, ModuleDef* mod) {
    printf(" [cleaning up unfinished line]\n");
    bdr->eraseSection(*ctx,mod);
    
    // XXX TODO: figure out how to erase any newly half-finished classes
    
    //    bdr->beginSection(*ctx,mod);
}
