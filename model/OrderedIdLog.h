#ifndef _model_OrderedIdLog_h_
#define _model_OrderedIdLog_h_

#include <map>
#include <vector>
#include <string>
#include <tr1/unordered_map>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

/**
 * OrderedIdLog: keep a vector of VarDef's that can be searched
 *   readily by value or by name. It's tail elements can be truncated
 *   off rapidly when we rollback/abort a transaction because
 *   of syntax error at the repl.
 *   It is ordered in the sense that the order in which definitions
 *   are pushed-back is preserved for ease of truncation.
 *   It is hashed in that location of a VarDef of name is fast.
 *
 * Ironically, tr1::hash_multimap invalidates iterators upon insert.
 *  which means we had to shift to std::multimap instead. multimap
 *  preserves iterators after either insert or erase.
 */

namespace model {

    class VarDef;
    class Namespace;

    // a simple vector together with simple hashtables for fast lookup

    class OrderedIdLog {
    public:
    
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
        //  one thing. So we will have "
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
        typedef std::pair<VdnMapIt,bool> VdnMapInsert;

        struct BadOrderedIdLogIndexOperatrion {};
        VarDefName& operator[] (unsigned long i) {
            VdnMapIt it = _mainMap.find(i);
            if (it == _mainMap.end()) throw BadOrderedIdLogIndexOperatrion();
            return (it->second);
        }

        VdnMap _mainMap;
        long   _lastId;

        long    nextId() {
            ++_lastId;
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
            DefPosMapInsert iv = v2i.insert(DefPosMapValue(v,id));
            Node.vi = iv;
            StrPosMapInsert is = s2i.insert(StrPosMapValue(s,id));
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
                
        // remove all elements >= k
        void eraseFrom(long k) {

            VdnMapIt en = _mainMap.end();
            VdnMapIt st = _mainMap.find(k);
            if (st == _mainMap.end()) {
                throw BadOrderedIdLogIndexOperatrion();
            }

            for (VdnMapIt it = st; it != en; ++it) {
                VarDefName& v = it->second;
                v2i.erase(v.vi);
                s2i.erase(v.si);
            }
            _mainMap.erase(_mainMap.find(k), _mainMap.end());
        }

        // erase one element from _mainMap and indices
        void erase(long k) {
            VdnMapIt target = _mainMap.find(k);
            if (target == _mainMap.end()) 
                throw BadOrderedIdLogIndexOperatrion();

            // ns2mm deletions
            Ns2MainMapPair pp = ns2mm.equal_range(target->second.ns);
            if (pp.first != ns2mm.end()) {
                for (Ns2MainMapIt it = pp.first; it != pp.second; ++it) {
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
        }

        void dump(long start = 0, bool dupsOnly = false) {
            long n = _mainMap.size();

            VdnMapIt en = _mainMap.end();
            VdnMapIt st = _mainMap.find(n);
            if (st == _mainMap.end()) {
                throw BadOrderedIdLogIndexOperatrion();
            }

            for (VdnMapIt it = st; it != en; ++it) {
                it->second.dump(dupsOnly);
            }
        }
        
    }; // end class Orderedhash
        
} // end namespace model

int main_orderedhash_test();
        
#endif // _model_OrderedIdLog_h_

