// Copyright 2010,2012 Shannon Weyrick <weyrick@mozek.us>
// Copyright 2010-2012 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 


#include "Namespace.h"

#include "spug/check.h"
#include "Context.h"
#include "Deserializer.h"
#include "Expr.h"
#include "OverloadDef.h"
#include "Serializer.h"
#include "VarDef.h"
#include <stdio.h>
#include "model/FuncDef.h"
#include "builder/Builder.h"
#include "builder/llvm/LLVMJitBuilder.h"
#include "builder/llvm/LLVMBuilder.h"
#include <llvm/LinkAllPasses.h>


using namespace std;
using namespace model;
using wisecrack::globalRepl;

// static so we get a global view of added definitions for rollback.
OrderedHash Namespace::orderedForTxn;

void Namespace::storeDef(VarDef *def) {

    if (globalRepl && globalRepl->debuglevel() > 3) {
        printf("NSLOG: '%s' ::storeDef(%s)\n", 
               canonicalName.c_str(), 
               def->getFullName().c_str());
    }

    assert(!FuncDefPtr::cast(def) && 
           "it is illegal to store a FuncDef directly (should be wrapped "
           "in an OverloadDef)");
    defs[def->name] = def;
    orderedForCache.push_back(def);
    OverloadDef* od = dynamic_cast<OverloadDef*>(def);
    if (od) {
        // how can we verify this assumption???
        // assume we want to add the last FuncDef on the FuncList
        OverloadDef::FuncList::iterator b = od->beginTopFuncs();
        OverloadDef::FuncList::iterator e = od->endTopFuncs();
        if (b != e) {
            b=e;
            --b;
            if (0==(*b)->getOwner()) {
                assert(def->getOwner());
                (*b)->setOwner(def->getOwner());
            }
            orderedForTxn.push_back((*b).get(), (*b)->getDisplayName().c_str(), this);
            return;
        } else {
            // no FuncList yet... how do we get our arg names?
            
        }
    }
    orderedForTxn.push_back(def, def->getDisplayName().c_str(), this);
}

void Namespace::noteOverloadForTxn(VarDef* def) {
    orderedForTxn.push_back(def, def->getDisplayName().c_str(), this);
}

VarDefPtr Namespace::lookUp(const std::string &varName, bool recurse) {
    VarDefMap::iterator iter = defs.find(varName);
    if (iter != defs.end()) {

        if (iter->second && globalRepl && globalRepl->debuglevel() > 3) { 
            printf("NSLOG: '%s' ::lookUp(%s)\n",canonicalName.c_str(),iter->second->getFullName().c_str());
        }

        return iter->second;
    } else if (recurse) {
        VarDefPtr def;

        // try to find the definition in the parents
        NamespacePtr parent;
        for (unsigned i = 0; parent = getParent(i++);)
            if (def = parent->lookUp(varName))
                break;

        if (def && globalRepl && globalRepl->debuglevel() > 3) {
            printf("NSLOG: '%s' ::lookUp(%s)\n",canonicalName.c_str(),def->getFullName().c_str()); 
        }
        return def;        
    }

    return 0;
}

ModuleDefPtr Namespace::getRealModule() {
    ModuleDefPtr mod = getModule();
    ModuleDefPtr owner = ModuleDefPtr::cast(mod->getOwner());
    return owner ? owner : mod;
}

bool Namespace::hasAliasFor(VarDef *def) const {
    for (VarDefMap::const_iterator iter = defs.begin(); iter != defs.end(); 
         ++iter
         )
        if (iter->second.get() == def)
            return true;
    return false;
}

void Namespace::addDef(VarDef *def) {
    assert(!def->getOwner());

    // storeDef needs owner already set, so that getDisplayName()
    //  works and differentiates overloaded functions from each other.
    def->setOwner(this);
    storeDef(def);
}

