/****************************************
*  Computer Algebra System SINGULAR     *
****************************************/
/* $Id$ */
/*
* ABSTRACT: numbers in a rational function field K(t_1, .., t_s) with
*           transcendental variables t_1, ..., t_s, where s >= 1.
*           Denoting the implemented coeffs object by cf, then these numbers
*           are represented as quotients of polynomials living in the
*           polynomial ring K[t_1, .., t_s] represented by cf->extring.
*
*           An element of K(t_1, .., t_s) may have numerous representations,
*           due to the possibility of common polynomial factors in the
*           numerator and denominator. This problem is handled by a
*           cancellation heuristic: Each number "knows" its complexity
*           which is 0 if and only if common factors have definitely been
*           cancelled, and some positive integer otherwise.
*           Each arithmetic operation of two numbers with complexities c1
*           and c2 will result in a number of complexity c1 + c2 + some
*           penalty (specific for each arithmetic operation; see constants
*           in the *.h file). Whenever the resulting complexity exceeds a
*           certain threshold (see constant in the *.h file), then the
*           cancellation heuristic will call 'factory' to compute the gcd
*           and cancel it out in the given number. (This definite cancel-
*           lation will also be performed at the beginning of ntWrite,
*           ensuring that any output is free of common factors.
*           For the special case of K = Q (i.e., when computing over the
*           rationals), this definite cancellation procedure will also take
*           care of nested fractions: If there are fractional coefficients
*           in the numerator or denominator of a number, then this number
*           is being replaced by a quotient of two polynomials over Z, or
*           - if the denominator is a constant - by a polynomial over Q.
*/
#define TRANSEXT_PRIVATES

#include "config.h"
#include <misc/auxiliary.h>

#include <omalloc/omalloc.h>

#include <reporter/reporter.h>

#include <coeffs/coeffs.h>
#include <coeffs/numbers.h>
#include <coeffs/longrat.h>

#include <polys/monomials/ring.h>
#include <polys/monomials/p_polys.h>
#include <polys/simpleideals.h>

#ifdef HAVE_FACTORY
#include <polys/clapsing.h>
#include <polys/clapconv.h>
#include <factory/factory.h>
#endif

#include "ext_fields/transext.h"
#include "prCopy.h"

/* constants for controlling the complexity of numbers */
#define ADD_COMPLEXITY 1   /**< complexity increase due to + and - */
#define MULT_COMPLEXITY 2   /**< complexity increase due to * and / */
#define BOUND_COMPLEXITY 10   /**< maximum complexity of a number */

#define NUMIS1(f) (p_IsConstant(NUM(f), cf->extRing) && \
                   n_IsOne(p_GetCoeff(NUM(f), cf->extRing), \
                           cf->extRing->cf))
                   /**< TRUE iff num. represents 1 */
#define COM(f) f->complexity


#ifdef LDEBUG
#define ntTest(a) ntDBTest(a,__FILE__,__LINE__,cf)
BOOLEAN  ntDBTest(number a, const char *f, const int l, const coeffs r);
#else
#define ntTest(a) (TRUE)
#endif

/// Our own type!
static const n_coeffType ID = n_transExt;

/* polynomial ring in which the numerators and denominators of our
   numbers live */
#define ntRing cf->extRing

/* coeffs object in which the coefficients of our numbers live;
 * methods attached to ntCoeffs may be used to compute with the
 * coefficients of our numbers, e.g., use ntCoeffs->nAdd to add
 * coefficients of our numbers */
#define ntCoeffs cf->extRing->cf



omBin fractionObjectBin = omGetSpecBin(sizeof(fractionObject));

/// forward declarations
BOOLEAN  ntGreaterZero(number a, const coeffs cf);
BOOLEAN  ntGreater(number a, number b, const coeffs cf);
BOOLEAN  ntEqual(number a, number b, const coeffs cf);
BOOLEAN  ntIsOne(number a, const coeffs cf);
BOOLEAN  ntIsMOne(number a, const coeffs cf);
BOOLEAN  ntIsZero(number a, const coeffs cf);
number   ntInit(int i, const coeffs cf);
int      ntInt(number &a, const coeffs cf);
number   ntNeg(number a, const coeffs cf);
number   ntInvers(number a, const coeffs cf);
number   ntAdd(number a, number b, const coeffs cf);
number   ntSub(number a, number b, const coeffs cf);
number   ntMult(number a, number b, const coeffs cf);
number   ntDiv(number a, number b, const coeffs cf);
void     ntPower(number a, int exp, number *b, const coeffs cf);
number   ntCopy(number a, const coeffs cf);
void     ntWrite(number &a, const coeffs cf);
number   ntRePart(number a, const coeffs cf);
number   ntImPart(number a, const coeffs cf);
number   ntGetDenom(number &a, const coeffs cf);
number   ntGetNumerator(number &a, const coeffs cf);
number   ntGcd(number a, number b, const coeffs cf);
number   ntLcm(number a, number b, const coeffs cf);
int      ntSize(number a, const coeffs cf);
void     ntDelete(number * a, const coeffs cf);
void     ntCoeffWrite(const coeffs cf, BOOLEAN details);
number   ntIntDiv(number a, number b, const coeffs cf);
const char * ntRead(const char *s, number *a, const coeffs cf);
static BOOLEAN ntCoeffIsEqual(const coeffs cf, n_coeffType n, void * param);

void heuristicGcdCancellation(number a, const coeffs cf);
void definiteGcdCancellation(number a, const coeffs cf,
                             BOOLEAN simpleTestsHaveAlreadyBeenPerformed);
void handleNestedFractionsOverQ(fraction f, const coeffs cf);

#ifdef LDEBUG
BOOLEAN ntDBTest(number a, const char *f, const int l, const coeffs cf)
{
  assume(getCoeffType(cf) == ID);
  fraction t = (fraction)a;
  if (IS0(t)) return TRUE;
  assume(NUM(t) != NULL);   /**< t != 0 ==> numerator(t) != 0 */
  p_Test(NUM(t), ntRing);
  if (!DENIS1(t)) 
  {
    p_Test(DEN(t), ntRing);
    if(p_IsConstant(DEN(t),ntRing) && (n_IsOne(pGetCoeff(DEN(t)),ntRing->cf)))
    {
      Print("?/1 in %s:%d\n",f,l);
       return FALSE;
    }
  }
  return TRUE;
}
#endif

