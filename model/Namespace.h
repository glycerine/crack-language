// Copyright 2010,2012 Shannon Weyrick <weyrick@mozek.us>
// Copyright 2010-2012 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _model_Namespace_h_
#define _model_Namespace_h_

#include <map>
#include <vector>
#include <spug/RCBase.h>
#include <spug/RCPtr.h>
#include "wisecrack/repl.h"
#include "model/OrderedHash.h"

using wisecrack::Repl;

namespace model {

class TypeDef;
class Context;
class Deserializer;
SPUG_RCPTR(Expr);
SPUG_RCPTR(FuncDef);
SPUG_RCPTR(ModuleDef);
SPUG_RCPTR(OverloadDef);
class Serializer;
SPUG_RCPTR(VarDef);

SPUG_RCPTR(Namespace);

/**
 * A namespace is a symbol table.  It defines the symbols that exist in a 
 * given context.
 */
        
class Namespace : public virtual spug::RCBase {

    public:
        typedef std::map<std::string, VarDefPtr> VarDefMap;
        typedef std::vector<VarDefPtr> VarDefVec;

    protected:        
        VarDefMap defs;

        // in an "instance" context, this maintains the order of declaration 
        // of the instance variables so we can create and delete in the 
        // correct order.
        VarDefVec ordered;

        // ordered list of _all_ vardefs. XXX this is only needed for
        // caching currently
        VarDefVec orderedForCache;

        // ordered list of all vardefs for transactions delete
        // used by repl for syntax error cleanup.
        // Make it static so we track additions across all namespaces.
        static OrderedHash orderedForTxn;

        // fully qualified namespace name, e.g. "crack.io"
        std::string canonicalName;

        /**
         * Stores a definition, promoting it to an overload if necessary.
         */
        virtual void storeDef(VarDef *def);


    public:
        
        Namespace(const std::string &cName) :
                canonicalName(cName)
        {

        }
        
        /**
         * Returns the fully qualified name of the namespace
         */
        const std::string &getNamespaceName() const { return canonicalName; }

        /** 
         * Returns the parent at the index, null if it is greater than 
         * the number of parents.
         */
        virtual NamespacePtr getParent(unsigned index) = 0;

        VarDefPtr lookUp(const std::string &varName, bool recurse = true);
        
        /**
         * Returns the module that the namespace is part of.
         */
        virtual ModuleDefPtr getModule() = 0;

        /**
         * Returns the "real module" that the namespace is part of.  If the 
         * namespace is part of an ephemeral module generated for a generic, 
         * the real module is the module that the generic was defined in.
         * This is equivalent to the owner of the module returned by 
         * getModule() if there is one, and simply the module returned by 
         * getModule() if it doesn't have an owner.
         */
        ModuleDefPtr getRealModule();

        /**
         * Returns true if the definition is aliased in the namespace.
         */
        bool hasAliasFor(VarDef *def) const;

        /**
         * Add a new definition to the namespace (this may not be used for 
         * FuncDef's, these must be wrapped in an OverloadDef.  See Context 
         * for an easy way to add FuncDef's)
         */        
        virtual void addDef(VarDef *def);
        
        /** 
         * Remove a definition.  Intended for use with stubs - "def" must not 
         * be an OverloadDef. 
         */
        virtual void removeDef(VarDef *def, 
                               bool OverloadDefAllowed = false,
                               bool bulkclean = false);

        /**
         *  Remove a possibly overloaded definition. Intended for use from the repl.
         */
        virtual void removeDefAllowOverload(VarDef *def, bool bulkclean = false);
        
        /**
         * Adds a definition to the context, but does not make the definition's 
         * context the context.  This is used for importing symbols into a 
         * module context.
         */
        virtual void addAlias(VarDef *def);
        virtual OverloadDefPtr addAlias(const std::string &name, 
                                        VarDef *def
                                        );

        /**
         * Adds an alias without special processing if 'def' is an overload.  
         * This is only safe in a situation where we know that the overload 
         * can never be extended in a new context, which happens to be the 
         * case when we're aliasing symbols from .builtin to the root module.
         */
        virtual void addUnsafeAlias(const std::string &name, VarDef *def);
        
        /** 
         * Alias all symbols from the other namespace and all of its ancestor 
         * namespaces. 
         */
        void aliasAll(Namespace *other);
        
        /**
         * Replace an existing defintion with the new definition.
         * This is only used to replace a StubDef with an external function 
         * definition.
         */
        virtual void replaceDef(VarDef *def);

        void dump(std::ostream &out, const std::string &prefix);
        void dump();
        void short_dump();

        /** overloads don't call addDef() unfortunately. so
         *   have them call this instead so we know exactly
         *   what got added to the namespace during the current
         *   transaction.
         */
        void noteOverloadForTxn(VarDef* def);
        
        /** Funcs to iterate over the set of definitions. */
        /// @{
        VarDefMap::iterator beginDefs() { return defs.begin(); }
        VarDefMap::iterator endDefs() { return defs.end(); }
        VarDefMap::const_iterator beginDefs() const { return defs.begin(); }
        VarDefMap::const_iterator endDefs() const { return defs.end(); }
        /// @}
        
        /** Funcs to iterate over the definitions in order of declaration. */
        /// @{
        VarDefVec::iterator beginOrderedDefs() { return ordered.begin(); }
        VarDefVec::iterator endOrderedDefs() { return ordered.end(); }
        VarDefVec::const_iterator beginOrderedDefs() const {
            return ordered.begin();
        }
        VarDefVec::const_iterator endOrderedDefs() const {
            return ordered.end();
        }
        /// @}

        /** XXX Cache ordered vector */
        /// @{
        VarDefVec::const_iterator beginOrderedForCache() const {
            return orderedForCache.begin();
        }
        VarDefVec::const_iterator endOrderedForCache() const {
            return orderedForCache.end();
        }
        /// @}

        /**
         * Serialize all of the definitions in the namespace.
         */
        void serializeDefs(Serializer &serializer) const;
        
        /** 
         * Deserialize an array of definitions and store them in the 
         * namespace.
         */
        void deserializeDefs(Deserializer &deser);

        /**
         * note where we are in the ordered defs, so we
         *  know how far back to delete if we have to undo
         *  the transaction implicit in the repl parsing.
         */
        struct Txmark { 
            Namespace* ns; // sanity check we're at the right place.
            long last_commit; // -1 => empty ns, don't delete last_commit.
            Txmark() : ns(0), last_commit(-1) {}
        };
        Txmark markTransactionStart();

        /** 
         * do the roll back, deleting var defs up to but not 
         * including last_commit. 
         */
        void undoTransactionTo(const Txmark& txstart, 
                               Repl *r = 0);

        /**
         * roll back the tail of the log, e.g. using txLog.back()
         */
        void undo(Repl *r = 0);
        
        std::vector<Txmark> txLog; // stack of transactions
        
        /*
         * get last symbol added to the tx table.
         *  Set tdef if you want a pointer to the type back.
         */
        const char* lastTxSymbol(model::TypeDef **tdef = 0);

 private:
        /** helpers for undo */
        void undoHelperDeleteFromDefs(VarDef* v, const Txmark& t, Repl* repl);
        void undoHelperRollbackOrderedForTx(const Txmark& t);

};

} // namespace model

#endif