void Namespace::removeDef(VarDef *def) {
    assert(!OverloadDefPtr::cast(def));

    string d = def->getDisplayName();
    string n = def->name;
    VarDefMap::iterator iter = defs.find(n);
    if (iter == defs.end()) {
        fprintf(stderr,"internal error in client of Namespace::removeDef(): def '%s'"
                " not found.\n", n.c_str());
        assert(0);
        exit(1);
    }
    assert(iter != defs.end());
    defs.erase(iter);

    // remove it from the ordered defs
    for (VarDefVec::iterator iter = ordered.begin();
         iter != ordered.end();
         ++iter
         ) {
        if (n == (*iter->get()).name) {
            ordered.erase(iter);
            break;
        }
    }
    
    // remove it from the ordered for cache defs
    for (VarDefVec::iterator iter = orderedForCache.begin();
         iter != orderedForCache.end();
         ++iter
         ) {
        if (n == (*iter->get()).name) {
            orderedForCache.erase(iter);
            break;
        }
    }

    long i = orderedForTxn.lookupI(def, this);
    if (i>=0) {
        orderedForTxn.erase(i);
    }

}

void Namespace::removeDefAllowOverload(VarDef *def) {

    OverloadDefPtr odef = OverloadDefPtr::cast(def);

    if (odef) {
        printf("this is wrong...figure out how to do it right!!\n");
        assert(0);
        exit(1);

            // we've got (possibly) a whole list to remove
            model::OverloadDef::FuncList::iterator it = odef->beginTopFuncs();
            model::OverloadDef::FuncList::iterator en = odef->endTopFuncs();
            for (; it != en; ++it) {
                FuncDefPtr fdp  = *it;
                FuncDef*   func = fdp.get();
                removeDef(func);
            }
            
    } else {
        removeDef(def);
    }
}

void Namespace::addAlias(VarDef *def) {
    // make sure that the symbol is already bound to a context.
    assert(def->getOwner());

    // overloads should never be aliased - otherwise the new context could 
    // extend them.
    OverloadDef *overload = OverloadDefPtr::cast(def);
    if (overload) {
        OverloadDefPtr child = overload->createAlias();
        child->setOwner(this);
        storeDef(child.get());
    } else {
        storeDef(def);
    }
}

OverloadDefPtr Namespace::addAlias(const string &name, VarDef *def) {
    // make sure that the symbol is already bound to a context.
    assert(def->getOwner());

    if (globalRepl && globalRepl->debuglevel() > 3) {
        printf("NSLOG: addAlias(%s,%s)\n", name.c_str(), def->getFullName().c_str());
    }

    // overloads should never be aliased - otherwise the new context could 
    // extend them.
    OverloadDef *overload = OverloadDefPtr::cast(def);
    if (overload) {
        OverloadDefPtr child = overload->createAlias();
        defs[name] = child.get();
        child->setOwner(this);
        return child;
    } else {
        defs[name] = def;
        return 0;
    }
}

void Namespace::addUnsafeAlias(const string &name, VarDef *def) {
    // make sure that the symbol is already bound to a context.
    assert(def->getOwner());
    defs[name] = def;
}

void Namespace::aliasAll(Namespace *other) {
    for (VarDefMap::iterator iter = other->beginDefs();
         iter != other->endDefs();
         ++iter
         )
        if (!lookUp(iter->first))
            addAlias(iter->second.get());
    
    // do parents afterwards - since we don't clobber existing aliases, we 
    // want to do the innermost names first.
    NamespacePtr parent;
    for (int i = 0; parent = other->getParent(i++);) {
        aliasAll(parent.get());
    }
}

void Namespace::replaceDef(VarDef *def) {
    assert(!def->getOwner());
    assert(!def->hasInstSlot() && 
           "Attempted to replace an instance variable, this doesn't work "
           "because it won't change the 'ordered' vector."
           );
    def->setOwner(this);
    defs[def->name] = def;
}

void Namespace::dump(ostream &out, const string &prefix) {
    out << canonicalName << " (0x" << this << ") {\n";
    string childPfx = prefix + "  ";
    unsigned i = 0;
    Namespace *parent;
    while (parent = getParent(i++).get()) {
        out << childPfx << "parent namespace ";
        parent->dump(out, childPfx);
    }
    
    for (VarDefMap::const_iterator varIter = defs.begin();
         varIter != defs.end();
         ++varIter
         )
        varIter->second->dump(out, childPfx);
    out << prefix << "}\n";
}

void Namespace::dump() {
    dump(cerr, "");
}

