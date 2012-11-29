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

//global:
llvm::FunctionPassManager *FPM =0;
llvm::Module *module = 0;
std::string modname;
llvm::ExecutionEngine *JIT = 0;

void jit_init();
void jit_shutdown();
void init_llvm_target(); // must be at bottom of file.
static void* resolve_external(const std::string& name);


// XXX test / experimental 88888888
#if 0
void construct_jit_and_callfunction1(builder::mvll::LLVMJitBuilder* bldr) {


    LLVMAppendBasicBlockInContext

        LLVMPositionBuilderAtEnd(labelBlock)

        // create blocks for the true and false conditions
        LLVMContext &lctx = getGlobalContext();
    BasicBlock *trueBlock = BasicBlock::Create(lctx, tLabel, func);

    BBranchpointPtr result = new BBranchpoint(
                                              BasicBlock::Create(lctx, fLabel, func)
                                              );

    if (condInCleanupFrame) context.createCleanupFrame();
    cond->emitCond(context);
    Value *condVal = lastValue; // condition value
    if (condInCleanupFrame) context.closeCleanupFrame();
    result->block2 = builder.GetInsertBlock(); // condition block
    lastValue = condVal;
    builder.CreateCondBr(lastValue, trueBlock, result->block);

    // repoint to the new ("if true") block
    builder.SetInsertPoint(trueBlock);

    

}
#endif 


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


void init_fpm(llvm::FunctionPassManager* FPM, llvm::ExecutionEngine* jitExecEngine, bool eager_jit) {

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

    //bool eager_jit = true;
    jitExecEngine->DisableLazyCompilation(eager_jit);

}



//
// from ~/pkg/crack/github/crack-language/builder/llvm/LLVMBuilder.cc
//   derived from   void finishClassType(Context &context, BTypeDef *classType) {
#if 0
void generate(Context &context, BTypeDef *classType) {

    // for the kinds of things we're about to do, we need a global block
    // for functions to restore to, and for that we need a function and
    // module.
    LLVMContext &lctx = getGlobalContext();
    LLVMBuilder &builder = dynamic_cast<LLVMBuilder &>(context.builder);
    // builder.module should already exist from .builtin module
    assert(builder.module);
    vector<Type *> argTypes;
    FunctionType *voidFuncNoArgs =
        FunctionType::get(Type::getVoidTy(lctx), argTypes, false);
    Function *func = Function::Create(voidFuncNoArgs,
                                      Function::ExternalLinkage,
                                      "__builtin_init__",
                                      builder.module
                                      );
    func->setCallingConv(llvm::CallingConv::C);
    builder.builder.SetInsertPoint(BasicBlock::Create(lctx,
                                                      "__builtin_init__",
                                                      builder.func
                                                      )
                                   );

    // add "Class"
    int lineNum = __LINE__ + 1;
    string temp("    byteptr name;\n"
                "    uint numBases;\n"
                "    array[Class] bases;\n"
                "    bool isSubclass(Class other) {\n"
                "        if (this is other)\n"
                "            return (1==1);\n"
                "        uint i;\n"
                "        while (i < numBases) {\n"
                "            if (bases[i].isSubclass(other))\n"
                "                return (1==1);\n"
                "            i = i + uint(1);\n"
                "        }\n"
                "        return (1==0);\n"
                "    }\n"
                "}\n"
                );

    // create the class context - different scope from parent context.
    ContextPtr classCtx =
        context.createSubContext(Context::instance, classType);

    CompositeNamespacePtr ns = new CompositeNamespace(classType,
                                                      context.ns.get()
                                                      );
    ContextPtr lexicalContext =
        classCtx->createSubContext(Context::composite, ns.get());
    BBuilderContextData *bdata =
        BBuilderContextData::get(lexicalContext.get());

    istringstream src(temp);
    try {
        parser::Toker toker(src, "<builtin>", lineNum);
        parser::Parser p(toker, lexicalContext.get());
        p.parseClassBody();
    } catch (parser::ParseError &ex) {
        std::cerr << ex << endl;
        assert(false);
    }

    // let the "end class" emitter handle the rest of this.
    context.builder.emitEndClass(*classCtx);

    // close off the block.
    builder.builder.CreateRetVoid();
}

