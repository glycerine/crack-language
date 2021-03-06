// Copyright 2011 Shannon Weyrick <weyrick@mozek.us>
// Copyright 2011 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _builder_llvm_FunctionTypeDef_h_
#define _builder_llvm_FunctionTypeDef_h_

#include "model/Context.h"
#include "BTypeDef.h"
#include <string>

namespace llvm {
    class Type;
}

namespace builder {
namespace mvll {

class FunctionTypeDef : public BTypeDef {
public:
    FunctionTypeDef(model::TypeDef *metaType,
                    const std::string &name,
                    llvm::Type *rep
                    );

    // specializations of array types actually create a new type
    // object.
    virtual model::TypeDef *getSpecialization(model::Context &context,
                                              TypeVecObj *types
                                              );

};


} // end namespace builder::vmll
} // end namespace builder

#endif
