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
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>  // link in the JIT
#include <llvm/Module.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Intrinsics.h>
#include <llvm/Support/Debug.h>

using namespace std;
using namespace model;
using namespace parser;
using namespace builder;
using namespace crack::ext;
using namespace llvm;


void Construct::runRepl(Context* prevContext) {

    wisecrack::Repl r;

    std::stringstream cn;
    cn << "wisecrack:" << &r;
    string canName = model::modNameFromFile(cn.str());
    
    // create the builder and context for the repl.
    BuilderPtr builder = rootBuilder->createChildBuilder();
    builderStack.push(builder);

    builder::mvll::LLVMJitBuilder* bldr = dynamic_cast<builder::mvll::LLVMJitBuilder*>(builder.get());
    assert(bldr);
    if (0==bldr) {
        cerr << "internal error: wisecrack repl requires LLVMJitBuilder" << endl;
        exit(1);
    }

    Context* prior = prevContext ? prevContext : rootContext.get();

    ContextPtr context =
        new Context(*builder, Context::local, prior,
                    new GlobalNamespace(rootContext->ns.get(), canName)
                    );
    context->toplevel = true;

    string name = "wisecrack-input";
    ModuleDefPtr modDef = context->createModule(canName, name);

    // setup to optimize the code at -O2 
    llvm::InitializeNativeTarget();
    llvm::ExecutionEngine* execEngine = bldr->getExecEng();
    // This is the default optimization used by the JIT.
    llvm::FunctionPassManager *O2 = new llvm::FunctionPassManager(bldr->module);

    O2->add(new llvm::TargetData(*execEngine->getTargetData()));
    O2->add(llvm::createCFGSimplificationPass());
    
    O2->add(llvm::createJumpThreadingPass());
    O2->add(llvm::createPromoteMemoryToRegisterPass());
    O2->add(llvm::createInstructionCombiningPass());
    O2->add(llvm::createCFGSimplificationPass());
    O2->add(llvm::createScalarReplAggregatesPass());
    
    O2->add(llvm::createLICMPass());
    O2->add(llvm::createJumpThreadingPass());
    
    O2->add(llvm::createGVNPass());
    O2->add(llvm::createSCCPPass());
    
    O2->add(llvm::createAggressiveDCEPass());
    O2->add(llvm::createCFGSimplificationPass());
    O2->add(llvm::createVerifierPass());
            
    O2->doInitialization();


    
    printf("*** starting wisecrack jit-compilation based interpreter, ctrl-d to exit. ***\n");

    while(!r.done()) {
        r.nextlineno();
        r.prompt(stdout);
        r.read(stdin);
        if (r.getLastReadLineLen()==0) continue;

        std::stringstream src;
        src << r.getLastReadLine();

        std::string path = r.getPrompt();

        try {

            Toker toker(src, path.c_str());
            Parser parser(toker, context.get());

            parser.parse();

            verifyModule(*bldr->module, llvm::PrintMessageAction);

            // optimize

            llvm::Function* f = bldr->func;
            //llvm::verifyFunction(*f);
            O2->run(*f);

            // JIT the function, returning a function pointer.
            int (*fptr)() = (int (*)())execEngine->getPointerToFunction(f);
            SPUG_CHECK(fptr, "repl-jit error: no address for function " << string(f->getName()));

            // execute the jit-ed code.
            fptr();


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