/* returns the bottom field in this field extension tower; if the tower
   is flat, i.e., if there is no extension, then r itself is returned;
   as a side-effect, the counter 'height' is filled with the height of
   the extension tower (in case the tower is flat, 'height' is zero) */
static coeffs nCoeff_bottom(const coeffs r, int &height)
{
  assume(r != NULL);
  coeffs cf = r;
  height = 0;
  while (nCoeff_is_Extension(cf))
  {
    assume(cf->extRing != NULL); assume(cf->extRing->cf != NULL);
    cf = cf->extRing->cf;
    height++;
  }
  return cf;
}

BOOLEAN ntIsZero(number a, const coeffs cf)
{
  ntTest(a);
  return (IS0(a));
}

void ntDelete(number * a, const coeffs cf)
{
  fraction f = (fraction)(*a);
  if (IS0(f)) return;
  p_Delete(&NUM(f), ntRing);
  if (!DENIS1(f)) p_Delete(&DEN(f), ntRing);
  omFreeBin((ADDRESS)f, fractionObjectBin);
  *a = NULL;
}

BOOLEAN ntEqual(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);

  /// simple tests
  if (a == b) return TRUE;
  if ((IS0(a)) && (!IS0(b))) return FALSE;
  if ((IS0(b)) && (!IS0(a))) return FALSE;

  /// cheap test if gcd's have been cancelled in both numbers
  fraction fa = (fraction)a;
  fraction fb = (fraction)b;
  if ((COM(fa) == 1) && (COM(fb) == 1))
  {
    poly f = p_Add_q(p_Copy(NUM(fa), ntRing),
                     p_Neg(p_Copy(NUM(fb), ntRing), ntRing),
                     ntRing);
    if (f != NULL) { p_Delete(&f, ntRing); return FALSE; }
    if (DENIS1(fa) && DENIS1(fb))  return TRUE;
    if (DENIS1(fa) && !DENIS1(fb)) return FALSE;
    if (!DENIS1(fa) && DENIS1(fb)) return FALSE;
    f = p_Add_q(p_Copy(DEN(fa), ntRing),
                p_Neg(p_Copy(DEN(fb), ntRing), ntRing),
                ntRing);
    if (f != NULL) { p_Delete(&f, ntRing); return FALSE; }
    return TRUE;
  }

  /* default: the more expensive multiplication test
              a/b = c/d  <==>  a*d = b*c */
  poly f = p_Copy(NUM(fa), ntRing);
  if (!DENIS1(fb)) f = p_Mult_q(f, p_Copy(DEN(fb), ntRing), ntRing);
  poly g = p_Copy(NUM(fb), ntRing);
  if (!DENIS1(fa)) g = p_Mult_q(g, p_Copy(DEN(fa), ntRing), ntRing);
  poly h = p_Add_q(f, p_Neg(g, ntRing), ntRing);
  if (h == NULL) return TRUE;
  else
  {
    p_Delete(&h, ntRing);
    return FALSE;
  }
}

number ntCopy(number a, const coeffs cf)
{
  ntTest(a);
  if (IS0(a)) return NULL;
  fraction f = (fraction)a;
  poly g = p_Copy(NUM(f), ntRing);
  poly h = NULL; if (!DENIS1(f)) h = p_Copy(DEN(f), ntRing);
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  DEN(result) = h;
  COM(result) = COM(f);
  return (number)result;
}

number ntGetNumerator(number &a, const coeffs cf)
{
  ntTest(a);
  definiteGcdCancellation(a, cf, FALSE);
  if (IS0(a)) return NULL;
  fraction f = (fraction)a;
  poly g = p_Copy(NUM(f), ntRing);
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  DEN(result) = NULL;
  COM(result) = 0;
  return (number)result;
}

number ntGetDenom(number &a, const coeffs cf)
{
  ntTest(a);
  definiteGcdCancellation(a, cf, FALSE);
  fraction f = (fraction)a;
  poly g;
  if (IS0(f) || DENIS1(f)) g = p_One(ntRing);
  else g = p_Copy(DEN(f), ntRing);
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  DEN(result) = NULL;
  COM(result) = 0;
  return (number)result;
}

BOOLEAN ntIsOne(number a, const coeffs cf)
{
  ntTest(a);
  definiteGcdCancellation(a, cf, FALSE);
  fraction f = (fraction)a;
  return (f!=NULL) && DENIS1(f) && NUMIS1(f);
}

BOOLEAN ntIsMOne(number a, const coeffs cf)
{
  ntTest(a);
  definiteGcdCancellation(a, cf, FALSE);
  fraction f = (fraction)a;
  if ((f==NULL) || (!DENIS1(f))) return FALSE;
  poly g = NUM(f);
  if (!p_IsConstant(g, ntRing)) return FALSE;
  return n_IsMOne(p_GetCoeff(g, ntRing), ntCoeffs);
}

/// this is in-place, modifies a
number ntNeg(number a, const coeffs cf)
{
  ntTest(a);
  if (!IS0(a))
  {
    fraction f = (fraction)a;
    NUM(f) = p_Neg(NUM(f), ntRing);
  }
  return a;
}

number ntImPart(number a, const coeffs cf)
{
  ntTest(a);
  return NULL;
}

number ntInit_bigint(number longratBigIntNumber, const coeffs src, const coeffs cf)
{
  assume( cf != NULL );

  const ring A = cf->extRing;

  assume( A != NULL );

  const coeffs C = A->cf;

  assume( C != NULL );

  number n = n_Init_bigint(longratBigIntNumber, src, C);

  if ( n_IsZero(n, C) )
  {
    n_Delete(&n, C);
    return NULL;
  }    

  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);

  NUM(result) = p_NSet(n, A);
  DEN(result) = NULL;
  COM(result) = 0;
  return (number)result;
}

   
number ntInit(int i, const coeffs cf)
{
  if (i == 0) return NULL;
  else
  {
    fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
    NUM(result) = p_ISet(i, ntRing);
    //DEN(result) = NULL; // done by omAlloc0Bin
    //COM(result) = 0; // done by omAlloc0Bin
    return (number)result;
  }
}

number ntInit(poly p, const coeffs cf)
{
  if (p == 0) return NULL;
  else
  {
    fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
    NUM(result) = p;
    DEN(result) = NULL;
    COM(result) = 0;
    return (number)result;
  }
}

