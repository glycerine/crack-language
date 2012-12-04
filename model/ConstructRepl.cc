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
#include "builder/util/CacheFiles.h"
#include "builder/util/SourceDigest.h"
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

// helper. at end of file.
bool continueOnSpecial(wisecrack::Repl& r, Context* context);


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

    // are we starting fresh, or dropping into a previously defined
    //  ctx, modd, bdr triple?
    if (arg_ctx) {
        if (0==arg_modd || 0==arg_bdr) {
            cerr << "internal error in Construct::runRepl: all three {ctx,modd,bdr} must be set or must all be null." << endl;
            assert(0);
            return 1;
        }

        // the previous script will have
        // already closed the last function and run it
        // so we just need to start a new section here,
        // without issuing another closeModule (as closeSection() would)
        // which would run the script a second time.
        bdr->beginSection(*ctx,mod);

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

    } // end else start fresh context/module/builder


    //printf("*** wisecrack 0.7.2, ctrl-d to exit. ***\n");

    // move outside while so it can be persisted with previous
    // lines for multi-line input.
    std::stringstream src;
    bool multiline = false;

    //
    // main Read-Eval-Print loop
    //
    while(!r.done()) {

        // READ
        r.nextlineno();
        r.prompt(stdout);
        r.read(stdin);
        if (r.getLastReadLineLen()==0) continue;

        if (continueOnSpecial(r,ctx)) continue;

        // EVAL
        src << r.getLastReadLine();

        std::string path = r.getPrompt();

        try {

            Toker toker(src, path.c_str());
            Parser parser(toker, ctx);
            parser.parse();            

            // stats collection
            StatState sState(ctx, ConstructStats::parser, mod);
            if (sState.statsEnabled()) {
                stats->incParsed();
            }

            // currently, closeSection also runs the code.
            // And then it opens a new section, i.e. it starts
            // a new module level function.
            bdr->closeSection(*ctx,mod);
            

            // PRINT: TODO. for now use dm or dump at repl. or -d at startup.

        } catch (const parser::EndStreamMidToken& ex) {
            // tell the end-of-while loop src handling
            //  to keep the current src contents, so we
            //  can add the next line and try again.
            multiline = true;

        } catch (const spug::Exception &ex) {
            cerr << ex << endl;

        } catch (...) {
            if (!uncaughtExceptionFunc)
                cerr << "Uncaught exception, no uncaught exception handler!" <<
                    endl;
            else if (!uncaughtExceptionFunc())
                cerr << "Unknown exception caught." << endl;
        }

        // clear eof on src
        src.clear();
        if (multiline) {
            src << "\n";
            // go to beginning for gets
            src.seekg(0);
            r.set_prompt("...");
        } else {
            // reset src to be empty
            src.str(std::string());
            r.reset_prompt_to_default();
        }
        multiline = false;

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
bool continueOnSpecial(wisecrack::Repl& r, Context* context) {

    // special commands
    if (0==strcmp("dump",r.getTrimmedLastReadLine())) {
        context->dump();
        return true;
    }
    else if (0==strcmp("dm",r.getTrimmedLastReadLine())) {
        // don't print parent namespaces (dump without the (p)arent namespace and without (u)pper namesapces)
        context->short_dump();
        return true;
    }
    return false;
}

