// copyright (c) 2012, Jason Aten <j.e.aten@gmail.com>
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#ifndef _wisecrack_editory_h
#define _wisecrack_editory_h

#include <stdio.h>
#include <spug/RCBase.h>
#include <spug/RCPtr.h>

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

// interface
class LineEditor : public virtual spug::RCBase {
 public:

    /** get a string */
    virtual const char *gets(char *readbuf, int readsz, std::istream& ins) = 0;

    /** show the prompt */
    virtual void displayPrompt(std::ostream& outs) = 0;

    /** have we hit end of file? */
    virtual bool eof(std::istream& ins) = 0;

    /** how we want the line recorded */
    virtual void addToHistory(const char *line) = 0;

};


SPUG_RCPTR(LineEditor);

// factory
LineEditorPtr makeLineEditor(wisecrack::Repl *repl, int historySize = 600);

#ifdef LIBEDIT_FOUND
#define EDITLINE 1
#endif

#ifdef EDITLINE

// implementation 1
class LibEditLineEditor : public LineEditor {
 public:

    /** 
     * make a line editor
     *
     *  the libeditline implementation provides command
     *  history (up arrow), and emacs style line editing
     *  (ctrl-a, ctrl-e, ctrl-k, ctrl-y, etc). 
     */
    LibEditLineEditor(wisecrack::Repl *repl, int historySize = 600);
    virtual ~LibEditLineEditor();

    /** get a string */
    virtual const char *gets(char *readbuf, int readsz, std::istream& ins);

    /** show the prompt */
    virtual void displayPrompt(std::ostream& outs);

    /** have we hit end of file? */
    virtual bool eof(std::istream& ins);

    /** how we want the line recorded */
    virtual void addToHistory(const char *line);

 private:

    wisecrack::Repl *_repl;
    LibEditLine::EditLine *_editLine;
    LibEditLine::History *_editLineHistory;
    LibEditLine::HistEvent *_editLineHistoryEvent;

    bool  _eof;
};

#endif // end ifdef EDITLINE


// if you want to switch back to the 
// utterly simply line handling (no editing capabilities)
// then instantiate SimplestEditor instead.

// implementation 2
class SimplestEditor : public LineEditor {

 public:

    SimplestEditor(wisecrack::Repl *repl, int historySize = 600);
    virtual ~SimplestEditor();

    virtual const char *gets(char *readbuf, int readsz, std::istream& ins);

    virtual void displayPrompt(std::ostream& outs);

    virtual bool eof(std::istream& ins);

    virtual void addToHistory(const char *line);


 private:
    wisecrack::Repl *_repl;
    bool  _eof;
};


#endif
