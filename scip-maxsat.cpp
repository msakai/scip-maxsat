#define __STDC_FORMAT_MACROS

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <vector>

#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "scip/scip.h"
#include "scip/scipdefplugins.h"

extern SCIP_RETCODE SCIPincludeEventHdlrBestsol(SCIP* scip);

SCIP_VAR **xs;

static
int read_wcnf(SCIP *scip, const char *filename)
{
    FILE *file = fopen(filename, "r");
    char line[1024*128];
    int nv, nc;
    int64_t top = -1;
    bool isWCNF = 0;

    if (!file) {
        fprintf(stderr, "failed to open file: %s\n", filename);
        exit(1);
    }

    while (1) {
        fgets(line, sizeof(line), file);
        if (line[0] == 'c')
            continue;
        else if (line[0] == 'p') {
            int ret = sscanf(line, "p cnf %d %d", &nv, &nc);
            if (ret == 2) goto BODY;

            ret = sscanf(line, "p wcnf %d %d %"PRId64, &nv, &nc, &top);
            if (ret >= 2) {
                isWCNF = 1;
                goto BODY;
            }
        }

        fprintf(stderr, "unexpected line: %s\n", line);
        exit(1);
    }

BODY:
    SCIP_VAR *x0 = NULL;
    SCIP_CALL_ABORT( SCIPcreateVarBasic(scip, &x0, "x0", 1, 1, 0, SCIP_VARTYPE_BINARY) );
    SCIP_CALL_ABORT( SCIPaddVar(scip, x0) );

    xs = new SCIP_VAR*[1+nv];
    SCIP_CALL_ABORT( SCIPsetObjsense(scip, SCIP_OBJSENSE_MINIMIZE) );
    for (int i = 1; i <= nv; i++) {
        SCIP_VAR *x = NULL;
        char name[128];
        snprintf(name, sizeof(name), "x%d", i);
        SCIP_CALL_ABORT( SCIPcreateVarBasic(scip, &x, name, 0, 1, 0, SCIP_VARTYPE_BINARY) );
        SCIP_CALL_ABORT( SCIPaddVar(scip, x) );
        xs[i] = x;
    }

    for (int i = 1; i <= nc; i++) {
        int64_t cost = 1;
        if (isWCNF) fscanf(file, " %"PRId64, &cost);

        std::vector<int> lits;
        while (1) {
            int lit;
            fscanf(file, "%d", &lit);
            if (lit == 0) break;
            lits.push_back(lit);
        }

        if (cost != top && lits.size() == 1) {
            int lit = lits[0];
            if (lit > 0) {
                int v = lit;
                // obj += cost*(1 - v)
                SCIP_CALL_ABORT( SCIPaddVarObj(scip, x0, cost) );
                SCIP_CALL_ABORT( SCIPaddVarObj(scip, xs[v], - cost) );
            } else {
                int v = - lit;
                // obj += cost*v
                SCIP_CALL_ABORT( SCIPaddVarObj(scip, xs[v], cost) );
            }
            continue;
        }

        char name[128];
        snprintf(name, sizeof(name), "c%d", i);
        SCIP_CONS* cons = NULL;
        SCIP_CALL_ABORT( SCIPcreateConsBasicLinear(scip, &cons, name, 0, NULL, NULL, -SCIPinfinity(scip), SCIPinfinity(scip)) );

        if (cost != top) {
            SCIP_VAR *r = NULL;
            snprintf(name, sizeof(name), "r%d", i);
            SCIP_CALL_ABORT( SCIPcreateVarBasic(scip, &r, name, 0, 1, cost, SCIP_VARTYPE_BINARY) );
            SCIP_CALL_ABORT( SCIPaddVar(scip, r) );
            SCIP_CALL_ABORT( SCIPaddCoefLinear(scip, cons, r, 1.0) );
        }

        int lb = 1;
        for (std::vector<int>::iterator j = lits.begin(); j != lits.end(); j++) {
            int lit = *j;
            if (lit > 0) {
                SCIP_CALL_ABORT( SCIPaddCoefLinear(scip, cons, xs[lit], 1.0) );
            } else {
                SCIP_CALL_ABORT( SCIPaddCoefLinear(scip, cons, xs[-lit], -1.0) );
                lb--;
            }
        }

        SCIP_CALL_ABORT( SCIPchgLhsLinear(scip, cons, lb) );
        SCIP_CALL_ABORT( SCIPaddCons(scip, cons) );
        SCIP_CALL_ABORT( SCIPreleaseCons(scip, &cons) );
    }

    fclose(file);
    return nv;
}

static
SCIP_DECL_MESSAGEWARNING(messageWarning)
{
    fprintf(file, "c WARNING: %s", msg);
    fflush(file);
}

static
SCIP_DECL_MESSAGEDIALOG(messageDialog)
{
    fprintf(file, "c %s", msg);
    fflush(file);
}

static
SCIP_DECL_MESSAGEINFO(messageInfo)
{
    fprintf(file, "c %s", msg);
    fflush(file);
}

static
void print_model(SCIP *scip, int nv)
{
    SCIP_SOL *sol = SCIPgetBestSol(scip);

    for (int i = 1; i <= nv; i++) {
        if (i % 10 == 1) {
            if (i != 1) puts(""); // new line
            fputs("v", stdout);
        }
        SCIP_Real val = SCIPgetSolVal(scip, sol, xs[i]);
        if (val >= 0.5)
            printf(" %d", i);
        else
            printf(" -%d", i);
    }
    puts(""); // new line
}

