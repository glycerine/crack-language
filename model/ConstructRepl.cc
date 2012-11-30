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



/**
 * runRepl(): experiemental jit-based interpreter. Project name: wisecrack.
 *
 */
void Construct::runRepl() {

    wisecrack::Repl r;

    string canName = "wisecrack_";

    // create the builder and context for the repl.
    BuilderPtr builder = rootBuilder->createChildBuilder();
    builderStack.push(builder);

    Context* prior = rootContext.get();

    GlobalNamespacePtr local_compile_ns = new GlobalNamespace(prior->ns.get(),"wisecrack_global");

    ContextPtr context = new Context(*builder, Context::module, prior, local_compile_ns.get(), local_compile_ns.get());  
    context->toplevel = true;

    string name = "wisecrack_lineno_";
    ModuleDefPtr modDef = context->createModule(canName, name);

    // close :main, so it isn't dangling open.
    builder->closeSection(*context, modDef.get());
    builder->closeModule(*context, modDef.get());


    printf("*** starting wisecrack jit-compilation based interpreter, ctrl-d to exit. ***\n");

    //
    // main Read-Eval-Print loop
    //
    while(!r.done()) {

        // READ
        r.nextlineno();
        r.prompt(stdout);
        r.read(stdin);
        if (r.getLastReadLineLen()==0) continue;

        if (continueOnSpecial(r,context.get())) continue;

        // EVAL
        std::stringstream src;
        src << r.getLastReadLine();
         
        std::stringstream anonFuncName;
        anonFuncName << name << r.lineno() << "_";

        std::string path = r.getPrompt();

        try {

            builder->startSection(*context, modDef.get(),anonFuncName.str());

            Toker toker(src, path.c_str());
            Parser parser(toker, context.get());
            parser.parse();
            
            // currently, closeSection also runs the code--only because, with
            //  two minutes of looking, I couldn't figure out how to return a 
            //  llvm::Function* wrapped in a FuncDefPtr. And we are the only
            //  clients at this point.
            builder->closeSection(*context,modDef.get());
            

            // PRINT: TODO. for now use dm or dump at repl.


        } catch (const spug::Exception &ex) {
            cerr << ex << endl;

        } catch (...) {
            if (!uncaughtExceptionFunc)
                cerr << "Uncaught exception, no uncaught exception handler!" <<
                    endl;
            else if (!uncaughtExceptionFunc())
                cerr << "Unknown exception caught." << endl;
        }

    } // end while


}

