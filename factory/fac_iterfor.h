// emacs edit mode for this file is -*- C++ -*-
// $Id: fac_iterfor.h,v 1.1 1996-07-08 08:18:49 stobbe Exp $

#ifndef INCL_ITERFOR_H
#define INCL_ITERFOR_H

/*
$Log: not supported by cvs2svn $
Revision 1.0  1996/05/17 10:59:40  stobbe
Initial revision

*/

#include <iostream.h>

class IteratedFor
{
private:
    int MAX;
    int FROM;
    int TO;
    int N;
    bool last;
    int * index;
    int * imax;
    void fill ( int from, int n );
public:
    IteratedFor( int from, int to, int max );
    IteratedFor( const IteratedFor & );
    ~IteratedFor();
    IteratedFor& operator= ( const IteratedFor & );
    int from() const { return FROM; }
    int to() const { return TO; }
    int n() const { return N; };
    int max() const { return MAX; }
    void nextiteration();
    void operator++ () { nextiteration(); }
    void operator++ ( int ) { nextiteration(); }
    bool iterations_left() const { return ! last; }
    int operator[] ( int ) const;
};

ostream& operator<< ( ostream &, const IteratedFor & );

#endif /* INCL_ITERFOR_H */
