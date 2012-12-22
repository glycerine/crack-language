// Copyright 2012 Jason E. Aten
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _model_OrderedIdLog_h_
#define _model_OrderedIdLog_h_

#include <map>
#include <vector>
#include <string>
#include <tr1/unordered_map>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

/**
 * OrderedIdLog: keep a map from serial number to VarDef's 
 *   that can be searched readily by value or by name. 
 *   It's tail elements can be truncated
 *   off cleanly when we rollback/abort a transaction due
 *   to syntax error at the repl.
 *   It is ordered by the serial numbers (see nextId() and
 *   lastId() which are allocated in the chronological order
 *   in which VarDefs are entered by the push_back() method.
 *
 * Originally used tr1::hash_multimap for indexes, but
 *  it turns out that tr1 hash-maps invalidate iterators upon insert.
 *  We had to shift to std::multimap instead for the indices. multimap
 *  preserves iterators after either insert or erase. The
 *  _mainMap that holds the central VarDefName element is a std::map,
 *  although it was originally a vector, and there are some
 *  vestiges of that yet to be refactored out.
 * 
 * We don't want to keep alive any of our VarDef or Namespace
 *  references, as this might induce errors that we aren't 
 *  ready to reason about. Hence there is no reference counting
 *  here.
 *
 * NB: Still under development, so all in the header file
 *     for ease of change.
 *
 * XXX TODO: once stabilized, encapsulate/separate .h/.cc
 */

namespace model {

    class VarDef;
    class Namespace;

    // a simple vector together with simple hashtables for fast lookup

    class OrderedIdLog {
    public:
    
        OrderedIdLog()
            : _lastId(0) {
            
        }

        // index by VarDef* 
        typedef std::multimap<VarDef*, long> DefPosMap;
        typedef DefPosMap::iterator   DefPosMapIt;
        typedef DefPosMap::value_type DefPosMapValue;
        typedef DefPosMapIt DefPosMapInsert;
        typedef std::pair<DefPosMapIt, DefPosMapIt> DefPosMapPair;

        //
        //  Index by string. Here the string is an overload-distinguishing 
        //  name;
        //  as opposed to the names in Namespace::defs which confound
        //  lookup (for removal) by combining overloaded names into
        //  one thing.
        //
        typedef std::multimap<std::string, long> StrPosMap;
        typedef StrPosMap::iterator   StrPosMapIt;
        typedef StrPosMap::value_type StrPosMapValue;
        typedef StrPosMapIt StrPosMapInsert;
        typedef std::pair<StrPosMapIt, StrPosMapIt> StrPosMapPair;

        // the main element we all refer to
        struct VarDefName {
            model::VarDef*      vardef;
            std::string  odname; // overload distinguishing name
            Namespace*   ns;
            long         id;
            DefPosMapIt  vi;    // index into v2i;
            StrPosMapIt  si;    // index into s2i;
            int        vdup;    // duplicate count, VarDef
            int        sdup;    // duplicate count, odname

            void dump(bool dupsOnly = false);
        };

        // The main container of elements, owns the VarDefNames.
        // It is indexed by an increasing long id, allowing quick
        // txn rollback. Used to be a vector, but the map
        // avoids the O(n^2) update issue.
        typedef std::map<long, VarDefName> VdnMap;
        typedef VdnMap::iterator VdnMapIt;
        typedef VdnMap::value_type VdnMapValue;
        typedef std::pair<VdnMapIt, bool> VdnMapInsert;
        typedef std::pair<VdnMapIt, VdnMapIt> VdnMapPair;

        struct BadOrderedIdLogIndexOperation {};
        VarDefName& operator[] (unsigned long i) {
            VdnMapIt it = _mainMap.find(i);
            if (it == _mainMap.end()) throw BadOrderedIdLogIndexOperation();
            return (it->second);
        }

        VdnMap _mainMap;
        long   _lastId;

        long    nextId() {
            ++_lastId;
            return _lastId;
        }

        long    lastId() {
            return _lastId;
        }

        VdnMap& vec();

        // index by Namespace
        typedef std::multimap<Namespace*, VdnMapIt> Ns2MainMap;
        typedef Ns2MainMap::iterator   Ns2MainMapIt;
        typedef Ns2MainMap::value_type Ns2MainMapValue;
        typedef Ns2MainMapIt Ns2MainMapInsert;
        typedef std::pair<Ns2MainMapIt, Ns2MainMapIt> Ns2MainMapPair;
        Ns2MainMap ns2mm;

        // indexes _mainMap by VarDef
        DefPosMap v2i;
        VarDefName* lookup(VarDef* v, Namespace* ns) {
            DefPosMapPair pp = v2i.equal_range(v);
            if (pp.first == v2i.end()) return 0;
            for (DefPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_mainMap[i->second].ns == ns) {
                    return & _mainMap[i->second];
                }
            }
            return 0;
        }