int ntInt(number &a, const coeffs cf)
{
  ntTest(a);
  if (IS0(a)) return 0;
  definiteGcdCancellation(a, cf, FALSE);
  fraction f = (fraction)a;
  if (!DENIS1(f)) return 0;
  if (!p_IsConstant(NUM(f), ntRing)) return 0;
  return n_Int(p_GetCoeff(NUM(f), ntRing), ntCoeffs);
}

/* This method will only consider the numerators of a and b, without
   cancelling gcd's before.
   Moreover it may return TRUE only if one or both numerators
   are zero or if their degrees are equal. Then TRUE is returned iff
   coeff(numerator(a)) > coeff(numerator(b));
   In all other cases, FALSE will be returned. */
BOOLEAN ntGreater(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  number aNumCoeff = NULL; int aNumDeg = 0;
  number bNumCoeff = NULL; int bNumDeg = 0;
  if (!IS0(a))
  {
    fraction fa = (fraction)a;
    aNumDeg = p_Totaldegree(NUM(fa), ntRing);
    aNumCoeff = p_GetCoeff(NUM(fa), ntRing);
  }
  if (!IS0(b))
  {
    fraction fb = (fraction)b;
    bNumDeg = p_Totaldegree(NUM(fb), ntRing);
    bNumCoeff = p_GetCoeff(NUM(fb), ntRing);
  }
  if (aNumDeg != bNumDeg) return FALSE;
  else return n_Greater(aNumCoeff, bNumCoeff, ntCoeffs);
}

/* this method will only consider the numerator of a, without cancelling
   the gcd before;
   returns TRUE iff the leading coefficient of the numerator of a is > 0
                    or the leading term of the numerator of a is not a
                    constant */
BOOLEAN ntGreaterZero(number a, const coeffs cf)
{
  ntTest(a);
  if (IS0(a)) return FALSE;
  fraction f = (fraction)a;
  poly g = NUM(f);
  return (n_GreaterZero(p_GetCoeff(g, ntRing), ntCoeffs) ||
          (!p_LmIsConstant(g, ntRing)));
}

void ntCoeffWrite(const coeffs cf, BOOLEAN details)
{
  assume( cf != NULL );

  const ring A = cf->extRing;

  assume( A != NULL );
  assume( A->cf != NULL );

  n_CoeffWrite(A->cf, details);

//  rWrite(A);

  const int P = rVar(A);
  assume( P > 0 );

  Print("//   %d parameter    : ", P);

  for (int nop=0; nop < P; nop ++)
    Print("%s ", rRingVar(nop, A));

  assume( A->minideal == NULL );

  PrintS("\n//   minpoly        : 0\n");

/*
  PrintS("//   Coefficients live in the rational function field\n");
  Print("//   K(");
  for (int i = 0; i < rVar(ntRing); i++)
  {
    if (i > 0) PrintS(" ");
    Print("%s", rRingVar(i, ntRing));
  }
  PrintS(") with\n");
  PrintS("//   K: "); n_CoeffWrite(cf->extRing->cf);
*/
}

number ntAdd(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  if (IS0(a)) return ntCopy(b, cf);
  if (IS0(b)) return ntCopy(a, cf);

  fraction fa = (fraction)a;
  fraction fb = (fraction)b;

  poly g = p_Copy(NUM(fa), ntRing);
  if (!DENIS1(fb)) g = p_Mult_q(g, p_Copy(DEN(fb), ntRing), ntRing);
  poly h = p_Copy(NUM(fb), ntRing);
  if (!DENIS1(fa)) h = p_Mult_q(h, p_Copy(DEN(fa), ntRing), ntRing);
  g = p_Add_q(g, h, ntRing);

  if (g == NULL) return NULL;

  poly f;
  if      (DENIS1(fa) && DENIS1(fb))  f = NULL;
  else if (!DENIS1(fa) && DENIS1(fb)) f = p_Copy(DEN(fa), ntRing);
  else if (DENIS1(fa) && !DENIS1(fb)) f = p_Copy(DEN(fb), ntRing);
  else /* both denom's are != 1 */    f = p_Mult_q(p_Copy(DEN(fa), ntRing),
                                                   p_Copy(DEN(fb), ntRing),
                                                   ntRing);

  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  DEN(result) = f;
  COM(result) = COM(fa) + COM(fb) + ADD_COMPLEXITY;
  heuristicGcdCancellation((number)result, cf);
  return (number)result;
}

number ntSub(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  if (IS0(a)) return ntNeg(ntCopy(b, cf), cf);
  if (IS0(b)) return ntCopy(a, cf);

  fraction fa = (fraction)a;
  fraction fb = (fraction)b;

  poly g = p_Copy(NUM(fa), ntRing);
  if (!DENIS1(fb)) g = p_Mult_q(g, p_Copy(DEN(fb), ntRing), ntRing);
  poly h = p_Copy(NUM(fb), ntRing);
  if (!DENIS1(fa)) h = p_Mult_q(h, p_Copy(DEN(fa), ntRing), ntRing);
  g = p_Add_q(g, p_Neg(h, ntRing), ntRing);

  if (g == NULL) return NULL;

  poly f;
  if      (DENIS1(fa) && DENIS1(fb))  f = NULL;
  else if (!DENIS1(fa) && DENIS1(fb)) f = p_Copy(DEN(fa), ntRing);
  else if (DENIS1(fa) && !DENIS1(fb)) f = p_Copy(DEN(fb), ntRing);
  else /* both den's are != 1 */      f = p_Mult_q(p_Copy(DEN(fa), ntRing),
                                                   p_Copy(DEN(fb), ntRing),
                                                   ntRing);

  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  DEN(result) = f;
  COM(result) = COM(fa) + COM(fb) + ADD_COMPLEXITY;
  heuristicGcdCancellation((number)result, cf);
  return (number)result;
}

