scip-maxsat
===========

Max-SAT frontend for SCIP <http://scip.zib.de/>.

It convert problems of MAX-SAT families into mixed integer programming
(MIP) problems and solve them using SCIP.

Usage
-----

    scip-maxsat [file.cnf|file.wcnf]

About SCIP
----------

Constraint integer programming (CIP) is a novel paradigm which
integrates constraint programming (CP), mixed integer programming
(MIP), and satisfiability (SAT) modeling and solving techniques.

SCIP (Solving Constraint Integer Programs) is currently one of the
fastest non-commercial mixed integer programming (MIP) solvers. It is
also a framework for constraint integer programming and
branch-cut-and-price. It allows total control of the solution process
and the access of detailed information down to the guts of the solver.