#endif

/**
 * runRepl(): experiemental jit-based interpreter. 
 *
 *             NB: flattened as much as possible for ease of
 *             experimentation. This is not 'production code', which would be much
 *             much more grouped into subroutines, not be one big function etc. The very
 *             shallow flow here means we have everything we need at our
 *             fingertips while conducting trial/error codebase exploration; and
 *             the llvm calls being made are obvious instead of buried.
 */
void Construct::runRepl() {

    wisecrack::Repl r;

    std::stringstream cn;
    cn << "wisecrack:" << &r;
    //    string canName = model::modNameFromFile(cn.str());
    string canName = "wisecrack_";

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

    Context* prior = rootContext.get();

    std::string local_ns_cname = "wisec_local_ns";
    std::string local_cns_cname = "wisec_local_compile_ns";
    LocalNamespacePtr local_ns = new LocalNamespace(prior->ns.get(),local_ns_cname);
    LocalNamespacePtr local_compile_ns = new LocalNamespace(prior->ns.get(),local_cns_cname);

    // with Context::composite, we could see our own function definitions.
    //ContextPtr context = new Context(*builder, Context::composite, prior, local_ns.get(), local_compile_ns.get());                                      

    // with Context::module, stuff is clearly in the wisecrack_ namesapce, as 'dum' shows. But we
    // cannot call our own function definitions.

    // in neither case can we see our own imports.
    // And import crack.io cout; cout `hi\n`; crashes on a null funcCtx->builderData at LLVMBuilder.cc:439

    ContextPtr context = new Context(*builder, Context::module, prior, local_ns.get(), local_compile_ns.get());                                      
    context->toplevel = true;

    string name = "wisecrack_lineno_";

    ModuleDefPtr modDef = context->createModule(canName, name);

    // we want wisecrack_:main function to be closed.
    bldr->closeSection(*context, modDef.get());
    verifyModule(*bldr->module, llvm::PrintMessageAction);

    // setup to optimize the code at -O2 
    init_llvm_target();

    llvm::ExecutionEngine* jitExecEngine = bldr->getExecEng();
    // This is the default optimization used by the JIT.
    llvm::FunctionPassManager *FPM = new llvm::FunctionPassManager(bldr->module);
    bool eager_jit = true;
    init_fpm(FPM, jitExecEngine, eager_jit);

    // XXX test / experimental
    //construct_jit_and_callfunction1(bldr);


    printf("*** starting wisecrack jit-compilation based interpreter, ctrl-d to exit. ***\n");

    LLVMContext &lctx = getGlobalContext();

    //
    // main Read-Eval-Print loop
    //
    while(!r.done()) {
        r.nextlineno();
        r.prompt(stdout);
        r.read(stdin);
        if (r.getLastReadLineLen()==0) continue;

        // special commands
        if (0==strcmp("dump",r.getTrimmedLastReadLine())) {
            context->dump();
            continue;
        }
        else if (0==strcmp("dm",r.getTrimmedLastReadLine())) {
            // don't print parent namespaces (dump without the (p)arent namespace and without (u)pper namesapces)
            context->short_dump();
            continue;
        }

        std::stringstream src;
        //         src << "void repl_" << r.lineno() << "() { ";
        src << r.getLastReadLine();
        //   src << "}";
         
        std::stringstream anonFuncName;
        anonFuncName << name << r.lineno() << "_";

        std::string path = r.getPrompt();
        BasicBlock* BB = 0;
        Function*   func = 0;

        try {

            // create a new context in the same scope
            std::string local_ns_cname_sub = anonFuncName.str() + "_ns_cname_subctx";
            std::string local_cns_cname_sub = anonFuncName.str() + "_cns_cname_subctx";
            LocalNamespacePtr local_ns_sub = new LocalNamespace(local_ns.get(), local_ns_cname_sub);
            LocalNamespacePtr local_compile_ns_sub = new LocalNamespace(local_compile_ns.get(), local_cns_cname_sub);
            //             ContextPtr context = new Context(*builder, Context::composite, prior, local_ns.get(), local_compile_ns.get());                                      
            std::string afn = anonFuncName.str();
            ContextPtr lexicalContext = context->createSubContext(Context::composite, local_ns_sub.get(), &afn, local_compile_ns_sub.get()); 
            lexicalContext->toplevel = true;


            // by default we start in function wisecrack_:main aka bldr->func;

            // example from kaleidoscope: evaluate into an anonymous function.
            assert(modDef.get() == bldr->module);

#if 0
            llvm::Function *TheFunction = AnonFunctionPrototype_Codegen(anonFuncName.str(), 
                                                                        bldr->module); 
            assert(TheFunction);
  
            // Create a new basic block to start insertion into.
            BB = BasicBlock::Create(lexicalContext.get(), "entry", TheFunction);
            bldr->builder.SetInsertPoint(BB);
#endif

            using namespace builder::mvll;
            LLVMBuilder &builder2 = dynamic_cast<LLVMBuilder &>(lexicalContext->builder);
            assert(&builder2 == builder);
            // builder.module should already exist from .builtin module
            assert(builder.module);

            // generate an anonymous function
            vector<llvm::Type *> argTypes;
            FunctionType *voidFuncNoArgs =
                FunctionType::get(llvm::Type::getVoidTy(lctx), argTypes, false);
            func = Function::Create(voidFuncNoArgs,
                                              Function::ExternalLinkage,
                                              anonFuncName.str().c_str(),
                                              bldr->module
                                              );
            func->setCallingConv(llvm::CallingConv::C);
            BB = BasicBlock::Create(lctx, anonFuncName.str().c_str(), func);
            //BasicBlock *BB = BasicBlock::Create(lexicalContext.get(), anonFuncName.str().c_str(), func);
            //BasicBlock *BB = BasicBlock::Create(lctx, anonFuncName.str().c_str(), builder.func)
            bldr->builder.SetInsertPoint(BB);


            Toker toker(src, path.c_str());
            Parser parser(toker, lexicalContext.get());
            //Parser::ContextStackFrame cstack(parser, lexicalContext.get());
            parser.parse();

            // close off the block.
            bldr->builder.CreateRetVoid();
            

            //assert(bldr->builder.GetInsertBlock()->getTerminator());

            // closeSection() is just a clone (at present) of closeModule()
            // doesn't appear to fix the segfault in getUnwindBlock
            // builder::mvll::BBuilderContextData::getUnwindBlock (this=0x0, func=0xa6ec30) at builder/llvm/BBuilderContextData.cc:37
            // so we comment out for now; also it prevents more than one repl command from being executed.
            //
            // bldr->closeSection(*context, modDef.get());

            // this also doesn't work, also missing bdata obtained by: BBuilderContextData *bdata = BBuilderContextData::get(&context);
            //
            // bldr->closeSection(*lexicalContext, modDef.get());

            //verifyModule(*bldr->module, llvm::PrintMessageAction);


#if 0
            if (!bldr->builder.GetInsertBlock()->getTerminator()) {
                bldr->builder.CreateRetVoid();
                //builder.builder.CreateRetVoid();
                //llvm::Value *RetVal = ConstantFP::get(getGlobalContext(), APFloat(0.0));
                //bldr->builder.CreateRet(RetVal);
            }
#endif
            //llvm::Function* f = TheFunction;
            //llvm::Function* f = bldr->func;
            llvm::Function* f = func;

            // Finish off the function.


            // Validate the generated code, checking for consistency.
            llvm::verifyFunction(*f);

            // optimize the function
            FPM->run(*f);

            // JIT the function, returning a function pointer.
            int (*fptr)() = (int (*)())jitExecEngine->getPointerToFunction(f);
            SPUG_CHECK(fptr, "wisecrack repl-jit error: no address for function " << string(f->getName()));

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

        // cleanup
        jitExecEngine->freeMachineCodeForFunction(func);
        delete func;
        func = 0;



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


