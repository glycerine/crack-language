// Copyright 2010 Google Inc, Shannon Weyrick <weyrick@mozek.us>

#ifndef _builder_llvm_BFuncDef_h_
#define _builder_llvm_BFuncDef_h_

#include "model/FuncDef.h"
#include "LLVMBuilder.h"
#include "BTypeDef.h"

namespace llvm {
    class Function;
}

namespace builder {
namespace mvll {

SPUG_RCPTR(BFuncDef);

class BFuncDef : public model::FuncDef {
public:
    // this holds the function object for the last module to request
    // it.
    llvm::Function *rep;

    // for a virtual function, this is the vtable slot position.
    unsigned vtableSlot;

    // for a virtual function, this holds the ancestor class that owns
    // the vtable pointer
    BTypeDefPtr vtableBase;

    BFuncDef(FuncDef::Flags flags, const std::string &name,
             size_t argCount
             ) :
    model::FuncDef(flags, name, argCount),
    rep(0),
    vtableSlot(0) {
    }

    /**
     * Returns the module-specific Function object for the function.
     */
    llvm::Function *getRep(LLVMBuilder &builder);

};

} // end namespace builder::vmll
} // end namespace builder

#endif