number ntMult(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  if (IS0(a) || IS0(b)) return NULL;

  fraction fa = (fraction)a;
  fraction fb = (fraction)b;

  poly g = p_Copy(NUM(fa), ntRing);
  poly h = p_Copy(NUM(fb), ntRing);
  g = p_Mult_q(g, h, ntRing);

  if (g == NULL) return NULL;   /* may happen due to zero divisors */

  poly f;
  if      (DENIS1(fa) && DENIS1(fb))  f = NULL;
  else if (!DENIS1(fa) && DENIS1(fb)) f = p_Copy(DEN(fa), ntRing);
  else if (DENIS1(fa) && !DENIS1(fb)) f = p_Copy(DEN(fb), ntRing);
  else /* both den's are != 1 */      f = p_Mult_q(p_Copy(DEN(fa), ntRing),
                                                   p_Copy(DEN(fb), ntRing),
                                                   ntRing);

  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  DEN(result) = f;
  COM(result) = COM(fa) + COM(fb) + MULT_COMPLEXITY;
  heuristicGcdCancellation((number)result, cf);
  return (number)result;
}

number ntDiv(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  if (IS0(a)) return NULL;
  if (IS0(b)) WerrorS(nDivBy0);

  fraction fa = (fraction)a;
  fraction fb = (fraction)b;

  poly g = p_Copy(NUM(fa), ntRing);
  if (!DENIS1(fb)) g = p_Mult_q(g, p_Copy(DEN(fb), ntRing), ntRing);

  if (g == NULL) return NULL;   /* may happen due to zero divisors */

  poly f = p_Copy(NUM(fb), ntRing);
  if (!DENIS1(fa)) f = p_Mult_q(f, p_Copy(DEN(fa), ntRing), ntRing);

  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = g;
  if (!p_IsConstant(f,ntRing) || !n_IsOne(pGetCoeff(f),ntRing->cf))
    DEN(result) = f;
  COM(result) = COM(fa) + COM(fb) + MULT_COMPLEXITY;
  heuristicGcdCancellation((number)result, cf);
  return (number)result;
}

/* 0^0 = 0;
   for |exp| <= 7 compute power by a simple multiplication loop;
   for |exp| >= 8 compute power along binary presentation of |exp|, e.g.
      p^13 = p^1 * p^4 * p^8, where we utilise that
      p^(2^(k+1)) = p^(2^k) * p^(2^k);
   intermediate cancellation is controlled by the in-place method
   heuristicGcdCancellation; see there.
*/
void ntPower(number a, int exp, number *b, const coeffs cf)
{
  ntTest(a);

  /* special cases first */
  if (IS0(a))
  {
    if (exp >= 0) *b = NULL;
    else          WerrorS(nDivBy0);
  }
  else if (exp ==  0) { *b = ntInit(1, cf); return;}
  else if (exp ==  1) { *b = ntCopy(a, cf); return;}
  else if (exp == -1) { *b = ntInvers(a, cf); return;}

  int expAbs = exp; if (expAbs < 0) expAbs = -expAbs;

  /* now compute a^expAbs */
  number pow; number t;
  if (expAbs <= 7)
  {
    pow = ntCopy(a, cf);
    for (int i = 2; i <= expAbs; i++)
    {
      t = ntMult(pow, a, cf);
      ntDelete(&pow, cf);
      pow = t;
      heuristicGcdCancellation(pow, cf);
    }
  }
  else
  {
    pow = ntInit(1, cf);
    number factor = ntCopy(a, cf);
    while (expAbs != 0)
    {
      if (expAbs & 1)
      {
        t = ntMult(pow, factor, cf);
        ntDelete(&pow, cf);
        pow = t;
        heuristicGcdCancellation(pow, cf);
      }
      expAbs = expAbs / 2;
      if (expAbs != 0)
      {
        t = ntMult(factor, factor, cf);
        ntDelete(&factor, cf);
        factor = t;
        heuristicGcdCancellation(factor, cf);
      }
    }
    ntDelete(&factor, cf);
  }

  /* invert if original exponent was negative */
  if (exp < 0)
  {
    t = ntInvers(pow, cf);
    ntDelete(&pow, cf);
    pow = t;
  }
  *b = pow;
}

/* assumes that cf represents the rationals, i.e. Q, and will only
   be called in that case;
   assumes furthermore that f != NULL and that the denominator of f != 1;
   generally speaking, this method removes denominators in the rational
   coefficients of the numerator and denominator of 'a';
   more concretely, the following normalizations will be performed,
   where t^alpha denotes a monomial in the transcendental variables t_k
   (1) if 'a' is of the form
          (sum_alpha a_alpha/b_alpha * t^alpha)
          -------------------------------------
            (sum_beta c_beta/d_beta * t^beta)
       with integers a_alpha, b_alpha, c_beta, d_beta, then both the
       numerator and the denominator will be multiplied by the LCM of
       the b_alpha's and the d_beta's (if this LCM is != 1),
   (2) if 'a' is - e.g. after having performed step (1) - of the form
          (sum_alpha a_alpha * t^alpha)
          -----------------------------
            (sum_beta c_beta * t^beta)
       with integers a_alpha, c_beta, and with a non-constant denominator,
       then both the numerator and the denominator will be divided by the
       GCD of the a_alpha's and the c_beta's (if this GCD is != 1),
   (3) if 'a' is - e.g. after having performed steps (1) and (2) - of the
       form
          (sum_alpha a_alpha * t^alpha)
          -----------------------------
                        c
       with integers a_alpha, and c != 1, then 'a' will be replaced by
       (sum_alpha a_alpha/c * t^alpha);
   this procedure does not alter COM(f) (this has to be done by the
   calling procedure);
   modifies f */
