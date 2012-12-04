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

#include <ctype.h> // isspace

namespace wisecrack {



    //ctor
    Repl::Repl()
        : _alldone(false),
          _lineno(0),
          _showLineN(true)
    {
        bzero(_readbuf, _readsz);
        set_default_prompt("crk");
        reset_prompt_to_default();
    }

    bool Repl::done() { return _alldone; }
    void Repl::setDone() { _alldone = true; }


    char* Repl::getPrompt() {
        static char _b[256];
        if (_showLineN) {
            sprintf(_b,"(%ld:%s)",_lineno, _prompt);
        } else {
            sprintf(_b,"(%s)",_prompt);
        }
        return _b;
    }

    void Repl::prompt(FILE* fout) {
        fprintf(fout,"%s ",getPrompt());
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
        trimr();
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
            nextlineno();
            prompt(fout);
            read(fin);
            eval();
            print(fout);
        }

        fprintf(fout,"\n");
    }

    long Repl::lineno() {
        return _lineno;
    }

    long Repl::nextlineno() {
        return ++_lineno;
    }

    char* Repl::getLastReadLine() {
        return _readbuf;
    }

    char* Repl::getTrimmedLastReadLine() {
        char *p = &_readbuf[0];
        while (p < &_readbuf[_readlen-1] && isspace(*p)) {
            ++p;
        }
        return p;
    }

    long Repl::getLastReadLineLen() {
        return _readlen;
    }


    // remove right side whitespace
    void Repl::trimr() {
        if (_readlen <=0) return;

        char* newp = &_readbuf[_readlen-1];
        while(newp > &_readbuf[0] && isspace(*newp)) {
            (*newp) ='\0';
            --newp;
            --_readlen;
        }
    }


    // should we display lines numbers at
    // the prompt?  returns previous value.
    bool Repl::showLineNo(bool show) {
        bool prev = _showLineN;
        _showLineN = show;
        return prev;
    }


    /**
     * set the default prompt
     */
    void Repl::set_default_prompt(const char* p) {
        bzero(_default_prompt, _promptsz);
        strncpy(_default_prompt,p,_promptsz-1);
    }
    
    /**
     * set a new prompt (useful for continuation ... lines)
     */
    void Repl::set_prompt(const char* p) {
        bzero(_prompt, _promptsz);
        strncpy(_prompt,p,_promptsz-1);        
    }
    
    /**
     * reset to using the default prompt
     */
    void Repl::reset_prompt_to_default() {
        set_prompt(_default_prompt);
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

