// Copyright 2010 Shannon Weyrick <weyrick@mozek.us>
// Copyright 2010-2011 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "BFuncDef.h"
#include "BTypeDef.h"

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/GlobalVariable.h>
#include <llvm/Module.h>
#include "model/Context.h"
#include "model/OverloadDef.h"
#include "PlaceholderInstruction.h"
#include "VTableBuilder.h"

using namespace model;
using namespace std;
using namespace builder::mvll;
using namespace llvm;

void BTypeDef::getDependents(std::vector<TypeDefPtr> &deps) {
    for (IncompleteChildVec::iterator iter = incompleteChildren.begin();
         iter != incompleteChildren.end();
         ++iter
         )
        deps.push_back(iter->first);
}

// add all of my virtual functions to 'vtb'
void BTypeDef::extendVTables(VTableBuilder &vtb) {
    // find all of the virtual functions
    for (Namespace::VarDefMap::iterator varIter = beginDefs();
         varIter != endDefs();
         ++varIter
         ) {

        BFuncDef *funcDef = BFuncDefPtr::rcast(varIter->second);
        if (funcDef && (funcDef->flags & FuncDef::virtualized)) {
            vtb.add(funcDef);
            continue;
        }

        // check for an overload (if it's not an overload, assume that
        // it's not a function).  Iterate over all of the overloads at
        // the top level - the parent classes have
        // already had their shot at extendVTables, and we don't want
        // their overloads to clobber ours.
        OverloadDef *overload =
            OverloadDefPtr::rcast(varIter->second);
        if (overload)
            for (OverloadDef::FuncList::iterator fiter =
                  overload->beginTopFuncs();
                 fiter != overload->endTopFuncs();
                 ++fiter
                 )
                if ((*fiter)->flags & FuncDef::virtualized)
                    vtb.add(BFuncDefPtr::arcast(*fiter));
    }
}

/**
 * Create all of the vtables for 'type'.
 *
 * @param vtb the vtable builder
 * @param name the name stem for the VTable global variables.
 * @param vtableBaseType the global vtable base type.
 * @param firstVTable if true, we have not yet discovered the first
 *  vtable in the class schema.
 */
void BTypeDef::createAllVTables(VTableBuilder &vtb, const string &name,
                                bool firstVTable
                                ) {
    // if this is VTableBase, we need to create the VTable.
    // This is a special case: we should only get here when
    // initializing VTableBase's own vtable.
    if (this == vtb.vtableBaseType)
        vtb.createVTable(this, name, true);

    // iterate over the base classes, construct VTables for all
    // ancestors that require them.
    for (TypeVec::iterator baseIter = parents.begin();
         baseIter != parents.end();
         ++baseIter
         ) {
        BTypeDef *base = BTypeDefPtr::arcast(*baseIter);

        // if the base class is VTableBase, we've hit bottom -
        // construct the initial vtable and store the first vtable
        // type if this is it.
        if (base == vtb.vtableBaseType) {
            vtb.createVTable(this, name, firstVTable);

            // otherwise, if the base has a vtable, create all of its
            // vtables
        } else if (base->hasVTable) {
            if (firstVTable)
                base->createAllVTables(vtb, name, firstVTable);
            else
                base->createAllVTables(vtb,
                                       name + ':' + base->getFullName(),
                                       firstVTable
                                       );
        }

        firstVTable = false;
    }

    // we must either have ancestors with vtables or be vtable base.
    assert(!firstVTable || this == vtb.vtableBaseType);

    // add my functions to their vtables
    extendVTables(vtb);
}

void BTypeDef::addBaseClass(BTypeDef *base) {
    ++fieldCount;
    parents.push_back(base);
    if (base->hasVTable)
        hasVTable = true;
}

void BTypeDef::addPlaceholder(PlaceholderInstruction *inst) {
    assert(!complete && "Adding placeholder to a completed class");
    placeholders.push_back(inst);
}

BTypeDef *BTypeDef::findFirstVTable(BTypeDef *vtableBaseType) {

    // special case - if this is VTableBase, it is its own first vtable 
    // (normally it is the first class to derive from VTableBase that is the 
    // first vtable).
    if (this == vtableBaseType)
        return this;

    // check the parents
    for (TypeVec::iterator parent = parents.begin();
         parent != parents.end();
         ++parent
         )
        if (parent->get() == vtableBaseType) {
            return this;
        } else if ((*parent)->hasVTable) {
            BTypeDef *par = BTypeDefPtr::arcast(*parent);
            return par->findFirstVTable(vtableBaseType);
        }

    cerr << "class is " << name << endl;
    assert(false && "Failed to find first vtable");
}

GlobalVariable *BTypeDef::getClassInstRep(Module *module,
                                          ExecutionEngine *execEng
                                          ) {
    if (classInst->getParent() == module) {
        return classInst;
    } else {
        GlobalVariable *gvar = 
            cast<GlobalVariable>(
                module->getGlobalVariable(classInst->getName())
            );
        if (!gvar) {
            gvar = new GlobalVariable(*module, 
                                      classInst->getType()->getElementType(), 
                                      true, // is constant
                                      GlobalValue::ExternalLinkage,
                                      0, // initializer: null for externs
                                      classInst->getName()
                                      );

            // if there's an execution engine, do the pointer hookup
            if (execEng) {
                void *p = execEng->getPointerToGlobal(classInst);
                execEng->addGlobalMapping(gvar, p);
            }
        }
        
        return gvar;
    }
}

void BTypeDef::addDependent(BTypeDef *type, Context *context) {
    incompleteChildren.push_back(pair<BTypeDefPtr, ContextPtr>(type, context));
}

void BTypeDef::fixIncompletes(Context &context) {
    // construct the vtable if necessary
    if (hasVTable) {
        VTableBuilder vtableBuilder(
            dynamic_cast<LLVMBuilder*>(&context.builder),
            BTypeDefPtr::arcast(context.construct->vtableBaseType)
        );
        createAllVTables(
            vtableBuilder, 
            ".vtable." + context.parent->ns->getNamespaceName() + "." + name
        );
        vtableBuilder.emit(this);
    }
    
    // fix-up all of the placeholder instructions
    for (vector<PlaceholderInstruction *>::iterator iter = 
            placeholders.begin();
         iter != placeholders.end();
         ++iter
         )
        (*iter)->fix();
    placeholders.clear();
    
    // fix up all incomplete children
    for (IncompleteChildVec::iterator iter = incompleteChildren.begin();
         iter != incompleteChildren.end();
         ++iter
         )
        iter->first->fixIncompletes(*iter->second);
    incompleteChildren.clear();
    
    complete = true;
}