void handleNestedFractionsOverQ(fraction f, const coeffs cf)
{
  assume(nCoeff_is_Q(ntCoeffs));
  assume(!IS0(f));
  assume(!DENIS1(f));

  if (!p_IsConstant(DEN(f), ntRing))
  { /* step (1); see documentation of this procedure above */
    p_Normalize(NUM(f), ntRing);
    p_Normalize(DEN(f), ntRing);
    number lcmOfDenominators = n_Init(1, ntCoeffs);
    number c; number tmp;
    poly p = NUM(f);
    /* careful when using n_Lcm!!! It computes the lcm of the numerator
       of the 1st argument and the denominator of the 2nd!!! */
    while (p != NULL)
    {
      c = p_GetCoeff(p, ntRing);
      tmp = n_Lcm(lcmOfDenominators, c, ntCoeffs);
      n_Delete(&lcmOfDenominators, ntCoeffs);
      lcmOfDenominators = tmp;
      pIter(p);
    }
    p = DEN(f);
    while (p != NULL)
    {
      c = p_GetCoeff(p, ntRing);
      tmp = n_Lcm(lcmOfDenominators, c, ntCoeffs);
      n_Delete(&lcmOfDenominators, ntCoeffs);
      lcmOfDenominators = tmp;
      pIter(p);
    }
    if (!n_IsOne(lcmOfDenominators, ntCoeffs))
    { /* multiply NUM(f) and DEN(f) with lcmOfDenominators */
      NUM(f) = p_Mult_nn(NUM(f), lcmOfDenominators, ntRing);
      p_Normalize(NUM(f), ntRing);
      DEN(f) = p_Mult_nn(DEN(f), lcmOfDenominators, ntRing);
      p_Normalize(DEN(f), ntRing);
    }
    n_Delete(&lcmOfDenominators, ntCoeffs);
    if (!p_IsConstant(DEN(f), ntRing))
    { /* step (2); see documentation of this procedure above */
      p = NUM(f);
      number gcdOfCoefficients = n_Copy(p_GetCoeff(p, ntRing), ntCoeffs);
      pIter(p);
      while ((p != NULL) && (!n_IsOne(gcdOfCoefficients, ntCoeffs)))
      {
        c = p_GetCoeff(p, ntRing);
        tmp = n_Gcd(c, gcdOfCoefficients, ntCoeffs);
        n_Delete(&gcdOfCoefficients, ntCoeffs);
        gcdOfCoefficients = tmp;
        pIter(p);
      }
      p = DEN(f);
      while ((p != NULL) && (!n_IsOne(gcdOfCoefficients, ntCoeffs)))
      {
        c = p_GetCoeff(p, ntRing);
        tmp = n_Gcd(c, gcdOfCoefficients, ntCoeffs);
        n_Delete(&gcdOfCoefficients, ntCoeffs);
        gcdOfCoefficients = tmp;
        pIter(p);
      }
      if (!n_IsOne(gcdOfCoefficients, ntCoeffs))
      { /* divide NUM(f) and DEN(f) by gcdOfCoefficients */
        number inverseOfGcdOfCoefficients = n_Invers(gcdOfCoefficients,
                                                     ntCoeffs);
        NUM(f) = p_Mult_nn(NUM(f), inverseOfGcdOfCoefficients, ntRing);
        p_Normalize(NUM(f), ntRing);
        DEN(f) = p_Mult_nn(DEN(f), inverseOfGcdOfCoefficients, ntRing);
        p_Normalize(DEN(f), ntRing);
        n_Delete(&inverseOfGcdOfCoefficients, ntCoeffs);
      }
      n_Delete(&gcdOfCoefficients, ntCoeffs);
    }
  }
  if (p_IsConstant(DEN(f), ntRing) &&
      (!n_IsOne(p_GetCoeff(DEN(f), ntRing), ntCoeffs)))
  { /* step (3); see documentation of this procedure above */
    number inverseOfDen = n_Invers(p_GetCoeff(DEN(f), ntRing), ntCoeffs);
    NUM(f) = p_Mult_nn(NUM(f), inverseOfDen, ntRing);
    n_Delete(&inverseOfDen, ntCoeffs);
    p_Delete(&DEN(f), ntRing);
    DEN(f) = NULL;
  }

  /* Now, due to the above computations, DEN(f) may have become the
     1-polynomial which needs to be represented by NULL: */
  if ((DEN(f) != NULL) &&
      p_IsConstant(DEN(f), ntRing) &&
      n_IsOne(p_GetCoeff(DEN(f), ntRing), ntCoeffs))
  {
    p_Delete(&DEN(f), ntRing); DEN(f) = NULL;
  }
}

/* modifies a */
void heuristicGcdCancellation(number a, const coeffs cf)
{
  ntTest(a);
  if (IS0(a)) return;

  fraction f = (fraction)a;
  if (DENIS1(f) || NUMIS1(f)) { COM(f) = 0; return; }

  /* check whether NUM(f) = DEN(f), and - if so - replace 'a' by 1 */
  if (p_EqualPolys(NUM(f), DEN(f), ntRing))
  { /* numerator and denominator are both != 1 */
    p_Delete(&NUM(f), ntRing); NUM(f) = p_ISet(1, ntRing);
    p_Delete(&DEN(f), ntRing); DEN(f) = NULL;
    COM(f) = 0;
    return;
  }

  if (COM(f) <= BOUND_COMPLEXITY) return;
  else definiteGcdCancellation(a, cf, TRUE);
}

/* modifies a */
void definiteGcdCancellation(number a, const coeffs cf,
                             BOOLEAN simpleTestsHaveAlreadyBeenPerformed)
{
  ntTest(a);

  fraction f = (fraction)a;

  if (!simpleTestsHaveAlreadyBeenPerformed)
  {
    if (IS0(a)) return;
    if (DENIS1(f) || NUMIS1(f)) { COM(f) = 0; return; }

    /* check whether NUM(f) = DEN(f), and - if so - replace 'a' by 1 */
    if (p_EqualPolys(NUM(f), DEN(f), ntRing))
    { /* numerator and denominator are both != 1 */
      p_Delete(&NUM(f), ntRing); NUM(f) = p_ISet(1, ntRing);
      p_Delete(&DEN(f), ntRing); DEN(f) = NULL;
      COM(f) = 0;
      return;
    }
  }

#ifdef HAVE_FACTORY
  /* singclap_gcd destroys its arguments; we hence need copies: */
  poly pNum = p_Copy(NUM(f), ntRing);
  poly pDen = p_Copy(DEN(f), ntRing);

  /* Note that, over Q, singclap_gcd will remove the denominators in all
     rational coefficients of pNum and pDen, before starting to compute
     the gcd. Thus, we do not need to ensure that the coefficients of
     pNum and pDen live in Z; they may well be elements of Q\Z. */
  poly pGcd = singclap_gcd(pNum, pDen, cf->extRing);
  if (p_IsConstant(pGcd, ntRing) &&
      n_IsOne(p_GetCoeff(pGcd, ntRing), ntCoeffs))
  { /* gcd = 1; nothing to cancel;
       Suppose the given rational function field is over Q. Although the
       gcd is 1, we may have produced fractional coefficients in NUM(f),
       DEN(f), or both, due to previous arithmetics. The next call will
       remove those nested fractions, in case there are any. */
    if (nCoeff_is_Q(ntCoeffs)) handleNestedFractionsOverQ(f, cf);
  }
  else
  { /* We divide both NUM(f) and DEN(f) by the gcd which is known
       to be != 1. */
    poly newNum = singclap_pdivide(NUM(f), pGcd, ntRing);
    p_Delete(&NUM(f), ntRing);
    NUM(f) = newNum;
    poly newDen = singclap_pdivide(DEN(f), pGcd, ntRing);
    p_Delete(&DEN(f), ntRing);
    DEN(f) = newDen;
    if (p_IsConstant(DEN(f), ntRing) &&
        n_IsOne(p_GetCoeff(DEN(f), ntRing), ntCoeffs))
    {
      /* DEN(f) = 1 needs to be represented by NULL! */
      p_Delete(&DEN(f), ntRing);
      newDen = NULL;
    }
    else
    { /* Note that over Q, by cancelling the gcd, we may have produced
         fractional coefficients in NUM(f), DEN(f), or both. The next
         call will remove those nested fractions, in case there are
         any. */
      if (nCoeff_is_Q(ntCoeffs)) handleNestedFractionsOverQ(f, cf);
    }
  }
  COM(f) = 0;
  p_Delete(&pGcd, ntRing);
#endif /* HAVE_FACTORY */
}

