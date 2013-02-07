// copyright (c) 2012, Jason Aten <j.e.aten@gmail.com>
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include <stdio.h>
#include <stdlib.h>
#include "wisecrack/editor.h"
#include "wisecrack/repl.h"

#include <string>
#include <iostream>
using std::cout;
using std::cerr;
using std::endl;

// implementation of LineEditor using BSD licensed
//  libary called libeditline
//  from http://sourceforge.net/projects/libedit/

// and a simple fallback, in case the
//  first is not available.

//ifdef EDITLINE
extern "C" {

 namespace LibEditLine {
  #include "libedit/histedit.h"
 }

 static wisecrack::Repl* staticEditorRepl = 0;

 // callback for lib editline
 const char* get_prompt_for_editLine(LibEditLine::EditLine* editLine) {
    
    return staticEditorRepl->getPrompt();
 }

} // end extern C


LibEditLineEditor::LibEditLineEditor(wisecrack::Repl *repl, int historySize) 
    : 
    _editLine(0),
    _editLineHistory(0),
    _editLineHistoryEvent(0),
    _repl(repl),
    _eof(false) {

    assert(repl);
    staticEditorRepl = repl;
    
    _editLine = LibEditLine::el_init("crack", stdin, stdout, stderr);
    
    // register the callback function that the library 
    // uses to obtain the prompt string
    LibEditLine::el_set(_editLine, EL_PROMPT, get_prompt_for_editLine);
    LibEditLine::el_set(_editLine, EL_EDITOR, "emacs");
    
    _editLineHistory = LibEditLine::history_init();
    if (!_editLineHistory) {
        cerr << "editLine history could not be initialized! Aborting." << endl;
        assert(0);
        exit(1);
    }
    
    /* Set the size of the history */
    _editLineHistoryEvent = new LibEditLine::HistEvent;
    LibEditLine::history(_editLineHistory, 
                         _editLineHistoryEvent,
                         H_SETSIZE,
                         historySize);
    
    // set up the call back functions for history functionality
    el_set(_editLine, EL_HIST, LibEditLine::history, _editLineHistory);
}

LibEditLineEditor::~LibEditLineEditor() {
    
    if (_editLineHistoryEvent)
        delete _editLineHistoryEvent;

    _editLineHistoryEvent = 0;

    if (_editLineHistory)
        LibEditLine::history_end(_editLineHistory);
    
    _editLineHistory=0;
    
    if (_editLine)
        LibEditLine::el_end(_editLine);
    
    _editLine=0;

}

void LibEditLineEditor::addToHistory(const char *line) {
    LibEditLine::history(_editLineHistory, 
                         _editLineHistoryEvent, 
                         H_ENTER, 
                         line);
}

const char *LibEditLineEditor::gets(char* readbuf, int readsz, std::istream& ins) {
    // ignores ins. it shouldn't, but not obvious how to
    //  implement; at this point ins is here because
    //  SimplestEditor needs it.

    int count = 0;
    const char *r = 0;;
    r = LibEditLine::el_gets(_editLine, &count);

    if (r) {
        strncpy(readbuf, (const char*)r, readsz-1);
        size_t len = strlen(readbuf);
        if (readbuf[len-1] == '\n')
            readbuf[len-1]=0;
    } else {
        _eof = true;
    }
    return r;
}

void LibEditLineEditor::displayPrompt(std::ostream& outs) {
    // for editing the cmd line, the libedit library
    // has to know the length of the prompt and print
    // it by itself, so we do nothing here. Basically
    // this method is here to allow SimplestEditor
    // to do its thing, while we are a no-op.
}

bool LibEditLineEditor::eof(std::istream& ins) {
    return _eof;
}

//endif // end ifdef EDITLINE


SimplestEditor::SimplestEditor(wisecrack::Repl *repl, int historySize) 
    : _repl(repl) {

}

SimplestEditor::~SimplestEditor() {
    // cout << "~SimplestEditor dtor firing" << endl;
}

/** get a string */

// old version
//const char *SimplestEditor::gets(char* readbuf, int readsz, FILE* fin) {
//    return fgets(readbuf, readsz, fin);
// }

// new version with stream instead of FILE*.
const char *SimplestEditor::gets(char* readbuf, int readsz, std::istream& ins) {
    bzero(readbuf, readsz);
    std::string s;
    std::getline(ins, s);
    strncpy(readbuf, s.c_str(), readsz-1);
    return readbuf;
}

void SimplestEditor::displayPrompt(std::ostream& outs) {
    outs << _repl->getPrompt();
    outs.flush();
}

bool SimplestEditor::eof(std::istream& ins) {
    return ins.eof();
}

void SimplestEditor::addToHistory(const char *line) {

}


// factory
LineEditorPtr makeLineEditor(wisecrack::Repl *repl, int historySize) {

    // we always want the editline version to be available,
    // so Make It So.
   return new LibEditLineEditor(repl, historySize);

   // old: indef EDITLINE
   //   return new SimplestEditor(repl, historySize);
}
