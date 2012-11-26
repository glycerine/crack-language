// copyright (c) 2012, Jason Aten <j.e.aten@gmail.com>
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "repl.h"

//
// wisecrack: an interpreter for crack
//

// Repl, read-eval-print-loop : an interpreter
//
//   deliberately kept as simple as possible
//    in order to enable experimentation to find the
//    overall structure.
//    e.g. use simple FILE*, no iostream, use no readline, 
//         no libedit history, etc.
//    so we can focus on the read-eval interaction, which
//    is the tricky part.


namespace wisecrack {


    //ctor
    Repl::Repl()
        : _alldone(false)
    {}

    bool Repl::done() { return _alldone; }
    void Repl::setDone() { _alldone = true; }


    void Repl::prompt(FILE* fout) {
        fprintf(fout,"(crack) ");
        fflush(fout);
    }


    void Repl::read(FILE* fin) {
        
        _readlen = 0;
        bzero(_readbuf, _readsz);
        char* r = fgets(_readbuf, _readsz, fin);

        if (NULL == r) {
            // eof or error
            if (feof(fin)) {
                
                // stub
                setDone();
                return;
            }
        }


        _readlen = strlen(_readbuf);
        if (_readbuf[_readlen-1] == '\n') {
            _readbuf[_readlen-1] = '\0';
            --_readlen;
        }
    }


    void Repl::eval() {

    }

    void Repl::print(FILE* fout) {
        if (done()) return;
        fprintf(fout,
                "*print* called, _readlen(%d), _readbuf: '%s'\n",
                _readlen,
                _readbuf);
    }


    void Repl::run(FILE* fin, FILE* fout) {

        while(!done()) {
            prompt(fout);
            read(fin);
            eval();
            print(fout);
        }

        fprintf(fout,"\n");
    }


} // end namespace wisecrack


// test driver
int test_wisecrack_main(int argc, char* argv[]) {

    wisecrack::Repl r;
    r.run(stdin,stdout);
    return 0;
}


#ifdef WISECRACK_STANDALONE
int main(int argc, char* argv[]) {
    return test_wisecrack_main(argc,argv);
}
#endif