/* modifies a */
void ntWrite(number &a, const coeffs cf)
{
  ntTest(a);
  definiteGcdCancellation(a, cf, FALSE);
  if (IS0(a))
    StringAppendS("0");
  else
  {
    fraction f = (fraction)a;
    // stole logic from napWrite from kernel/longtrans.cc of legacy singular
    BOOLEAN omitBrackets = p_IsConstant(NUM(f), ntRing);
    if (!omitBrackets) StringAppendS("(");
    p_String0(NUM(f), ntRing, ntRing);
    if (!omitBrackets) StringAppendS(")");
    if (!DENIS1(f))
    {
      StringAppendS("/");
      omitBrackets = p_IsConstant(DEN(f), ntRing);
      if (!omitBrackets) StringAppendS("(");
      p_String0(DEN(f), ntRing, ntRing);
      if (!omitBrackets) StringAppendS(")");
    }
  }
}

const char * ntRead(const char *s, number *a, const coeffs cf)
{
  poly p;
  const char * result = p_Read(s, p, ntRing);
  if (p == NULL) { *a = NULL; return result; }
  else
  {
    fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
    NUM(f) = p;
    DEN(f) = NULL;
    COM(f) = 0;
    *a = (number)f;
    return result;
  }
}

/* expects *param to be castable to TransExtInfo */
static BOOLEAN ntCoeffIsEqual(const coeffs cf, n_coeffType n, void * param)
{
  if (ID != n) return FALSE;
  TransExtInfo *e = (TransExtInfo *)param;
  /* for rational function fields we expect the underlying
     polynomial rings to be IDENTICAL, i.e. the SAME OBJECT;
     this expectation is based on the assumption that we have properly
     registered cf and perform reference counting rather than creating
     multiple copies of the same coefficient field/domain/ring */
  return (ntRing == e->r);
}

number ntLcm(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  fraction fb = (fraction)b;
  if ((b==NULL)||(DEN(fb)==NULL)) return ntCopy(a,cf);
#ifdef HAVE_FACTORY
  fraction fa = (fraction)a;
  /* singclap_gcd destroys its arguments; we hence need copies: */
  poly pa = p_Copy(NUM(fa), ntRing);
  poly pb = p_Copy(DEN(fb), ntRing);

  /* Note that, over Q, singclap_gcd will remove the denominators in all
     rational coefficients of pa and pb, before starting to compute
     the gcd. Thus, we do not need to ensure that the coefficients of
     pa and pb live in Z; they may well be elements of Q\Z. */
  poly pGcd = singclap_gcd(pa, pb, cf->extRing);
  if (p_IsConstant(pGcd, ntRing) &&
      n_IsOne(p_GetCoeff(pGcd, ntRing), ntCoeffs))
  { /* gcd = 1; return pa*pb*/
    p_Delete(&pGcd,ntRing);
    fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
    NUM(result) = pp_Mult_qq(NUM(fa),DEN(fb),ntRing);
    return (number)result;
  }
  else
  { /* return pa*pb/gcd */
    poly newNum = singclap_pdivide(NUM(fa), pGcd, ntRing);
    p_Delete(&pGcd,ntRing);
    fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
    NUM(result) = p_Mult_q(p_Copy(DEN(fb),ntRing),newNum,ntRing);
    return (number)result;
  }
#else
  Print("// factory needed: transext.cc:ntLcm\n");
  return NULL;
#endif /* HAVE_FACTORY */
  return NULL;
}

number ntGcd(number a, number b, const coeffs cf)
{
  ntTest(a); ntTest(b);
  if (a==NULL) return ntCopy(b,cf);
  if (b==NULL) return ntCopy(a,cf);
#ifdef HAVE_FACTORY
  fraction fa = (fraction)a;
  fraction fb = (fraction)b;
  /* singclap_gcd destroys its arguments; we hence need copies: */
  poly pa = p_Copy(NUM(fa), ntRing);
  poly pb = p_Copy(NUM(fb), ntRing);

  /* Note that, over Q, singclap_gcd will remove the denominators in all
     rational coefficients of pa and pb, before starting to compute
     the gcd. Thus, we do not need to ensure that the coefficients of
     pa and pb live in Z; they may well be elements of Q\Z. */
  poly pGcd = singclap_gcd(pa, pb, cf->extRing);
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = pGcd;
  return (number)result;
#else
  Print("// factory needed: transext.cc:ntGcd\n");
  return NULL;
#endif /* HAVE_FACTORY */
}

int ntSize(number a, const coeffs cf)
{
  ntTest(a);
  if (IS0(a)) return -1;
  /* this has been taken from the old implementation of field extensions,
     where we computed the sum of the degrees and the numbers of terms in
     the numerator and denominator of a; so we leave it at that, for the
     time being */
  fraction f = (fraction)a;
  poly p = NUM(f);
  int noOfTerms = 0;
  int numDegree = 0;
  while (p != NULL)
  {
    noOfTerms++;
    int d = 0;
    for (int i = 1; i <= rVar(ntRing); i++)
      d += p_GetExp(p, i, ntRing);
    if (d > numDegree) numDegree = d;
    pIter(p);
  }
  int denDegree = 0;
  if (!DENIS1(f))
  {
    p = DEN(f);
    while (p != NULL)
    {
      noOfTerms++;
      int d = 0;
      for (int i = 1; i <= rVar(ntRing); i++)
        d += p_GetExp(p, i, ntRing);
      if (d > denDegree) denDegree = d;
      pIter(p);
    }
  }
  return numDegree + denDegree + noOfTerms;
}