static
int64_t get_wc_time()
{
  struct timeval r;
  gettimeofday (&r, NULL);
  return (((int64_t) r.tv_sec) * 1000000) + r.tv_usec;
}

static
int64_t get_cpu_time()
{
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  return (((int64_t) usage.ru_utime.tv_sec) * 1000000) + usage.ru_utime.tv_usec +
         (((int64_t) usage.ru_stime.tv_sec) * 1000000) + usage.ru_stime.tv_usec;
}

SCIP *scip_orig;

int64_t start_wc_time;

int main(int argc, char **argv)
{
    if (1 >= argc) {
        fprintf(stderr, "USAGE: scip-maxsat [file.cnf|file.wcnf]");
        exit(1);
    }
    char *filename = argv[1];
    start_wc_time = get_wc_time();

    SCIP *scip = NULL;
    SCIP_CALL_ABORT( SCIPcreate(&scip) );
    scip_orig = scip;
    SCIP_CALL_ABORT( SCIPincludeDefaultPlugins(scip) );
    SCIP_CALL_ABORT( SCIPincludeEventHdlrBestsol(scip) );

    SCIP_MESSAGEHDLR *messagehdlr = NULL;
    SCIP_CALL_ABORT( SCIPmessagehdlrCreate(&messagehdlr, TRUE, NULL, FALSE,
      messageWarning, messageDialog, messageInfo,
      NULL, NULL) );
    SCIP_CALL_ABORT( SCIPsetMessagehdlr(scip, messagehdlr) );

    SCIPprintVersion(scip, NULL);

    SCIP_CALL_ABORT( SCIPcreateProbBasic(scip, filename) );
    int nv = read_wcnf(scip, filename);

    SCIP_CALL_ABORT( SCIPsolve(scip) );

    SCIP_STATUS status = SCIPgetStatus(scip);
    switch (status) {
    case SCIP_STATUS_OPTIMAL:
        puts("s OPTIMUM FOUND");
        print_model(scip, nv);
        break;
    case SCIP_STATUS_INFEASIBLE:
        puts("s UNSATISFIABLE");
        break;
    default:
        printf("c SCIPgetStatus() returns %d\n", status);
        puts("s UNKNOWN");
        exit(1);
    }

    fflush(stdout);
    SCIPfree(&scip);
    return 0;
}



// Following part is copied and modified from bestsol.c

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2013 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define EVENTHDLR_NAME         "bestsol"
#define EVENTHDLR_DESC         "event handler for best solutions found"

/** copy method for event handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_EVENTCOPY(eventCopyBestsol) 
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* call inclusion method of event handler */
   SCIP_CALL( SCIPincludeEventHdlrBestsol(scip) );

   return SCIP_OKAY;
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitBestsol)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* notify SCIP that your event handler wants to react on the event type best solution found */
   SCIP_CALL( SCIPcatchEvent( scip, SCIP_EVENTTYPE_BESTSOLFOUND, eventhdlr, NULL, NULL) );

   return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTEXIT(eventExitBestsol)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   
   /* notify SCIP that your event handler wants to drop the event type best solution found */
   SCIP_CALL( SCIPdropEvent( scip, SCIP_EVENTTYPE_BESTSOLFOUND, eventhdlr, NULL, -1) );

   return SCIP_OKAY;
}

/** execution method of event handler */
static
SCIP_DECL_EVENTEXEC(eventExecBestsol)
{  /*lint --e{715}*/
   SCIP_SOL* bestsol;
   int64_t solvalue;

   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(event != NULL);
   assert(scip != NULL);
   assert(SCIPeventGetType(event) == SCIP_EVENTTYPE_BESTSOLFOUND);

   SCIPdebugMessage("exec method of event handler for best solution found\n");

   if (scip != scip_orig) return SCIP_OKAY;
   
   bestsol = SCIPgetBestSol(scip);
   assert(bestsol != NULL);
   solvalue = llround(SCIPgetSolOrigObj(scip, bestsol));
   
   /* print best solution value */
   int64_t now = get_wc_time();
   int64_t cpu_time = get_cpu_time();
   printf("o %"PRId64" wctime=%fs cputime=%fs\n", solvalue, (now - start_wc_time) / 1000000.0, cpu_time / 1000000.0);
   fflush(stdout);
   
   return SCIP_OKAY;
}

/** includes event handler for best solution found */
SCIP_RETCODE SCIPincludeEventHdlrBestsol(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_EVENTHDLR* eventhdlr;
   eventhdlrdata = NULL;
   
   eventhdlr = NULL;
   /* create event handler for events on watched variables */
   SCIP_CALL( SCIPincludeEventhdlrBasic(scip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC, eventExecBestsol, eventhdlrdata) );
   assert(eventhdlr != NULL);

   SCIP_CALL( SCIPsetEventhdlrCopy(scip, eventhdlr, eventCopyBestsol) );
   SCIP_CALL( SCIPsetEventhdlrInit(scip, eventhdlr, eventInitBestsol) );
   SCIP_CALL( SCIPsetEventhdlrExit(scip, eventhdlr, eventExitBestsol) );
   
   return SCIP_OKAY;
}
