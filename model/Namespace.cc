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
OrderedIdLog Namespace::orderedForTxn;

Namespace::~Namespace() {
    if (globalRepl && globalRepl->debugLevel() > 0)
        printf("~Namespace dtor firing on 0x%lx\n",(long)this);

    //printf("calling orderedForTxn.eraseNamespace(0x%lx) '%s'\n", (long)this, canonicalName.c_str());
    orderedForTxn.eraseNamespace(this);
}


void Namespace::storeDef(VarDef *def) {

    if (globalRepl && globalRepl->debugLevel() > 3) {
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

        if (iter->second && globalRepl && globalRepl->debugLevel() > 3) { 
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

        if (def && globalRepl && globalRepl->debugLevel() > 3) {
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

void Namespace::removeDef(VarDef *def, bool OverloadDefAllowed, bool bulkclean) {
    if (!OverloadDefAllowed) {
        assert(!OverloadDefPtr::cast(def));
    }

    string d = def->getDisplayName();
    string n = def->name;
    VarDefMap::iterator iter = defs.find(n);
    if (iter == defs.end()) {
        return;

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

    // skip slow one-off erase if bulkclean-up is in progress.
    if (!bulkclean) {
        long i = orderedForTxn.lookupI(def, this);
        if (i>=0) {
            orderedForTxn.erase(i);
        }
    }
}

void Namespace::removeDefAllowOverload(VarDef *def, bool bulkclean) {

    OverloadDef* odef = dynamic_cast<OverloadDef*>(def);    
    // crashes???   OverloadDefPtr odef = OverloadDefPtr::cast(def);

    if (odef) {
        model::OverloadDef::FuncList::iterator it = odef->beginTopFuncs();
        model::OverloadDef::FuncList::iterator en = odef->endTopFuncs();
        model::OverloadDef::FuncList::iterator two = it;
        ++two;
        
        if (two != en) {
            // we've got two functions, don't removeDef because that
            // would delete them both.

            // just delete the last added overload.
            it = en;
            --it;
            FuncDefPtr fdp  = *it;
            FuncDef*   func = fdp.get();

            long i = orderedForTxn.lookupI(func, this);
            if (i>=0) {
                orderedForTxn.erase(i);
            }

            odef->erase(it);
            //func->eraseFromParent();
            return;
        } else {
            removeDef(def, true, bulkclean);
            return;
        }
    }
    removeDef(def, false, bulkclean);
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

    if (globalRepl && globalRepl->debugLevel() > 3) {
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

    if (globalRepl && globalRepl->debugLevel() > 0) {

        // start printing two txn back, if we have them
        long start = 0;
        long inclusiveEnd = orderedForTxn.lastId();
        if (txLog.size()) {
            TxLogIt it = txLog.end();
            --it;
            if (txLog.size() > 1) {
                --it;
            }
            start = it->last_commit;
        }

        printf("=== begin orderedForTxn, from %ld  through  %ld ===\n", start, inclusiveEnd);
        orderedForTxn.dump(start, inclusiveEnd, false);

        printf("=== end orderedForTxn ===\n");
    }
}



Namespace::Txmark Namespace::markTransactionStart() {
    Namespace::Txmark t;
    t.ns = this;
    t.last_commit = orderedForTxn.lastId();
    txLog.push_back(t);
    return t;
}


void Namespace::undoHelperDeleteFromDefs(VarDef* v, const Txmark& t, Repl* repl) {

    VarDefMap::iterator mapit = defs.find(v->name);

    if (mapit != defs.end() && mapit->second.get() == v) { 

        if (repl && repl->debugLevel() > 0) {
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

    OrderedIdLog::VdnMap& mainMap = orderedForTxn.vec();
    OrderedIdLog::VdnMapIt en = mainMap.end();
    OrderedIdLog::VdnMapIt st = mainMap.find(t.last_commit);
    if (st == mainMap.end()) {
        // deleted already, need to get previous still valid key...
        OrderedIdLog::VdnMapPair pp = mainMap.equal_range(t.last_commit);

        if (pp.first == mainMap.end()) {
            // all newer transactions gone, last_commit was greater
            // than anything left.
            return; // done
        }
        
        // don't need to increment, this is where we want to
        // start deleting.
        st = pp.first;

    } else {
        // move past the last commit
        ++st;
    }

    if (st == en) return;

    for (OrderedIdLog::VdnMapIt it = st; it != en; ++it) {
        OrderedIdLog::VarDefName& d = it->second;
        if (this == d.ns) {
            d.ns->removeDefAllowOverload(d.vardef, true);

        } else {
            bool found = false;
            Namespace *parent = 0;
            unsigned int i = 0;
            while(parent = getParent(i++).get()) {
                if (d.ns == parent) {
                    printf("undo txn found vardef '%s' in ancestor namespace '%s'\n",
                           d.vardef->name.c_str(),
                           d.ns->getNamespaceName().c_str());

                    parent->removeDefAllowOverload(d.vardef, true);
                    found = true;
                    break;
                }
            }
            if (found) continue;
        }

        //        printf("undo tx saw unknown, non-ancestor namespace '%s'\n",
        //               d.ns->getNamespaceName().c_str());
    }

    orderedForTxn.eraseBeyond(t.last_commit);
}

void Namespace::undoTransactionTo(const Namespace::Txmark& t,
                                  Repl* repl) {
    
    if (this != t.ns) {
        printf("error in undoTransactionTo: in wrong namespace\n");
        assert(0);
        exit(1);
    }

    if (t.last_commit < 0) return;

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