number ntInvers(number a, const coeffs cf)
{
  ntTest(a);
  if (IS0(a)) WerrorS(nDivBy0);
  fraction f = (fraction)a;
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  poly g;
  if (DENIS1(f)) g = p_One(ntRing);
  else           g = p_Copy(DEN(f), ntRing);
  NUM(result) = g;
  DEN(result) = p_Copy(NUM(f), ntRing);
  COM(result) = COM(f);
  return (number)result;
}

/* assumes that src = Q, dst = Q(t_1, ..., t_s) */
number ntMap00(number a, const coeffs src, const coeffs dst)
{
  if (n_IsZero(a, src)) return NULL;
  assume(src == dst->extRing->cf);
  poly p = p_One(dst->extRing);
  p_SetCoeff(p, n_Copy(a, src), dst->extRing);
  fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(f) = p; DEN(f) = NULL; COM(f) = 0;
  return (number)f;
}

/* assumes that src = Z/p, dst = Q(t_1, ..., t_s) */
number ntMapP0(number a, const coeffs src, const coeffs dst)
{
  if (n_IsZero(a, src)) return NULL;
  /* mapping via intermediate int: */
  int n = n_Int(a, src);
  number q = n_Init(n, dst->extRing->cf);
  poly p;
  if (n_IsZero(q, dst->extRing->cf))
  {
    n_Delete(&q, dst->extRing->cf);
    return NULL;
  }
  p = p_One(dst->extRing);
  p_SetCoeff(p, q, dst->extRing);
  fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(f) = p; DEN(f) = NULL; COM(f) = 0;
  return (number)f;
}

/* assumes that either src = Q(t_1, ..., t_s), dst = Q(t_1, ..., t_s), or
                       src = Z/p(t_1, ..., t_s), dst = Z/p(t_1, ..., t_s) */
number ntCopyMap(number a, const coeffs cf, const coeffs dst)
{
//  if (n_IsZero(a, cf)) return NULL;
   
  ntTest(a);

  if (IS0(a)) return NULL;
   
  const ring rSrc = cf->extRing;
  const ring rDst = dst->extRing;
  
  if( rSrc == rDst )
    return ntCopy(a, dst); // USUALLY WRONG!
  
  fraction f = (fraction)a;
  poly g = prCopyR(NUM(f), rSrc, rDst);
   
  poly h = NULL;
  
  if (!DENIS1(f))
     h = prCopyR(DEN(f), rSrc, rDst);
   
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
   
  NUM(result) = g;
  DEN(result) = h;
  COM(result) = COM(f);
  return (number)result;  
}

number ntCopyAlg(number a, const coeffs cf, const coeffs dst)
{
  if (n_IsZero(a, cf)) return NULL;
   
  fraction f = (fraction)omAlloc0Bin(fractionObjectBin); 
  // DEN(f) = NULL; COM(f) = 0;
  NUM(f) = prCopyR((poly)a, cf->extRing, dst->extRing);
  return (number)f;
}

/* assumes that src = Q, dst = Z/p(t_1, ..., t_s) */
number ntMap0P(number a, const coeffs src, const coeffs dst)
{
  if (n_IsZero(a, src)) return NULL;
  int p = rChar(dst->extRing);
  int n = nlModP(a, p, src);
  number q = n_Init(n, dst->extRing->cf);
  poly g;
  if (n_IsZero(q, dst->extRing->cf))
  {
    n_Delete(&q, dst->extRing->cf);
    return NULL;
  }
  g = p_One(dst->extRing);
  p_SetCoeff(g, q, dst->extRing);
  fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(f) = g; // DEN(f) = NULL; COM(f) = 0;
  return (number)f;
}

/* assumes that src = Z/p, dst = Z/p(t_1, ..., t_s) */
number ntMapPP(number a, const coeffs src, const coeffs dst)
{
  if (n_IsZero(a, src)) return NULL;
  assume(src == dst->extRing->cf);
  poly p = p_One(dst->extRing);
  p_SetCoeff(p, n_Copy(a, src), dst->extRing);
  fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(f) = p; DEN(f) = NULL; COM(f) = 0;
  return (number)f;
}

/* assumes that src = Z/u, dst = Z/p(t_1, ..., t_s), where u != p */
number ntMapUP(number a, const coeffs src, const coeffs dst)
{
  if (n_IsZero(a, src)) return NULL;
  /* mapping via intermediate int: */
  int n = n_Int(a, src);
  number q = n_Init(n, dst->extRing->cf);
  poly p;
  if (n_IsZero(q, dst->extRing->cf))
  {
    n_Delete(&q, dst->extRing->cf);
    return NULL;
  }
  p = p_One(dst->extRing);
  p_SetCoeff(p, q, dst->extRing);
  fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(f) = p; DEN(f) = NULL; COM(f) = 0;
  return (number)f;
}

