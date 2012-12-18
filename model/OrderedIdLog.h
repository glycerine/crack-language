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

        //
        // And, index by string. Here the string is an overload-distinguishing name;
        //  as opposed to the names in Namespace::defs which confound
        //  lookup (for removal) by combining overloaded names into
        //  one thing. So we will have "
        //
        typedef std::multimap<std::string, long> StrPosMap;

        typedef DefPosMap::iterator   DefPosMapIt;
        typedef DefPosMap::value_type DefPosMapValue;
        typedef DefPosMapIt DefPosMapInsert;
        typedef std::pair<DefPosMapIt, DefPosMapIt> DefPosMapPair;

        typedef StrPosMap::iterator   StrPosMapIt;
        typedef StrPosMap::value_type StrPosMapValue;
        typedef StrPosMapIt StrPosMapInsert;
        typedef std::pair<StrPosMapIt, StrPosMapIt> StrPosMapPair;

        // the main element we all refer to
        struct VarDefName {
            model::VarDef*      vardef;
            std::string  odname; // overload distinguishing name
            Namespace*   ns;
            long         i;      // vdnvec index
            DefPosMapIt  vi;    // index into v2i;
            StrPosMapIt  si;    // index into s2i;
            int        vdup;    // duplicate count, VarDef
            int        sdup;    // duplicate count, odname

            void dump(bool onlydup = false);
        };

        // the main container of elements, owns the VarDefNames
        typedef std::vector<VarDefName> VDNVector;
        typedef VDNVector::iterator ohit;

        VDNVector _vec;
        VDNVector& vec();

        // indexes _vec by VarDef
        DefPosMap v2i;
        VarDefName* lookup(VarDef* v, Namespace* ns) {
            DefPosMapPair pp = v2i.equal_range(v);
            if (pp.first == v2i.end()) return 0;
            for (DefPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_vec[i->second].ns == ns) {
                    return & _vec[i->second];
                }
            }
            return 0;
        }

        long lookupI(VarDef* v, Namespace* ns) {
            DefPosMapPair pp = v2i.equal_range(v);
            if (pp.first == v2i.end()) return -1;
            for (DefPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_vec[i->second].ns == ns) {
                    return i->second;
                }
            }
            return -1;
        }


        // index _vec by odname
        StrPosMap s2i;
        VarDefName* lookup(const char* s, Namespace* ns) {
            StrPosMapPair pp = s2i.equal_range((char*)s);
            if (pp.first == s2i.end()) return 0;
            for (StrPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_vec[i->second].ns == ns) {
                    return & _vec[i->second];
                }
            }
            return 0;
        }

        long lookupI(const char* s, Namespace* ns) {
            StrPosMapPair pp = s2i.equal_range((char*)s);
            if (pp.first == s2i.end()) return -1;
            for (StrPosMapIt i = pp.first; i != pp.second; ++i) {
                if (_vec[i->second].ns == ns) {
                    return i->second;
                }
            }
            return -1;
        }


        long lookupI(const char* s) {
            StrPosMapIt it = s2i.find((char*)s);
            if (it == s2i.end()) return -1;
            return it->second;
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
            long n = _vec.size();
            assert(s2i.size() == n);
            assert(v2i.size() == n);
 
            bool done_early = false;
            long dup = lookupI(v, nspc); 
            if (-1 != dup) {
                ++(_vec[dup].vdup);
                done_early = true;
            }
            dup = lookupI(s, nspc);
            if (-1 != dup) {
                ++(_vec[dup].sdup);
                done_early = true;
            }
            if (done_early) return false;

            VarDefName Node;
            Node.i  = n;
            Node.ns = nspc;
            Node.vardef      = v;
            Node.odname      = s;
            Node.sdup = 0;
            Node.vdup = 0;
            DefPosMapInsert id = v2i.insert(DefPosMapValue(v,n));
            Node.vi = id;
            StrPosMapInsert is = s2i.insert(StrPosMapValue(s,n));
            Node.si = is;
            

            _vec.push_back(Node);
            return true;
        }

        // empty-out the container and indices completely.
        void clear() {
            v2i.clear();
            s2i.clear();
            _vec.clear();
        }
                
        // remove all elements >= k
        void eraseFrom(long k) {
            long n = _vec.size();
            for (long i = k; i < n; ++i) {
                VarDefName& v = _vec[i];
                v2i.erase(v.vi);
                s2i.erase(v.si);
            }
            _vec.resize(k);
        }

        // erase one element from vector, O(n) time.
        void erase(long k) {
            long n = _vec.size();
            if (k >= n || k < 0) {
                assert(0);
                return;
            }

            ohit target = _vec.begin() + k;
            ohit j = target + 1;
            for (; j != _vec.end(); ++j) {
                VarDefName& d = *j;
                d.i = d.i - 1;
            }
            v2i.erase(target->vi);
            s2i.erase(target->si);
            _vec.erase(target);
        }

        void dump(long start = 0, bool dupsonly = false) {
            long n = _vec.size();
            for (long i = start; i < n; ++i) {
                _vec[i].dump(dupsonly);
            }
        }
        
    }; // end class Orderedhash
        
} // end namespace model

int main_orderedhash_test();
        
#endif // _model_OrderedIdLog_h_

