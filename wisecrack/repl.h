// copyright (c) 2012, Jason Aten <j.e.aten@gmail.com>
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 
#ifndef REPL_H
#define REPL_H

#include <stdio.h>
#include <strings.h> // bzero
#include <string.h>  // strlen

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

    class Repl {

    public:

        //ctor
        Repl();
        bool done();
        void setDone();


        void prompt(FILE* fout);
        char* getPrompt();

        void read(FILE* fin);
        void eval();
        void print(FILE* fout);
        void run(FILE* fin, FILE* fout);

        long lineno();
        long nextlineno();
        long  getLastReadLineLen();

        /**
         * always right trimmed of whitespace
         */
        char* getLastReadLine();

        /**
         * any left whitespace trimmed too.
         */
        char* getTrimmedLastReadLine();

        /**
         * should we display lines numbers at
         *  the prompt?  returns previous value.
         */
        bool showLineNo(bool show);



    private:
        bool _alldone;
        long _lineno;

        const static int _readsz = 4096;
        char       _readbuf[_readsz];
        int        _readlen;

        bool  _showLineN;

        void trimr();

    };


} // end namespace wisecrack


// test driver
int test_wisecrack_main(int argc, char* argv[]);

#endif // REPL_H

