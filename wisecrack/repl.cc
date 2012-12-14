// copyright (c) 2012, Jason Aten <j.e.aten@gmail.com>
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "repl.h"
#include "spug/Exception.h"
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>

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

    Repl* globalRepl = 0;

    void print_signal_mask() {
        
        sigset_t print_cur_sset;
        sigset_t print_old_sset;
        
        bzero(&print_cur_sset, sizeof(sigset_t));
        bzero(&print_old_sset, sizeof(sigset_t));
        sigemptyset(&print_cur_sset);
        sigemptyset(&print_old_sset);
        
        int how = SIG_SETMASK;
        if (-1 == sigprocmask(how, NULL, &print_old_sset)) {
            perror("sigprocmask returned -1");
            exit(1);
        }
        
        if (sigismember(&print_old_sset, SIGINT)) {
            printf("SIGINT is blocked\n");
        } else {
            printf("SIGINT is not blocked\n");
        }
        
    }


    jmp_buf   ctrl_c_jb;
    sigset_t  sigset;

    const static int caught_ctrl_c = -1;
    const static int normal_finish_after_read = -2;

    // gdb setup for testing ctrl-c:
    // (gdb) handle SIGINT nostop print pass 

    /** signal handler for ctrl-c */
    void repl_sa_sigaction(int signum, siginfo_t* si, void* vs) {
        
        //printf(" [ctrl-c]\n");

        siglongjmp(ctrl_c_jb, caught_ctrl_c);
    }

/** setup handler on ctrl-c press */

struct sigaction old_sigint_action;
void init_ctrl_c_handling() {

    struct sigaction sa;
    bzero(&sa,sizeof(struct sigaction));

    sa.sa_sigaction = &repl_sa_sigaction;
    sa.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(SIGINT, &sa, &old_sigint_action)) {
        perror("error: wisecrack::init_ctrl_c_handling() could not setup "
               "SIGINT signal handler. Aborting.");
        exit(1);
    }
    //    print_signal_mask();
}




    //ctor
    Repl::Repl()
        : _alldone(false),
          _lineno(0),
          _showLineN(true),
          _debuglevel(0),
          _crkhist(0),
          _histon(false)
    {
        bzero(_readbuf, _readsz);
        set_default_prompt("crk");
        reset_prompt_to_default();

        // last write wins, but we only expect to use
        // this for debugging situations.
        globalRepl = this;

    }
    bool Repl::hist() { return _histon; }


    void Repl::histon() {
        // open history file
        _crkhist = fopen(".crkhist","a");
        if (_crkhist) {
            _histon = true;
        } else {
            perror("could not open history file '.crkhist'");
        }
    }

    void Repl::histoff() {
        _histon = false;
        if (_crkhist) {
            fflush(_crkhist);
        }
    }

    void Repl::loghist(const char* line) {
        if (_histon && _crkhist) {
            fwrite(line,strlen(line),1,_crkhist);
            fwrite("\n",1,1,_crkhist);
            fflush(_crkhist);
        }
    }

    Repl::~Repl() {
        if (_crkhist) fclose(_crkhist);
    }

    bool Repl::done() { return _alldone; }
    void Repl::setDone() { _alldone = true; }

    void Repl::set_debuglevel(int level) { _debuglevel = level; }
    int Repl::debuglevel() { return _debuglevel; }


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


    /** 
     *  read(fin) takes a line of input from fin, and if ctrl-c SIGINT is detected,
     *     then it throws an ExceptionCtrlC.
     */
    void Repl::read(FILE* fin) {
        
        _readlen = 0;
        bzero(_readbuf, _readsz);

        volatile char* r = 0;
        volatile int rc = 0;

        init_ctrl_c_handling();
        rc = sigsetjmp(ctrl_c_jb, 1);

        switch (rc) {

            case  0: {
                // initial time, no siglongjmp yet.
                r = fgets(_readbuf, _readsz, fin);

                siglongjmp(ctrl_c_jb, normal_finish_after_read);  
                break;
            }

            case caught_ctrl_c: {
                throw wisecrack::ExceptionCtrlC();                
                break;
            }

            case normal_finish_after_read: {
                break;
            }

            default: {
                fprintf(stderr, "Wierd and unhandled return in repl.cc  switch(sigsetjmp) , rc = %d\n", rc);
                assert(0);                
                break;
            }

        }

        if (NULL == r) {
            // eof or error
            if (feof(fin)) {
                
                // stub
                setDone();
                return;
            }
        }
        
        // trimr needs _readlen set correctly.
        _readlen = strlen(_readbuf);
        trimr();

        history.push_back(_readbuf);
    }


    void Repl::set_next_line(const char* tbr) {
        bzero(_readbuf, _readsz);
        strncpy(_readbuf, tbr, _readsz-1);
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


    /** restart src stringstream with an empty string */
    void Repl::reset_src_to_empty() { 
        src.str(std::string());
        // clear eof flags so we can read again
        src.clear();
    }

    /** return true if more input obtained. */
    bool Repl::get_more_input() {
        set_prompt("...");
        
        nextlineno();
        prompt(stdout);
        read(stdin);

        // crude, but perhaps effective?
        //  if we get no input, tell the Toker so.
        if (*getTrimmedLastReadLine() == '\0') {
            return false;
        }

        src.clear();
        src << "\n";
        src << getLastReadLine();

        reset_prompt_to_default();

        return true;
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