nMapFunc ntSetMap(const coeffs src, const coeffs dst)
{
  /* dst is expected to be a rational function field */
  assume(getCoeffType(dst) == ID);

  int h = 0; /* the height of the extension tower given by dst */
  coeffs bDst = nCoeff_bottom(dst, h); /* the bottom field in the tower dst */
  coeffs bSrc = nCoeff_bottom(src, h); /* the bottom field in the tower src */

  /* for the time being, we only provide maps if h = 1 and if b is Q or
     some field Z/pZ: */
  if (h==0)
  {
    if (nCoeff_is_Q(src) && nCoeff_is_Q(bDst))
      return ntMap00;                                 /// Q       -->  Q(T)
    if (nCoeff_is_Zp(src) && nCoeff_is_Q(bDst))
      return ntMapP0;                                 /// Z/p     -->  Q(T)
    if (nCoeff_is_Q(src) && nCoeff_is_Zp(bDst))
      return ntMap0P;                                 /// Q       --> Z/p(T)
    if (nCoeff_is_Zp(src) && nCoeff_is_Zp(bDst))
    {
      if (src->ch == dst->ch) return ntMapPP;         /// Z/p     --> Z/p(T)
      else return ntMapUP;                            /// Z/u     --> Z/p(T)
    }
  }
  if (h != 1) return NULL;
  if ((!nCoeff_is_Zp(bDst)) && (!nCoeff_is_Q(bDst))) return NULL;

  /* Let T denote the sequence of transcendental extension variables, i.e.,
     K[t_1, ..., t_s] =: K[T];
     Let moreover, for any such sequence T, T' denote any subsequence of T
     of the form t_1, ..., t_w with w <= s. */

  if ((!nCoeff_is_Zp(bSrc)) && (!nCoeff_is_Q(bSrc))) return NULL;

  if (nCoeff_is_Q(bSrc) && nCoeff_is_Q(bDst))
  {
    if (rVar(src->extRing) > rVar(dst->extRing)) 
       return NULL;
     
    for (int i = 0; i < rVar(src->extRing); i++)
      if (strcmp(rRingVar(i, src->extRing), rRingVar(i, dst->extRing)) != 0) 
	 return NULL;
     
    if (src->type==n_transExt)
       return ntCopyMap;          /// Q(T')   --> Q(T)
    else
       return ntCopyAlg;
  }

  if (nCoeff_is_Zp(bSrc) && nCoeff_is_Zp(bDst))
  {
    if (rVar(src->extRing) > rVar(dst->extRing)) 
       return NULL;
     
    for (int i = 0; i < rVar(src->extRing); i++)
      if (strcmp(rRingVar(i, src->extRing), rRingVar(i, dst->extRing)) != 0) 
	 return NULL;
     
    if (src->type==n_transExt)
       return ntCopyMap;         /// Z/p(T') --> Z/p(T)
    else
       return ntCopyAlg;
  }

  return NULL;                                 /// default
}

void ntKillChar(coeffs cf)
{
  if ((--cf->extRing->ref) == 0)
    rDelete(cf->extRing);
}
#ifdef HAVE_FACTORY
number ntConvFactoryNSingN( const CanonicalForm n, const coeffs cf)
{
  if (n.isZero()) return NULL;
  poly p=convFactoryPSingP(n,ntRing);
  fraction result = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(result) = p;
  //DEN(result) = NULL; // done by omAlloc0Bin
  //COM(result) = 0; // done by omAlloc0Bin
  return (number)result;
}
CanonicalForm ntConvSingNFactoryN( number n, BOOLEAN setChar, const coeffs cf )
{
  ntTest(n);
  if (IS0(n)) return CanonicalForm(0);

  fraction f = (fraction)n;
  return convSingPFactoryP(NUM(f),ntRing);
}
#endif

BOOLEAN ntInitChar(coeffs cf, void * infoStruct)
{

  assume( infoStruct != NULL );

  TransExtInfo *e = (TransExtInfo *)infoStruct;

  assume( e->r                != NULL);      // extRing;
  assume( e->r->cf            != NULL);      // extRing->cf;
  assume( e->r->minideal == NULL );

  assume( cf != NULL );
  assume(getCoeffType(cf) == ID);                // coeff type;

  cf->extRing           = e->r;
  cf->extRing->ref ++; // increase the ref.counter for the ground poly. ring!
  cf->factoryVarOffset = cf->extRing->cf->factoryVarOffset+rVar(cf->extRing);

  /* propagate characteristic up so that it becomes
     directly accessible in cf: */
  cf->ch = cf->extRing->cf->ch;

  cf->cfGreaterZero  = ntGreaterZero;
  cf->cfGreater      = ntGreater;
  cf->cfEqual        = ntEqual;
  cf->cfIsZero       = ntIsZero;
  cf->cfIsOne        = ntIsOne;
  cf->cfIsMOne       = ntIsMOne;
  cf->cfInit         = ntInit;
  cf->cfInit_bigint  = ntInit_bigint;
  cf->cfInt          = ntInt;
  cf->cfNeg          = ntNeg;
  cf->cfAdd          = ntAdd;
  cf->cfSub          = ntSub;
  cf->cfMult         = ntMult;
  cf->cfDiv          = ntDiv;
  cf->cfExactDiv     = ntDiv;
  cf->cfPower        = ntPower;
  cf->cfCopy         = ntCopy;
  cf->cfWrite        = ntWrite;
  cf->cfRead         = ntRead;
  cf->cfDelete       = ntDelete;
  cf->cfSetMap       = ntSetMap;
  cf->cfGetDenom     = ntGetDenom;
  cf->cfGetNumerator = ntGetNumerator;
  cf->cfRePart       = ntCopy;
  cf->cfImPart       = ntImPart;
  cf->cfCoeffWrite   = ntCoeffWrite;
#ifdef LDEBUG
  cf->cfDBTest       = ntDBTest;
#endif
  cf->cfGcd          = ntGcd;
  cf->cfLcm          = ntLcm;
  cf->cfSize         = ntSize;
  cf->nCoeffIsEqual  = ntCoeffIsEqual;
  cf->cfInvers       = ntInvers;
  cf->cfIntDiv       = ntDiv;
  cf->cfKillChar     = ntKillChar;

#ifndef HAVE_FACTORY
  PrintS("// Warning: The 'factory' module is not available.\n");
  PrintS("//          Hence gcd's cannot be cancelled in any\n");
  PrintS("//          computed fraction!\n");
#else
  cf->convFactoryNSingN =ntConvFactoryNSingN;
  cf->convSingNFactoryN =ntConvSingNFactoryN;
#endif

  return FALSE;
}


number ntParam(const short iParameter, const coeffs cf)
{
  assume(getCoeffType(cf) == ID);

  const ring R = cf->extRing;
  assume( R != NULL );
  assume( 0 < iParameter && iParameter <= rVar(R) );

  poly p = p_One(R); p_SetExp(p, iParameter, 1, R); p_Setm(p, R);

//  return (number) p;

  fraction f = (fraction)omAlloc0Bin(fractionObjectBin);
  NUM(f) = p;
  DEN(f) = NULL;
  COM(f) = 0;

  return (number)f;
}


/// if m == var(i)/1 => return i,
int ntIsParam(number m, const coeffs cf)
{
  assume(getCoeffType(cf) == ID);

  const ring R = cf->extRing;
  assume( R != NULL );

  fraction f = (fraction)m;

  if( DEN(f) != NULL )
    return 0;

  return p_Var( NUM(f), R );
}
