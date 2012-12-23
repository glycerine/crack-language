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
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include "spug/Exception.h"
#include "wisecrack/editor.h"

namespace builder {
    class Builder;
};

namespace llvm {
    class Function;
}

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

    // crude global for better debugging at the repl
    class Repl;
    extern Repl *globalRepl;
    
    /** thrown on ctrl-c SIGINT interrupt while at the repl*/
    class ExceptionCtrlC : public spug::Exception {
    public:
        ExceptionCtrlC() {}
        ExceptionCtrlC(const char *msg) : Exception(msg) {}
    };

    class Repl {

    public:

        //ctor
        Repl();
        ~Repl();
        bool done();
        void setDone();

        /** only employ one of LineEditor or SimplestEditor,
         *   one or the other, not both, for lineEd */

        /** uses lib editline  */
        LineEditor _lineEd;

        //* uses simplest fgets, no fancy library */
        //SimplestEditor _lineEd;

        void prompt(FILE *fout);
        char *getPrompt();

        /** read one line, ctrl-c interrupt will throw */
        void read(FILE *fin);

        void eval();
        void print(FILE *fout);
        void run(FILE *fin, FILE *fout);

        /**
         * get the current line number.
         */
        long lineNumber();

        /**
         * advance the current line number by one.
         */
        long nextLineNumber();

        /**
         * get the length of the last current line.
         */
        long  getLastReadLineLen();

        /**
         * Returns the curent line, which is always right trimmed of (trailing) whitespace.
         *  Probably the desired default.
         */
        char *getLastReadLine();

        /**
         * Delete the current line and replace it with tbr, to be returned
         *  on the next call to getlastReadLine(). Allows the repl to
         *  have special aliases that get re-written into actual code.
         */
        void setNextLine(const char *tbr);

        /**
         * any left whitespace trimmed too. Probably not what you want
         *  as a default, given i-strings.
         */
        char *getTrimmedLastReadLine();

        /**
         * should we display lines numbers at
         *  the prompt?  returns previous value.
         */
        bool showLineNo(bool show);

        /**
         * set the default prompt
         */
        void set_default_prompt(const char *p);
        
        /**
         * set a new prompt (useful for continuation lines)
         */
        void set_prompt(const char *p);

        /**
         * reset to using the default prompt
         */
        void resetPromptToDefault();

        /** src, a stream version, is what other components expect */
        std::stringstream src;

        /** restart src stringstream with an empty string */
        void resetSrcToEmpty();

        /** return true if more input obtained. */
        bool getMoreInput();

        /** set (get) debug level */
        void set_debugLevel(int level);
        int debugLevel();

        /** command line history */
        typedef std::vector<std::string> histlist;
        histlist history;

        /** turn on (off) save history to .crkhist file */
        void histon();
        void histoff();
        bool hist(); // status: true if on.

        /** write to file, if hist() is true. Appends newline and flushes.. */
        void loghist(const char *line);

        const char *getReplCmdStart();
        void   setReplCmdStart(const char *s);

        /** return 0 if not start of repl command,
         *   otherwise return pointer to the suffix
         *   that follows the '.' or '\' or whatever
         *   the repl start character(s) are.
         */
        const char *replCmd(const char *s);
        
        /** builder to clean up with */
        void setBuilder(builder::Builder *b);
        builder::Builder *builder();

        /** set of functions already cleaned up by eraseSection();
         *   listed here so we don't delete them twice.
         */
        typedef std::set<llvm::Function*> gset;
        gset goneSet;

    private:
        bool _alldone;
        long _lineno;

        const static int _readsz = 4096;
        char       _readbuf[_readsz];
        int        _readlen;

        bool  _showLineN;

        void trimr();

        const static int _promptsz = 256;
        char             _prompt[_promptsz];
        char             _default_prompt[_promptsz];

        int _debugLevel;
        FILE *_crkhist; // history file.
        bool  _histon;  // write to history file?

        // recognize repl commands that start with
        // this character (buffer is 8 in case
        // someone wants to do utf8).
        // This is usually '.'. But could be '\'.
        const static int  _rcs_sz = 8;
        char              _repl_cmd_start[_rcs_sz];

        // Builder to cleanup with
        builder::Builder *_bdr;
    };


} // end namespace wisecrack


// test driver
int test_wisecrack_main(int argc, char *argv[]);

#endif // REPL_H