void Namespace::serializeDefs(Serializer &serializer) const {
    
    // count the number of definitions to serialize
    int count = 0;
    for (VarDefMap::const_iterator i = defs.begin();
         i != defs.end();
         ++i
         ) {
        if (i->second->isSerializable(serializer.module))
            ++count;
    }
    
    // write the count and the definitions
    serializer.write(count, "#defs");
    for (VarDefMap::const_iterator i = defs.begin();
         i != defs.end();
         ++i
         ) {
        if (!i->second->isSerializable(serializer.module))
            continue;
        else if (i->second->getModule() != serializer.module)
            i->second->serializeAlias(serializer, i->first);
        else
            i->second->serialize(serializer, true);
    }
}

void Namespace::deserializeDefs(Deserializer &deser) {
    // read all of the symbols
    unsigned count = deser.readUInt("#defs");
    for (int i = 0; i < count; ++i) {
        int kind = deser.readUInt("kind");
        switch (static_cast<Serializer::DefTypes>(kind)) {
            case Serializer::variableId:
                addDef(VarDef::deserialize(deser).get());
                break;
            case Serializer::aliasId: {
                string alias = 
                    deser.readString(Serializer::varNameSize, "alias");
                addAlias(alias, VarDef::deserializeAlias(deser).get());
                break;
            }
            case Serializer::genericId:
                // XXX don't think we need this, generics are probably stored 
                // in a type.
                SPUG_CHECK(false, "can't deserialize generics yet");
//                addDef(Generic::deserialize(deser));
                break;
            case Serializer::overloadId:
                addDef(OverloadDef::deserialize(deser).get());
                break;
            case Serializer::typeId:
                TypeDef::deserialize(deser).get();
                break;
            default:
                SPUG_CHECK(false, "Bad definition type id " << kind);
        }
    }
}


/**
 * short_dump - parent namespaces omitted.
 */
void Namespace::short_dump() {
    ostream& out = std::cerr;
    const std::string prefix ="";
    out << canonicalName << " (0x" << this << ") {\n";
    string childPfx = prefix + "  ";
    for (VarDefMap::const_iterator varIter = defs.begin();
         varIter != defs.end();
         ++varIter
         )
        varIter->second->dump(out, childPfx);
    out << prefix << "}\n";

    if (globalRepl && globalRepl->debuglevel() > 1) {
        long start = 0;
        if (txLog.size()) {
            start = txLog.front().last_commit;
        }

        printf("=== begin orderedForTxn, everything from %ld ===\n", start);
        orderedForTxn.dump(start, false);
        //printf("=== begin orderedForTxn, dups only, from %ld  ===\n", start);
        //            orderedForTxn.dump(start, true); // only dups (shorter)
        printf("=== end orderedForTxn ===\n");
    }
}



Namespace::Txmark Namespace::markTransactionStart() {
    Namespace::Txmark t;
    t.ns = this;
    t.last_commit = orderedForTxn.vec().size()-1;
    txLog.push_back(t);
    return t;
}


void Namespace::undoHelperDeleteFromDefs(VarDef* v, const Txmark& t, Repl* repl) {

    VarDefMap::iterator mapit = defs.find(v->name);

    if (mapit != defs.end() && mapit->second.get() == v) { 

        if (repl && repl->debuglevel() > 0) {
            cerr << "undo in namespace '" 
                 << canonicalName
                 << "'removing name: '" 
                 << v->name << "'" << endl;
        }
        
        defs.erase(mapit);
    }
}

void Namespace::undoHelperRollbackOrderedForTx(const Txmark& t) {

    //    ordered.erase(ordered.begin() + t.last_commit + 1,
    //                  ordered.end());
    //    orderedForCache.erase(orderedForCache.begin() + t.last_commit + 1,
    //                          orderedForCache.end());
    orderedForTxn.eraseFrom(t.last_commit + 1);
}

