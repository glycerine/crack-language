// copyright (c) 2012, Jason Aten <j.e.aten@gmail.com>
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _wisecrack_editory_h
#define _wisecrack_editory_h

#include <stdio.h>

/**
 * interface to the editline library, or
 *  alternative implementation
 */

namespace LibEditLine {
    struct editline;
    typedef struct editline EditLine;
    struct history;
    typedef struct history History;
    struct HistEvent;
}

namespace wisecrack {
    class Repl;
}


class LineEditor {
 public:

    /** 
     * make a line editor
     *
     *  the libeditline implementation provides command
     *  history (up arrow), and emacs style line editing
     *  (ctrl-a, ctrl-e, ctrl-k, ctrl-y, etc). 
     */
    LineEditor(wisecrack::Repl* repl, int historySize = 600);
    virtual ~LineEditor();

    /** get a string */
    virtual const char *gets(char* readbuf, int readsz, FILE* fin);

    /** show the prompt */
    virtual void displayPrompt(FILE* fout);

    /** have we hit end of file? */
    virtual bool eof(FILE* fin);

    /** how we want the line recorded */
    virtual void addToHistory(const char *line);

 protected:
    wisecrack::Repl* _repl;

 private:

    LibEditLine::EditLine *_editLine;
    LibEditLine::History *_editLineHistory;
    LibEditLine::HistEvent *_editLineHistoryEvent;

    bool  _eof;
};

// if you want to switch back to the 
// utterly simply line handling (no editing capabilities)
// then instantiate SimplestEditor instead.

class SimplestEditor : virtual public LineEditor {

 public:

    SimplestEditor(wisecrack::Repl* repl, int historySize = 600);
    virtual ~SimplestEditor();

    virtual const char *gets(char* readbuf, int readsz, FILE* fin);

    virtual void displayPrompt(FILE* fout);

    virtual bool eof(FILE* fin);

    virtual void addToHistory(const char *line);
};


#endif