        long lookupI(VarDef* v, Namespace* ns) {
            DefPosMapPair pp = v2i.equal_range(v);
            if (pp.first == v2i.end()) return -1;
            for (DefPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_mainMap[i->second].ns == ns) {
                    return i->second;
                }
            }
            return -1;
        }


        // index _mainMap by odname
        StrPosMap s2i;
        VarDefName* lookup(const char* s, Namespace* ns) {
            StrPosMapPair pp = s2i.equal_range((char*)s);
            if (pp.first == s2i.end()) return 0;
            for (StrPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_mainMap[i->second].ns == ns) {
                    return & _mainMap[i->second];
                }
            }
            return 0;
        }

        long lookupI(const char* s, Namespace* ns) {
            StrPosMapPair pp = s2i.equal_range((char*)s);
            if (pp.first == s2i.end()) return -1;
            for (StrPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_mainMap[i->second].ns == ns) {
                    return i->second;
                }
            }
            return -1;
        }

        // lookup def by overload distiguishing name
        VarDef* lookup_def_by_odname(const char* s, Namespace* ns) {
            VarDefName* p = lookup(s, ns);
            if (p) {
                return p->vardef;
            }
            return 0;
        }

        // add element. Allow duplicates now, even if nspc is same.
        //  Count the dups in vdup and sdup
        //
        bool push_back(VarDef* v, const char* s, Namespace* nspc) {
            // sanity check
            assert(s2i.size() == _mainMap.size());
            assert(v2i.size() == _mainMap.size());
 
            long id = nextId();

            bool done_early = false;
            long dup = lookupI(v, nspc); 
            if (-1 != dup) {
                ++(_mainMap[dup].vdup);
                done_early = true;
            }
            dup = lookupI(s, nspc);
            if (-1 != dup) {
                ++(_mainMap[dup].sdup);
                done_early = true;
            }
            if (done_early) return false;

            VarDefName Node;
            Node.id  = id;
            Node.ns = nspc;
            Node.vardef      = v;
            Node.odname      = s;
            Node.sdup = 0;
            Node.vdup = 0;
            DefPosMapInsert iv = v2i.insert(DefPosMapValue(v, id));
            Node.vi = iv;
            StrPosMapInsert is = s2i.insert(StrPosMapValue(s, id));
            Node.si = is;

            VdnMapInsert e = _mainMap.insert(VdnMapValue(id, Node));
            assert(e.second);
            ns2mm.insert(Ns2MainMapValue(nspc, e.first));
            return true;
        }

        // empty-out the container and indices completely.
        void clear() {
            v2i.clear();
            s2i.clear();
            ns2mm.clear();
            _mainMap.clear();
        }

        /**
         * requesting -1 gets you the first, no matter at what
         *   id it is actually at.
         */
        VdnMapIt lookupKOrBeyond(long k, bool& exact) {
            if (k < 0) {
                exact = false;
                return _mainMap.begin();
            }

            VdnMapIt en = _mainMap.end();
            VdnMapIt st = _mainMap.find(k);
            if (st != en) {
                exact = true;
                return st;
            }

            exact = false;
            return _mainMap.upper_bound(k);
        }
                
        // remove all elements > k
        void eraseBeyond(long k) {

            VdnMapIt en = _mainMap.end();
            bool exactly_k = false;
            VdnMapIt st = lookupKOrBeyond(k, exactly_k);
            if (st == en) return;

            if (exactly_k) {
                // move beyond k so k can be last_committed.
                ++st;
            }

            if (st == _mainMap.end()) return;

            for (VdnMapIt it = st; it != en; ++it) {
                erase(it);
            }
        }

        // erase one element from _mainMap and indices
        bool erase(long k) {
            VdnMapIt target = _mainMap.find(k);
            return erase(target);
        }

        /** return true if erased, false if could not locate */
        bool erase(VarDef* v, Namespace* ns) {
            long i = lookupI(v, ns);
            if (-1 == i) return false;

            VdnMapIt target = _mainMap.find(i);
            return erase(target);
        }

        bool erase(const VdnMapIt &target) {
            if (target == _mainMap.end()) return false;

            // ns2mm deletions
            Ns2MainMapPair pp = ns2mm.equal_range(target->second.ns);
            if (pp.first != ns2mm.end()) {
                for (Ns2MainMapIt it = pp.first; it != pp.second 
                         && it != ns2mm.end(); ++it) {
                    if (it->second == target) {
                        ns2mm.erase(it);
                        // possible optimization, double check 
                        // correctness first: 
                        // break;
                    }
                }
            }

            v2i.erase(target->second.vi);
            s2i.erase(target->second.si);

            _mainMap.erase(target);
            return true;
        }

        void eraseNamespace(Namespace* ns) {

            Ns2MainMapPair pp = ns2mm.equal_range(ns);
            if (pp.first != ns2mm.end()) {
                for (Ns2MainMapIt it = pp.first; it != pp.second
                         && it != ns2mm.end(); ++it) {
                    VdnMapIt target = it->second;

                    v2i.erase(target->second.vi);
                    s2i.erase(target->second.si);
                    _mainMap.erase(target);
                    
                    ns2mm.erase(it);
                }
            }
        }

        /** if inclusiveEnd < 0 it is ignore and we dump until we have no more. */
        void dump(long start = 0, long inclusiveEnd = -1, bool dupsOnly = false) {
            if (inclusiveEnd < 0) inclusiveEnd = LONG_MAX;

            VdnMapIt en = _mainMap.end();
            bool exactly_start = false; // don't care about this here
            VdnMapIt st = lookupKOrBeyond(start, exactly_start);
            if (st == en) return;

            for (VdnMapIt it = st; it != en && it->second.id <= inclusiveEnd; ++it) {
                it->second.dump(dupsOnly);
            }
        }
        
    }; // end class OrderedLogId
        
} // end namespace model

int main_orderedhash_test();
        
#endif // _model_OrderedIdLog_h_

