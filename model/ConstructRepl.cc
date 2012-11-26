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

using namespace std;
using namespace model;
using namespace parser;
using namespace builder;
using namespace crack::ext;
using namespace llvm;

//global:
llvm::FunctionPassManager *FPM =0;
llvm::Module *module = 0;
std::string modname;
llvm::ExecutionEngine *JIT = 0;

void jit_init();
void jit_shutdown();
void init_llvm_target(); // must be at bottom of file.
static void* resolve_external(const std::string& name);


// from kaleidoscope, codegen for a double F(); anonymous function declaration.
llvm::Function *AnonFunctionPrototype_Codegen(std::string Name, llvm::Module* TheModule) {

  // Make the function type:  double(double,double) etc.
    std::vector<llvm::Type*> Doubles(0,
                                     llvm::Type::getDoubleTy(llvm::getGlobalContext()));
    llvm::FunctionType *FT = FunctionType::get(llvm::Type::getDoubleTy(getGlobalContext()),
                                       Doubles, false);
  
    llvm::Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule);
      
  return F;
}



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

    // XXX TODO: what should these context parameters actually be???`

     Context* prior = prevContext ? prevContext : rootContext.get();

     ContextPtr context =
         new Context(*builder, Context::module, prior,
                     new GlobalNamespace(rootContext->ns.get(), canName)
                     );
     context->toplevel = true;

     string name = "wisecrack_lineno_";
     ModuleDefPtr modDef = context->createModule(canName, name);

     // setup to optimize the code at -O2 
     init_llvm_target();

     llvm::ExecutionEngine* jitExecEngine = bldr->getExecEng();
     // This is the default optimization used by the JIT.
     llvm::FunctionPassManager *FPM = new llvm::FunctionPassManager(bldr->module);

     FPM->add(new llvm::TargetData(*jitExecEngine->getTargetData()));
     FPM->add(llvm::createCFGSimplificationPass());

     FPM->add(llvm::createJumpThreadingPass());
     FPM->add(llvm::createPromoteMemoryToRegisterPass());
     FPM->add(llvm::createInstructionCombiningPass());
     FPM->add(llvm::createCFGSimplificationPass());
     FPM->add(llvm::createScalarReplAggregatesPass());

     FPM->add(llvm::createLICMPass());
     FPM->add(llvm::createJumpThreadingPass());

     FPM->add(llvm::createGVNPass());
     FPM->add(llvm::createSCCPPass());

     FPM->add(llvm::createAggressiveDCEPass());
     FPM->add(llvm::createCFGSimplificationPass());
     FPM->add(llvm::createVerifierPass());

     FPM->doInitialization();

     // from pure-lang:
     // Install a fallback mechanism to resolve references to the runtime, on
     // systems which do not allow the program to dlopen itself.
     jitExecEngine->InstallLazyFunctionCreator(resolve_external);

     bool eager_jit = true;
     jitExecEngine->DisableLazyCompilation(eager_jit);



     printf("*** starting wisecrack jit-compilation based interpreter, ctrl-d to exit. ***\n");

     //
     // main Read-Eval-Print loop
     //
     while(!r.done()) {
         r.nextlineno();
         r.prompt(stdout);
         r.read(stdin);
         if (r.getLastReadLineLen()==0) continue;

         std::stringstream src;
         src << r.getLastReadLine();

         std::stringstream anonFuncName;
         anonFuncName << name << r.lineno() << "_";

         std::string path = r.getPrompt();

         try {

             // example from kaleidoscope: evaluate into an anonymous function.

#if 0
            llvm::Function *TheFunction = AnonFunctionPrototype_Codegen(anonFuncName.str(), 
                                                                        bldr->module); 
            assert(TheFunction);
  
            // Create a new basic block to start insertion into.
            BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "entry", TheFunction);
            bldr->builder.SetInsertPoint(BB);
#endif

            Toker toker(src, path.c_str());
            Parser parser(toker, context.get());
            parser.parse();

            

            //bldr->innerCloseModule(*context, modDef.get());
            //verifyModule(*bldr->module, llvm::PrintMessageAction);
            if (!bldr->builder.GetInsertBlock()->getTerminator())
                bldr->builder.CreateRetVoid();


            // do I need to add an implicit 'using' of this new module?

            // optimize

            llvm::Function* f = bldr->func;

            // Validate the generated code, checking for consistency.
            llvm::verifyFunction(*f);

            // optimize the function
            FPM->run(*f);

            // JIT the function, returning a function pointer.
            int (*fptr)() = (int (*)())jitExecEngine->getPointerToFunction(f);
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

    // XXX TODO: put FPM in an RAII resource so it gets cleaned up automatically.
    if (FPM) {
        // It seems that this is needed for LLVM 3.1 and later.
        FPM->doFinalization();
        delete FPM;
        FPM = 0;
    }

}




