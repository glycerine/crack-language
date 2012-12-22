// Copyright 2012 Jason E. Aten
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "model/OrderedIdLog.h"
#include "model/Namespace.h"
#include "model/VarDef.h"

namespace model {

    OrderedIdLog::VdnMap& OrderedIdLog::vec() { return _mainMap; }

    void OrderedIdLog::VarDefName::dump(bool dupsOnly) {
        if (dupsOnly) {
            if (vdup == 0 && sdup == 0) return;
        }
        printf("%ld   %s   : (vdup %d, sdup %d)   vardef: 0x%lx    ns: 0x%lx\n", 
               id, 
               odname.c_str(),
               vdup,
               sdup,
               (long)vardef, 
               (long)ns); // ns might be long gone, so don't try to print it.
    }

}


#undef TESTING_OH
//define TESTING_OH
#ifdef TESTING_OH

namespace model {
    struct VarDef {
        long _a;
        VarDef(long a) : _a(a) {}
    };

    
}

typedef model::VarDef D;
int main_orderedhash_test() {
    
    D a(0);
    D b(1);
    D c(2);
    model::OrderedIdLog v;

    v.push_back(&a, "zero");
    v.push_back(&b, "one");
    v.push_back(&c, "two");

    v.dump();

    typedef model::OrderedIdLog::VarDefName vn;
    vn* n = v.lookup("one");
    n->dump();
    vn* m = v.lookup("two");
    assert(m->i == m->vardef->_a);
    vn* p = v.lookup("three");
    assert(!p);

    v.eraseFrom(1);
    v.dump();

    assert(!v.lookup("two"));
    assert(!v.lookup(&b));
    assert(!v.lookup(&c));
    assert(v.lookup(&a));

    D d(3);
    D e(4);
    D f(5);

    v.push_back(&d,"dee");
    v.dump();
    v.push_back(&e,"eee");
    v.dump();
    v.push_back(&f,"eff");
    v.dump();

    v.eraseFrom(v.vec().size()-2);
    v.dump();

    assert(!v.lookup_def_by_odname("eff"));
    assert(v.lookup_def_by_odname("dee") == &d);

    v.push_back(&f,"eff");
    v.dump();
    
    v.erase(1);
    v.dump();

    assert(0);
    return 0;
}

#endif //  TESTING_OH

