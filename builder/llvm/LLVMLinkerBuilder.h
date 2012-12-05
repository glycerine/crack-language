// Copyright 2011-2012 Shannon Weyrick <weyrick@mozek.us>
// Copyright 2011-2012 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _builder_LLVMLinkerBuilder_h_
#define _builder_LLVMLinkerBuilder_h_

#include "LLVMBuilder.h"
#include <vector>

namespace llvm {
    class Linker;
    class BasicBlock;
}

namespace builder {
namespace mvll {

class BModuleDef;
SPUG_RCPTR(LLVMLinkerBuilder);

class LLVMLinkerBuilder : public LLVMBuilder {
    private:
        typedef std::vector<builder::mvll::BModuleDef *> ModuleListType;

        llvm::Linker *linker;
        ModuleListType *moduleList;
        llvm::BasicBlock *mainInsert;
        std::vector<std::string> sharedLibs;

        ModuleListType *addModule(BModuleDef *mp);
        llvm::Function *emitAggregateCleanup(llvm::Module *module);

    protected:
        virtual void engineFinishModule(model::Context &context,
                                        BModuleDef *moduleDef
                                        );
        virtual void fixClassInstRep(BTypeDef *type);

    public:
        LLVMLinkerBuilder(void) : linker(0),
                                  moduleList(0),
                                  mainInsert(0),
                                  sharedLibs() { }

        virtual void *getFuncAddr(llvm::Function *func);

        virtual void finishBuild(model::Context &context);

        virtual BuilderPtr createChildBuilder();

        virtual model::ModuleDefPtr createModule(model::Context &context,
                                                 const std::string &name,
                                                 const std::string &path,
                                                 model::ModuleDef *owner
                                                 );

        virtual void initializeImport(model::ModuleDef*,
                                      const model::ImportedDefVec &symbols,
                                      bool annotation);

        virtual void *loadSharedLibrary(const std::string &name);

        virtual void closeModule(model::Context &context,
                                 model::ModuleDef *module
                                 );

        virtual bool isExec() { return false; }

        virtual model::ModuleDefPtr materializeModule(
            model::Context &context,
            const std::string &canonicalName,
            model::ModuleDef *owner
        );

        virtual llvm::ExecutionEngine *getExecEng() const;

        virtual void startSection(model::Context &context,
                                  model::ModuleDef *moduleDef,
                                  const std::string& next_section_name) {}
        
        virtual void closeSection(model::Context &context,
                                  model::ModuleDef *module
                                  ) {}
        

};

} } // namespace

#endif