extern "C"
void crack_function_unresolved()
{
    // how to do this in crack?
    //pure_throw(unresolved_exception());
}

// XXX TODO: port this from Pure -> Crack
static void* resolve_external(const std::string& name)
{
  /* If we come here, the dynamic loader has already tried everything to
     resolve the function, so instead we just print an error message and
     return a dummy function which raises a Pure exception when called. In any
     case that's better than aborting the program (which is what the JIT will
     do when we return NULL here). */
    std::cout.flush();
  cerr << "error trying to resolve external: "
       << (name.compare(0, 2, "$$") == 0?"<<anonymous>>":name) << '\n';
  return (void*)crack_function_unresolved;
}

void   init_llvm_target();

void jit_init() {

    // fragments to model from pure-lang code by A. Graef.
  // Initialize the JIT.

  using namespace llvm;

  init_llvm_target();
  module = new llvm::Module(modname, llvm::getGlobalContext());

  llvm::EngineBuilder factory(module);
  factory.setEngineKind(llvm::EngineKind::JIT);
  factory.setAllocateGVsWithCode(false);

  llvm::TargetOptions Opts;

  Opts.GuaranteedTailCallOpt = true;

  factory.setTargetOptions(Opts);

  JIT = factory.create();
  assert(JIT);

  FPM = new FunctionPassManager(module);


  // Set up the optimizer pipeline. Start with registering info about how the
  // target lays out data structures.
  FPM->add(new TargetData(*JIT->getTargetData()));
  // Promote allocas to registers.
  FPM->add(createPromoteMemoryToRegisterPass());
  // Do simple "peephole" optimizations and bit-twiddling optimizations.
  FPM->add(createInstructionCombiningPass());
  // Reassociate expressions.
  FPM->add(createReassociatePass());
  // Eliminate common subexpressions.
  FPM->add(createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  FPM->add(createCFGSimplificationPass());

  // It seems that this is needed for LLVM 3.1 and later.
  FPM->doInitialization();


  // Install a fallback mechanism to resolve references to the runtime, on
  // systems which do not allow the program to dlopen itself.
  JIT->InstallLazyFunctionCreator(resolve_external);

  bool eager_jit = true;
  JIT->DisableLazyCompilation(eager_jit);



}

void jit_shutdown() {

    if (JIT) {
        delete JIT;
        JIT = 0;
    }

  if (FPM) {
    // It seems that this is needed for LLVM 3.1 and later.
    FPM->doFinalization();
    delete FPM;
    FPM = 0;
  }

}


/* advice from pure-lang code by A. Graef:
   Make sure to make this import of TargetSelect the very last thing,
   since TargetSelect.h pulls in LLVM's config.h file which may stomp on our own config
   settings! */

// LLVM 3.0 or later
#include <llvm/Support/TargetSelect.h>
//include <llvm/Target/TargetSelect.h>

void init_llvm_target()
{
  llvm::InitializeNativeTarget();
}