void Namespace::undoTransactionTo(const Namespace::Txmark& t,
                                  Repl* repl) {
    
    if (this != t.ns) {
        printf("error in undoTransactionTo: in wrong namespace\n");
        assert(0);
        exit(1);
    }

    if (t.last_commit < 0) return;

    long n = (long)orderedForTxn.vec().size();

    VarDefPtr v;
    builder::Builder* bdr = repl ? repl->builder() : 0;

#if 0

    for(long i = t.last_commit + 1; i < n; ++i) {

        // detect overloads
        v = orderedForTxn[i];
        model::OverloadDef *odef = 0;

        VarDefMap::iterator mapit = defs.find(v->name);

        if (mapit != defs.end()) {
            odef     = dynamic_cast<model::OverloadDef*>(mapit->second.get());
        }

        if (!odef) {
            undoHelperDeleteFromDefs(v.get(), t, repl);
        } else {

            //unreliable:            if (odef->isSingleFunction()) {
            //undoHelperDeleteFromDefs(v.get(), t, repl);

                // multiple overloads, so skip the update of defs, which 
                // has them all under the same symbol.

                // INVAR: done with defs update.

                // cleanup with eraseFromParent() to
                // prevent dangling half-completed functions that will keep us
                // from compiling further code.
                
                // just delete the last still-live overload on the list
                if (bdr) {
                    model::OverloadDef::FuncList::iterator first = odef->beginTopFuncs();
                    model::OverloadDef::FuncList::iterator fit = odef->beginTopFuncs();
                    model::OverloadDef::FuncList::iterator fen = odef->endTopFuncs();
                    model::OverloadDef::FuncList::iterator flast = fen;
                    --flast;
                    ++fit;
                    for (fit = flast; fit != first; --fit) {
                        
                        FuncDefPtr fdp  = *fit;
                        FuncDef*   func = fdp.get();
                        
                        builder::mvll::LLVMBuilder* llvm_bdr = dynamic_cast<builder::mvll::LLVMBuilder*>(bdr);
                        
                        if (func && llvm_bdr) {
                            
                            builder::mvll::LLVMBuilder::ModFuncMap::iterator j = llvm_bdr->moduleFuncs.find(func);
                            
                            if (j != llvm_bdr->moduleFuncs.end()) {
                                
                                llvm::Function* f = j->second;
                                
                                if (repl) {
                                    wisecrack::Repl::gset::iterator en = repl->goneSet.end();
                                    wisecrack::Repl::gset::iterator it = repl->goneSet.find(f);
                                    if (it != en) {
                                        // already removedFromParent; don't do it twice or we'll crash.
                                        printf("undoTransactionTo(): detected this "
                                               "function '%s' is already gone. Not cleaning up twice!\n", 
                                               orderedForTxn[i]->name.c_str());
                                        f = 0;
                                        odef->erase(fit);
                                        break; // just do one.
                                    }
                                }
                                
                                if (f) {
                                    if (repl && repl->debuglevel() > 0) {                                
                                        printf("trying to delete:\n");
                                        llvm::outs() << static_cast<llvm::Value&>(*f);
                                    }
                                    f->eraseFromParent();
                                    odef->erase(fit);
                                }
                            }
                        } else {
                            printf("error in Namesapce::undoTransactionTo(): "
                                   "could not eraseFromParent() on '%s'.\n",
                                   orderedForTxn[i]->name.c_str());
                        }

                        break; // only do the last.
                    } // end for over overloads
                } // end if bdr

        } // end else
    } // end for i
#endif

   // last thing
   undoHelperRollbackOrderedForTx(t);
}

void Namespace::undo(Repl *r) {
    assert(txLog.size());
    Namespace::Txmark t = txLog.back();
    undoTransactionTo(t, r);
    txLog.pop_back();
}

const char* Namespace::lastTxSymbol(model::TypeDef **tdef) {
    size_t n = orderedForTxn.vec().size();
    if (0==n) return NULL;

    size_t i = n;
    const char* s = 0;

    // skip internal :exStruct additions;
    // anything that starts with a : won't print
    // well regardless via cout `$(s)` 
    while(i > 0) {
        s = orderedForTxn.vec()[i-1].vardef->name.c_str();
        if (':' == *s) {
            --i;
            continue;
        }
        // can't print some internally generated stuff
        model::VarDef   *d = (orderedForTxn.vec()[i-1].vardef);
        model::TypeDef *td = d->type.get();
        if (!td) {
            --i;
            continue;
        }
        // pass back type info
        if (tdef){
            *tdef = td;
        }
        return s;
    }

    return NULL;
}
