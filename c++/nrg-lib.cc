/*
 "NRG Ljubljana" - Numerical renormalization group for multiple
 impurities and an arbitrary number of channels

 Copyright (C) 2005-2020 Rok Zitko

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

   Contact information:
   Rok Zitko
   F1 - Theoretical physics
   "Jozef Stefan" Institute
   Jamova 39
   SI-1000 Ljubljana
   Slovenia

   rok.zitko@ijs.si
*/

#include "nrg-general.h"
#include "nrg-lib.h" // exposed interfaces for wrapping into a library
#include "portabil.h"
#include "time_mem.h"
#include "debug.h"
#include "misc.h"
#include "openmp.h"
#include "mp.h"

#include "params.cc"
#include "outfield.cc"

// This is included in the library only. Should not be used if cblas library is available.
#ifdef CBLAS_WORKAROUND
 #define ADD_
 #include "cblas_globals.c"
 #include "cblas_dgemm.c"
 #ifdef NRG_COMPLEX
  #include "cblas_zgemm.c"
 #endif
 #include "cblas_xerbla.c"
#endif

inline const size_t MAX_NDX = 1000; // max index number

// Timing of various parts of the code and memory statistics
namespace time_mem {
 Timing tm;
 MemoryStats ms;
 void timing_report() { tm.report(); }
 void memory_report() { ms.report(); }
 void memory_time_brief_report() {
#ifdef HAS_MEMORY_USAGE
   cout << "Memory used: " << long(ms.used() / 1024) << " MB "; // NOLINT
#endif
   cout << "Time elapsed: " << prec3(tm.total_in_seconds()) << " s" << endl;
 }
}

#ifdef NRG_MPI
mpi::environment *mpienv;
mpi::communicator *mpiw;
int myrank() { return mpiw->rank(); } // used in diag.h, time_mem.h
#else
int myrank() { return 0; }
#endif

// Quantum number types defined to enforce type checking
using Number = int;
using Ispin = int;
using Sspin = int;
using Tangmom = int;
using SZspin = int;

// Invariant subspace abstraction (container with quantum numbers)
#include "invar.cc"

// *** Commonly used types ***

#ifdef NRG_REAL
using t_matel = double;                       // type for the matrix elements
using t_eigen = double;                       // type for the eigenvalues
using t_coef = double;                        // type for the Wilson chain coefficients
using t_factor = double;                      // type for various prefactors in recalculations
using t_expv = double;                        // type for expectation values of operators
inline double CONJ_ME(double x) { return x; } // Conjugation of matrix elements: no op
#endif

#ifdef NRG_COMPLEX
using t_matel = cmpl;
using t_eigen = double;
using t_coef = cmpl;
using t_factor = cmpl;
using t_expv = cmpl; // we allow the calculation of expectation values of
                     // non-Hermitian operators!
inline cmpl CONJ_ME(cmpl z) { return conj(z); }
#endif

using t_weight = cmpl; // spectral weight accumulators (complex in general)

/* NOTE: Row major is the C array format: A[0][0], A[0][1], A[0][2],
A[1][0], A[1][1], etc. The default in UBLAS is row major, while LAPACK
routines expect column major matrices. Of course, this is of no concern for
symmetric matrices. Default storage type is unbounded_array<T>.

Thus, as always:
  first index - row
  second index - column

 when accessing columns - stride=m.size2()
 when accessing rows - stide=1

 Optimization rule: use stride 1 sequential access where possible.
 ublas default matrix storage is row major (i.e. C-like). The rule is
 "right index the same as inner loop variable".
*/

using Matrix = ublas::matrix<t_matel>;

#include "numerics.h"

using MatrixElements = map<Twoinvar, Matrix>;
using DensMatElements = map<Invar, Matrix>;

using ThreeInvar = tuple<Invar, Invar, Invar>; // For 3-leg vertex functions

// Dump matrix elements: one matrix
void dump_matrix_elements(const Matrix &m, ostream &fout = cout,
                          const double chopsmall = 1e-8, const size_t maxdump = 10) {
  for (auto r1 = 0; r1 < std::min(m.size1(), maxdump); r1++) {
    for (auto r2 = 0; r2 < std::min(m.size2(), maxdump); r2++) 
      fout << chop(m(r1, r2), chopsmall) << ' ';
    fout << endl;
  }
}

// Dump matrix elements: all subspace pairs
void dump_matrix_elements(const MatrixElements &m, ostream &fout = cout,
                          const double chopsmall = 1e-8, const size_t maxdump = 10) {
  for (const auto &[II, mat] : m) {
    fout << "----" << II << "----" << endl;
    dump_matrix_elements(mat, fout, chopsmall, maxdump);
  }
}

template <typename T> 
  inline pair<T, T> reverse_pair(const pair<T, T> &i) { 
    return make_pair(i.second, i.first); 
  }

// Map of operators matrices
using CustomOp = map<string, MatrixElements>;

// Vector containing irreducible matrix elements of f operators.
using OpchChannel = std::vector<MatrixElements>;
// Each channel contains P.perchannel OpchChannel matrices.
using Opch = std::vector<OpchChannel>;

// Object of class IterInfo cotains full information about matrix representations 
// when entering stage N of the NRG iteration.
class IterInfo {
  public:
  Opch opch;     // f operators (channels)
  CustomOp ops;  // singlet operators (even parity)
  CustomOp opsp; // singlet operators (odd parity)
  CustomOp opsg; // singlet operators [global op]
  CustomOp opd;  // doublet operators (spectral functions)
  CustomOp opt;  // triplet operators (dynamical spin susceptibility)
  CustomOp opq;  // quadruplet operators (spectral functions for J=3/2)
  CustomOp opot; // orbital triplet operators
};

class Eigen;
class Stats;

using DiagInfo = map<Invar, Eigen>; // Full information after diagonalizations

// Dimensions of the invariant subspaces |r,1>, |r,2>, |r,3>, etc. The name "rmax" comes from the maximal value of
// the index "r" which ranges from 1 through rmax.

class Rmaxvals {
 private:
   ublas::vector<size_t> values;
 public:
   Rmaxvals() = default;
   Rmaxvals(const Invar &I, const InvarVec &In, const DiagInfo &diagprev);
   size_t rmax(size_t i) const { return values[i-1]; } // FOR COMPATIBILITY OFFSET 1!
   size_t offset(size_t i) const { return std::accumulate(begin(values), begin(values) + (i-1), 0); }
   size_t operator[](size_t i) const { return rmax(i); }
   size_t total() const { return std::accumulate(begin(values), end(values), 0); } // total number of states
 private:
   friend ostream &operator<<(ostream &os, const Rmaxvals &rmax) {
     for (const auto &x : rmax.values) os << x << ' ';
     return os;
   }
   template <class Archive> void serialize(Archive &ar, const unsigned int version) { ar &values; }
   friend class boost::serialization::access;
};

using QSrmax = map<Invar, Rmaxvals>;

// Information about the number of states, kept and discarded, rmax,
// and eigenenergies. Required for the density-matrix construction.
template<typename M> struct DimSubGen {
  using EVEC = ublas::vector<M>;
  size_t kept  = 0;
  size_t total = 0;
  Rmaxvals rmax;   // substructure of vectors omega
  EVEC eigenvalue; // all eigenvalues
  EVEC absenergy;  // absolute energies
  EVEC absenergyG; // absolute energies referred to the overall ground-state energy
  EVEC absenergyN; // absolute energies referred to the step-N ground-state energy
  bool is_last = false;
  size_t min() const { return (is_last ? 0 : kept); } // min(), max() return the range of D states to be summed over in FDM
  size_t max() const { return total; }
};

using DimSub = DimSubGen<t_eigen>;

// Full information about the number of states and matrix dimensions
// Example: dm[N].rmax[I] etc.
using Subs = map<Invar, DimSub>;
using AllSteps = std::vector<Subs>;

// NOTE: "absolute" energy means that it is expressed in the absolute energy scale rather than SCALE(N). It is a
// statement about scaling, not about possible linear shifts/offsets.

// Result of a diagonalisation: eigenvalues and eigenvectors
struct RawEigen {
  using EVEC = ublas::vector<t_eigen>;
  EVEC value_orig;     // eigenvalues as computed
  Matrix matrix; // eigenvectors
  RawEigen() {}
  RawEigen(size_t nr, size_t dim) {
    my_assert(nr <= dim);
    value_orig.resize(nr);
    matrix.resize(nr, dim);
  }
  size_t getnrc() const { return value_orig.size(); } // number of computed eigenpairs
  size_t getdim() const { return matrix.size2(); } // valid also after the split_in_blocks_Eigen() call
};
  
// Augments RawEigen with the information about truncation and block structure of the eigenvectors.
struct Eigen : public RawEigen {
  EVEC value_zero;     // Egs subtracted
  size_t getnr() const  { return value_zero.size(); }
  size_t nrpost = 0;   // number of eigenpairs after truncation
  double shift  = 0.0; // shift of eigenvalues (0 or Egs)
  EVEC absenergy;      // absolute energies
  EVEC absenergyG;     // absolute energies (0 is the absolute ground state of the system) [SAVED TO FILE]
  EVEC absenergyN;     // absolute energies (referenced to the lowest energy in the N-th step)
  EVEC boltzmann;      // Boltzmann factors
  // 'blocks' contains eigenvectors separated according to the invariant
  // subspace from which they originate. This separation is required for
  // using the efficient BLAS routines when performing recalculations of
  // the matrix elements.
  std::vector<Matrix> blocks;
  Eigen() : RawEigen() {}
  Eigen(size_t nr, size_t rmax) : RawEigen(nr, rmax) {}
  // Returns the number of eigenpairs after truncation.
  size_t getnrkept() const { return nrpost; }
  // Truncate to nrpost states.
  void truncate_prepare_subspace(size_t _nrpost) {
    nrpost = _nrpost;
    my_assert(nrpost <= getnr());
  }
  void truncate_perform() {
    for (auto &i : blocks) {
      my_assert(nrpost <= i.size1());
      i.resize(nrpost, i.size2());
    }
    value_zero.resize(nrpost);
  }
  // Initialize the data structures with eigenvalues 'v'. The eigenvectors form an identity matrix. This is used to
  // represent the spectral decomposition in the eigenbasis itself.
  void diagonal(const EVEC &v) {
    value_orig = value_zero = v;
    matrix   = ublas::identity_matrix<t_eigen>(v.size());
    shift    = 0.0;
  }
  void subtract_Egs(double Egs) {
    my_assert(shift == 0.0);
    value_zero = value_orig;
    for (auto &x : value_zero) x -= Egs;
    shift = Egs;
    my_assert(value_zero[0] >= 0.0);
  }
  void shift_absenergyG(double GS_energy) {
    for (auto &x : absenergyG) x -= GS_energy;
  }

private:
  friend class boost::serialization::access;
  template <class Archive> void serialize(Archive &ar, const unsigned int version) {
     ar &value_orig;
     ar &matrix;
     ar &value_zero;
     ar &nrpost;
     ar &shift;
     ar &absenergy;
     ar &absenergyG;
     ar &absenergyN;
     ar &boltzmann;
  }
};

template <typename T> ostream &operator<<(ostream &os, const ublas::vector<T> &v) {
  for (const auto &x : v) os << x << ' ';
  return os;
}

enum class RUNTYPE { NRG, DMNRG };

class Step {
 private:
   // N denotes the order of the Hamiltonian. N=0 corresponds to H_0, i.e. the initial Hamiltonian
   size_t trueN; // "true N", sets the energy scale, may be negative, trueN <= ndxN
   size_t ndxN; // "index N", iteration step, used as an array index, ndxN >= 0
   const Params &P; // reference to parameters (beta, T)
   
 public:
   RUNTYPE runtype; // NRG vs. DM-NRG run
   void set(int newN) {
     trueN = newN;
     ndxN = std::max(newN, 0);
   }
   void init() { set(P.Ninit); }
   Step(const Params &P_) : P(P_) { init(); }
   Step(const Params &P_, RUNTYPE runtype_) : P(P_), runtype(runtype_) { init(); }
   void operator++(int) { trueN++; ndxN++; }
   size_t N() const { return ndxN; }
   size_t ndx() const { return ndxN; }
   double scale() const { return P.SCALE(trueN+1); } // scale factor: current energy scale in units of bandwidth D
   double Teff() const { return scale()/P.betabar; }  // effective temperature for thermodynamic calculations
   double scT() const { return scale()/P.T; } // scT = scale*P.T, scaled physical temperature that appears in the exponents in spectral function calculations (Boltzmann weights)
   pair<size_t, size_t> NM() const {
     size_t N = ndxN / P.channels;
     size_t M = ndxN - N*P.channels; // M ranges 0..channels-1
     return {N, M};
   }
   // Compensate for different definition of SCALE in initial.m and C++ code in case of substeps==true.
   // Used in sym-qs.cc and sym-qsz.cc
   double scale_fix() const {
     const auto [N, M] = NM();
     my_assert(ndxN == N * P.channels + M);
     size_t N_at_end_of_full_step     = N * P.channels + P.channels - 1; // M=0,...,channels-1
     double scale_now                 = P.SCALE(ndxN + 1); // NOLINT
     double scale_at_end_of_full_step = P.SCALE(N_at_end_of_full_step + 1); // NOLINT
     return scale_now / scale_at_end_of_full_step;
   }
   void infostring() const {
     string info = " ***** [" + (runtype == RUNTYPE::NRG ? "NRG"s : "DM"s) + "] " 
       + "Iteration " + to_string(ndxN + 1) + "/" + to_string(P.Nmax) 
         + " (scale " + to_string(scale()) + ")" + " ***** ";
     if (P.substeps) {
       const auto [N, M] = NM();
       info += " step " + to_string(N + 1) + " substep " + to_string(M + 1);
     }
     cout << endl << info << endl;
   }
   void set_ZBW() {
     trueN = P.Ninit - 1; // if Ninit=0, trueN will be -1 (this is the only exceptional case)
     ndxN = P.Ninit;
   }
   // Return true if the spectral-function merging is to be performed at the current step
   bool N_for_merging() const {
     if (P.NN1) return true;
     if (P.NN2avg) return true;
     return P.NN2even ? IS_EVEN(ndxN) : IS_ODD(ndxN);
   }
   size_t firstndx() const { return P.Ninit; }
   size_t lastndx() const { return P.ZBW ? P.Ninit : P.Nmax-1; }
   // Return true if this is the first step of the NRG iteration
   bool first() const { return ndxN == firstndx(); }
   // Return true if N is the last step of the NRG iteration
   bool last(int N) const {
     return N == lastndx() || (P.ZBW && N == firstndx()); // special case!
   }
   bool last() const { return last(ndxN); }
   bool end() const { return ndxN >= P.Nmax; } // ndxN is outside the allowed range
   // NOTE: for ZBWcalculations, Ninit=0 and Nmax=0, so that first() == true and last() == true for ndxN=0.
   bool nrg() const { return runtype == RUNTYPE::NRG; }
   bool dmnrg() const { return runtype == RUNTYPE::DMNRG; }
   // Index 'n' of the last site in the existing chain, f_n (at iteration 'N'). The site being added is f_{n+1}. This
   // is the value that we use in building the matrix, cf. nrg-makematrix-ISO.cc
   int getnn() const { return ndxN; }
};

// Namespace for storing various statistical quantities calculated during iteration.
class Stats {
 public:
   // ** Ground-state subspace and energy at the current NRG iteration
   Invar ground;
   t_eigen Egs;
   
   // Find the ground state in the current NRG shell.
   void find_groundstate(const DiagInfo &diag) {
     Egs = DBL_MAX;
     for (const auto &[i, eig]: diag) {
       my_assert(eig.value_orig.size() > 0);
       t_eigen Emin = eig.value_orig(0); // Eigenvalues are sorted
       if (Emin < Egs) {
         Egs    = Emin;
         ground = i;
       }
     }
   }

   // ** Thermodynamic quantities
   double Z;
   double Zft;   // grand-canonical partition function (at shell n)
   double Zgt;   // grand-canonical partition function for computing G(T)
   double Zchit; // grand-canonical partition function for computing chi(T)

   double E;     // energy times beta
   double E2;    // (energy times beta) squared
   double C;     // heat capcity (in units of 1/k_B)
   double F;     // free energy times beta
   double S;     // entropy: (beta E)-(beta F)
   
   TD td;
   
   //  ** Expectation values
   map<string, t_expv> expv;    // expectation values of custom operators
   map<string, t_expv> fdmexpv; // Expectation values computed using the FDM algorithm
   
   // ** Energies
   // "total_energy" is the total energy of the ground state at the current iteration. This is the sum of all the 
   // zero state energies (eigenvalue shifts converted to absolute energies) for all the iteration steps so far.
   t_eigen total_energy;
   // GS_energy is the energy of the ground states in absolute units. It is equal to the value of the variable
   // "total_energy" at the end of the iteration.
   t_eigen GS_energy;
   std::vector<double> rel_Egs;        // Values of 'Egs' for all NRG steps.
   std::vector<double> abs_Egs;        // Values of 'Egs' (multiplied by the scale, i.e. in absolute scale) for all NRG steps.
   std::vector<double> energy_offsets; // Values of "total_energy" for all NRG steps.
   
   // Containers related to the FDM-NRG approach
   // ==========================================
   // Consult A. Weichselbaum, J. von Delft, PRL 99, 076402 (2007).
   vmpf ZnDG;                    // Z_n^D=\sum_s^D exp(-beta E^n_s), sum over **discarded** states at shell n
   vmpf ZnDN;                    // Z'_n^D=Z_n^D exp(beta E^n_0)=\sum_s^D exp[-beta(E^n_s-E^n_0)]
   std::vector<double> ZnDNd;    // 
   std::vector<double> wn;       // Weights w_n. They sum to 1.
   std::vector<double> wnfactor; // wn/ZnDG

   double ZZG;                   // grand-canonical partition function with energies referred to the ground state energy

   double Z_fdm;                 // grand-canonical partition function (full-shell) at temperature T
   double F_fdm;                 // free-energy at temperature T
   double E_fdm;                 // energy at temperature T
   double C_fdm;                 // heat capacity at temperature T
   double S_fdm;                 // entropy at temperature T
   
   TD_FDM td_fdm;

   Stats(const Params &P) : td(P, FN_TD), rel_Egs(MAX_NDX), abs_Egs(MAX_NDX), energy_offsets(MAX_NDX),
     ZnDG(MAX_NDX), ZnDN(MAX_NDX), ZnDNd(MAX_NDX), wn(MAX_NDX), wnfactor(MAX_NDX), td_fdm(P, FN_TDFDM) {}
};

void subtract_groundstate_energy(const Stats &stats, DiagInfo &diag) {
  for (auto &[i, eig] : diag) eig.subtract_Egs(stats.Egs);
}

// The absenergyG[] values are shifted so that the ground state corresponds
// to zero. This is required in the FDM approach for calculating the
// spectral functions. This is different from subtract_groundstate_energy().
// Called from do_diag() when diag is loaded from a stored file during
// the second pass of the NRG iteration.
// XXX: remove this. Use absenergyG in dm.
void shift_abs_energies(const Stats &stats, DiagInfo &diag) {
  for (auto &[i, eig] : diag) eig.shift_absenergyG(stats.GS_energy);
}

// called before calc_ZnD()
void shift_abs_energies_dm(const Stats &stats, AllSteps &dm)
{
  for (size_t N = P.Ninit; N < P.Nlen; N++)
    for (auto &[I, ds] : dm[N])
      for (size_t i = 0; i < ds.total; i++) {
        ds.absenergyG[i] -= stats.GS_energy;
        my_assert(ds.absenergyG[i] >= 0.0);
      }
}

class ChainSpectrum;
using spCS_t = shared_ptr<ChainSpectrum>;
class BaseSpectrum;

class SPEC {
  public:
  SPEC() = default;
  SPEC(const SPEC &) = default;
  SPEC(SPEC &&) = default;
  SPEC &operator=(const SPEC &) = default;
  SPEC &operator=(SPEC &&) = default;
  virtual ~SPEC() = default;
  virtual spCS_t make_cs(const BaseSpectrum &) = 0;
  virtual void calc(const Step &step, const Eigen &, const Eigen &, const Matrix &, const Matrix &, const BaseSpectrum &, t_factor, spCS_t, const Invar &,
                    const Invar &, const DensMatElements &, const Stats &stats){};
  virtual void calc_A(const Step &step, const Eigen &, const Eigen &, const Eigen &, const Matrix &, const Matrix &, const Matrix &, const BaseSpectrum &, t_factor,
                      spCS_t, const Invar &, const Invar &, const Invar &, const DensMatElements &, const Stats &stats){};
  virtual void calc_B(const Step &step, const Eigen &, const Eigen &, const Eigen &, const Matrix &, const Matrix &, const Matrix &, const BaseSpectrum &, t_factor,
                      spCS_t, const Invar &, const Invar &, const Invar &, const DensMatElements &, const Stats &stats){};
  virtual string name() = 0;
  virtual string merge() { return ""; }    // what merging rule to use
  virtual string rho_type() { return ""; } // what rho type is required
};

using SPECTYPE = shared_ptr<SPEC>;

#include "spectral.h"

// Returns true if option 'c' is selected for logging // XXX move to P?
bool logletter(char c) { return (sP.logall ? true : sP.log.find(c) != string::npos); }

#include "coef.cc"
#include "tridiag.h"
#include "diag.h"
#include "symmetry.cc"
#include "matrix.cc"
#include "recalc.cc"

// Select which symmetries to compile in.
#ifdef NRG_SYM_BASIC
#include "sym-QS.cc"
#include "sym-QSZ.cc"
#endif
#ifdef NRG_SYM_MORE
#include "sym-ISO.cc"
#include "sym-ISOSZ.cc"
#include "sym-SPSU2.cc"
#include "sym-SPU1.cc"
#endif
#ifdef NRG_SYM_ALL
#include "sym-DBLSU2.cc"
#include "sym-DBLISOSZ.cc"
#include "sym-ISOLR.cc"
#include "sym-ISOSZLR.cc"
#include "sym-NONE.cc"
#include "sym-P.cc"
#include "sym-PP.cc"
#include "sym-SL.cc"
#include "sym-SL3.cc"
#include "sym-SPSU2LR.cc"
#include "sym-SPSU2T.cc"
#include "sym-SPSU2C3.cc"
#include "sym-SPU1LR.cc"
#include "sym-SU2.cc"
#include "sym-QSLR.cc"
#include "sym-QSC3.cc"
#include "sym-QST.cc"
#include "sym-QSTZ.cc"
#include "sym-QSZTZ.cc"
#include "sym-QSZLR.cc"
#include "sym-QJ.cc"
#include "sym-U1.cc"
#endif

#include "read-input.cc"

// Formated output for the expectation values
template <typename T> string output_val(const T &x, size_t prec = std::numeric_limits<double>::max_digits10) {
  ostringstream F;
  F << setprecision(prec) << x;
  return F.str();
}

// Specialization for complex values, the output format is X+IY or
// X-IY, where X and Y are real and imaginary part, respectively. The
// imaginary part is only shown where its value relative to the real
// part is sufficiently large. No space is used in the outputted
// string in order to simplify parsing. This behavior can be turned
// off using P.noimag, which is the default.
const double OUTPUT_IMAG_EPS = 1.0e-13;
string output_val(const cmpl &val) {
  ostringstream F;
  if (P.noimag || abs(val.imag()) < abs(val.real()) * OUTPUT_IMAG_EPS) {
    F << val.real();
  } else {
    F << val.real();
    if (val.imag() > 0.0)
      F << "+I" << val.imag();
    else
      F << "-I" << -val.imag();
  }
  return F.str();
}

template <typename T> void formatted_output(ostream &F, T x, const Params &P) {
  // Important: setw first, setprecision second
  F << setw(P.width_custom) << setprecision(P.prec_custom) << x << ' ';
}

void formatted_output(ostream &F, cmpl val, const Params &P) {
  ostringstream str;
  // This sets precision for both real and imaginary parts.
  str << setprecision(P.prec_custom);
  if (P.noimag || abs(val.imag()) < abs(val.real()) * OUTPUT_IMAG_EPS) {
    str << val.real();
  } else {
    str << val.real();
    if (val.imag() > 0.0)
      str << "+I" << val.imag();
    else
      str << "-I" << -val.imag();
  }
  // The width for the whole X+IY string.
  F << setw(P.width_custom) << str.str() << ' ';
}

#include "bins.h"

/* Object of class 'ChainSpectrum' will contain information about the
 spectral density calculated at a given stage of the NRG run, i.e. for a
 finite Wilson chain.  We then merge it in an object of class 'Spectrum'
 which holds the spectral information for the entire run (i.e. the physical
 spectral density). */

class ChainSpectrum {
  public:
  virtual void add(double energy, t_weight weight) = 0;
  ChainSpectrum() = default;
  ChainSpectrum(const ChainSpectrum &) = default;
  ChainSpectrum(ChainSpectrum &&) = default;
  ChainSpectrum &operator=(const ChainSpectrum &) = default;
  ChainSpectrum &operator=(ChainSpectrum &&) = default;
  virtual ~ChainSpectrum() = default; // required, because there are virtual members
};

class ChainSpectrumBinning : public ChainSpectrum {
  private:
  Bins spos, sneg;
  public:
  void add(double energy, t_weight weight) override {
    if (energy >= 0.0)
      spos.add(energy, weight);
    else
      sneg.add(-energy, weight);
  }
  t_weight total_weight() const { return spos.total_weight() + sneg.total_weight(); }
  friend class SpectrumRealFreq;
};

class ChainSpectrumTemp : public ChainSpectrum {
  private:
  Temp v;
  public:
  void add(double T, t_weight value) override { v.add_value(T, value); }
  friend class SpectrumTemp;
};

#include "matsubara.h"
#include "matsubara2.h"

class ChainSpectrumMatsubara : public ChainSpectrum {
  private:
  Matsubara m;
  public:
  ChainSpectrumMatsubara() = delete;
  explicit ChainSpectrumMatsubara(matstype _mt) : m(P.mats, _mt){};
  void add(size_t n, t_weight w) { m.add(n, w); }
  void add(double energy, t_weight w) override { my_assert_not_reached(); }
  t_weight total_weight() const { return m.total_weight(); }
  friend class SpectrumMatsubara;
};

class ChainSpectrumMatsubara2 : public ChainSpectrum {
  private:
  Matsubara2 m;
  public:
  ChainSpectrumMatsubara2() = delete;
  explicit ChainSpectrumMatsubara2(matstype _mt) : m(P.mats, _mt){};
  void add(size_t i, size_t j, t_weight w) { m.add(i, j, w); }
  void add(double energy, t_weight w) override { my_assert_not_reached(); }
  t_weight total_weight() const { return m.total_weight(); }
  friend class SpectrumMatsubara2;
};

// Object of class spectrum will contain everything that we know about a
// spectral density.
class Spectrum {
  public:
  string opname, filename;
  SPECTYPE spectype;
  Spectrum(const string &_opname, const string &_filename, SPECTYPE _spectype) : opname(_opname), filename(_filename), spectype(_spectype){}; // NOLINT
  Spectrum(const Spectrum &) = default;
  Spectrum(Spectrum &&) = default;
  Spectrum &operator=(const Spectrum &) = default;
  Spectrum &operator=(Spectrum &&) = default;
  virtual ~Spectrum()= default; // required (the destructor saves the results to a file)
  virtual void merge(spCS_t, const Step &) = 0; // called from spec.cc as the very last step
  string name() { return opname; }
};

#include "spectrumrealfreq.cc"

// G(T) type of results, i.e. not a real spectrum
class SpectrumTemp : public Spectrum {
  private:
  std::vector<pair<double, t_weight>> results;
  public:
  SpectrumTemp(const string &_opname, const string &_filename, SPECTYPE _spectype) : Spectrum(_opname, _filename, _spectype) {}
  void merge(spCS_t, const Step &) override;
  SpectrumTemp(const SpectrumTemp &) = default;
  SpectrumTemp(SpectrumTemp &&) = default;
  SpectrumTemp &operator=(const SpectrumTemp &) = default;
  SpectrumTemp &operator=(SpectrumTemp &&) = default;
  ~SpectrumTemp() override;
};

void SpectrumTemp::merge(spCS_t cs, const Step & step) {
  auto t = dynamic_pointer_cast<ChainSpectrumTemp>(cs);
  copy(begin(t->v), end(t->v), back_inserter(results));
}

SpectrumTemp::~SpectrumTemp() {
  string fn = filename + ".dat";
  cout << "Spectrum: " << opname << " " << spectype->name() << " -> " << fn << endl;
  Spikes d(results);
  sort(begin(d), end(d), sortfirst());
  ofstream Fd = safe_open(fn);
  save_densfunc(Fd, d, P.reim);
}

// This container actually holds the GF on the Matsubara axis, not a
// spectral function.
class SpectrumMatsubara : public Spectrum {
  private:
  Matsubara results;
  public:
  SpectrumMatsubara(const string &_opname, const string &_filename, SPECTYPE _spectype, matstype _mt)
     : Spectrum(_opname, _filename, _spectype), results(P.mats, _mt) {}
  void merge(spCS_t, const Step &) override;
  SpectrumMatsubara(const SpectrumMatsubara &) = default;
  SpectrumMatsubara(SpectrumMatsubara &&) = default;
  SpectrumMatsubara &operator=(const SpectrumMatsubara &) = default;
  SpectrumMatsubara &operator=(SpectrumMatsubara &&) = default;
  ~SpectrumMatsubara() override;
};

void SpectrumMatsubara::merge(spCS_t cs, const Step &) {
  auto t = dynamic_pointer_cast<ChainSpectrumMatsubara>(cs);
  for (size_t n = 0; n < P.mats; n++) results.v[n].second += t->m.v[n].second;
}

SpectrumMatsubara::~SpectrumMatsubara() {
  cout << "Spectrum: " << opname << " " << spectype->name() << endl;
  ofstream Fd = safe_open(filename + ".dat");
  results.save(Fd);
}

class SpectrumMatsubara2 : public Spectrum {
  private:
  Matsubara2 results;
  public:
  SpectrumMatsubara2(const string &_opname, const string &_filename, SPECTYPE _spectype, matstype _mt)
     : Spectrum(_opname, _filename, _spectype), results(P.mats, _mt) {}
  void merge(spCS_t, const Step &) override;
  SpectrumMatsubara2(const SpectrumMatsubara2 &) = default;
  SpectrumMatsubara2(SpectrumMatsubara2 &&) = default;
  SpectrumMatsubara2 &operator=(const SpectrumMatsubara2 &) = default;
  SpectrumMatsubara2 &operator=(SpectrumMatsubara2 &&) = default;
  ~SpectrumMatsubara2() override;
};

void SpectrumMatsubara2::merge(spCS_t cs, const Step &) {
  auto t = dynamic_pointer_cast<ChainSpectrumMatsubara2>(cs);
  for (size_t m = 0; m < P.mats; m++)
    for (size_t n = 0; n < P.mats; n++) results.v(m, n) += t->m.v(m, n);
}

SpectrumMatsubara2::~SpectrumMatsubara2() {
  cout << "Spectrum: " << opname << " " << spectype->name() << endl;
  ofstream Fd = safe_open(filename + ".dat");
  results.save(Fd);
}

// This is mathematical trace, i.e. the sum of the diagonal elements.
CONSTFNC double trace(const DensMatElements &m) {
  double tr = 0.0;
  for (const auto &[I, mat] : m) tr += Sym->mult(I) * trace_real_nochecks(mat);
  return tr;
}

// Check if the trace of the density matrix equals 'ref_value'.
void check_trace_rho(const DensMatElements &m, double ref_value = 1.0) {
  const double tr = trace(m);
  if (!num_equal(trace(m), ref_value)) 
    throw std::runtime_error("check_trace_rho() failed");
}

enum class axis { RealFreq, Temp, Matsubara, Matsubara2 };

string axisstring(axis a) {
  switch (a) {
    case axis::RealFreq: return "RealFreq";
    case axis::Temp: return "Temp";
    case axis::Matsubara: return "Matsubara";
    case axis::Matsubara2: return "Matsubara,Matsubara";
    default: my_assert_not_reached();
  }
}

ostream &operator<<(ostream &os, const axis a) { return os << axisstring(a); }

// class BaseSpectrum contains all information about calculating the spectrum: pointers to the operator data and
// miscelaneous data, such as the spectrum type. Functions calc_specdens() et al. receive an object of this type as
// input.
class BaseSpectrum {
  public:
  string name;
  string prefix; // "dens", "corr", etc.
  size_t nr;     // number of operators
  const MatrixElements &op1, &op2, &op3;
  shared_ptr<Spectrum> spec;
  SPECTYPE spectype{}; // SPEC_FT, ...
  axis a;            // axis::RealFreq, axis::Temp, axis::Matsubara, etc.
  matstype mt;       // matstype::bosonic, matstype::fermionic, etc.
  int spin{};          // -1 or +1, or 0 where irrelevant
  string fullname() const {
    string s = name + " " + prefix + " " + spectype->name() + " " + axisstring(a);
    if (a != axis::RealFreq && a != axis::Temp) s += " " + matstypestring(mt);
    return s;
  }
  void about() { cout << "Spectrum: " << fullname() << endl; }
  BaseSpectrum(const MatrixElements &_op1, const MatrixElements &_op2) : 
     op1(_op1), op2(_op2), op3(_op2), a(axis::RealFreq), mt(matstype::fermionic) { nr = 2; } // op3 initialization is a hack
  BaseSpectrum(const MatrixElements &_op1, const MatrixElements &_op2, const MatrixElements &_op3) : 
     op1(_op1), op2(_op2), op3(_op3), a(axis::RealFreq), mt(matstype::fermionic) { nr = 3; }
};

// XXX: remove this, if possible
class speclist;
using lsl = list<speclist *>;
lsl allspectra; // list of list of spectra

class speclist {
  private:
  list<BaseSpectrum> spectra;

  public:
  speclist() { allspectra.push_back(this); }
  auto begin() { return spectra.begin(); }
  auto end() { return spectra.end(); }
  void push_back(BaseSpectrum &bs) { spectra.push_back(bs); }
  // Broaden spectra, close spectral files and deallocate all data storage (in destructor!)
  void clear() { spectra.clear(); }
  void about() {
    for (auto &i : spectra) i.about();
  }
};

speclist spectraD, spectraS, spectraT, spectraQ, spectraGT, spectraI1T, spectraI2T, spectraK, spectraCHIT, spectraC, spectraOT, spectraV3;

/**** CALCULATION OF SPECTRAL FUNCTIONS ****/

using IIfnc = std::function<t_factor(const Invar &, const Invar &)>;

// Operator sumrules.
double norm(const MatrixElements &m, const DiagInfo &diag, IIfnc factor_fnc, int SPIN) {
  weight_bucket sum;
  for (const auto &[Ip, eigp] : diag) {
    for (const auto &[I1, eig1] : diag) {
      if (!Sym->check_SPIN(I1, Ip, SPIN)) continue;
      if (auto it = m.find(Twoinvar{I1, Ip}); it != m.end()) {
        const auto mat = it->second;
        const auto spinfactor = factor_fnc(Ip, I1);
        for (size_t r1 = 0; r1 < mat.size1(); r1++)
          for (size_t rp = 0; rp < mat.size2(); rp++) 
            sum += spinfactor * sqr(abs(mat(r1, rp)));
      }
    }
  }
  // Factor 2: Tr[d d^\dag + d^\dag d] = 2 \sum_{i,j} A_{i,j}^2 !!
  return 2.0 * cmpl(sum).real();
}

#include "spec.cc"
#include "dmnrg.h"

// **** Helper functions for the NRG RUN ****

std::vector<t_eigen> sort_energies(const DiagInfo &diag) {
  std::vector<t_eigen> energies;
  for (const auto &[i, eig]: diag)
    energies.insert(end(energies), begin(eig.value_zero), end(eig.value_zero));
  sort(begin(energies), end(energies));
  return energies;
}

#include "splitting.cc"

inline size_t size_subspace(const DiagInfo &diag, const Invar &I) { // XXX : necessary??
  if (const auto f = diag.find(I); f != diag.cend())
    return f->second.getnr();
  else
    return 0;
}

// Determine the ranges of index r
Rmaxvals::Rmaxvals(const Invar &I, const InvarVec &InVec, const DiagInfo &diagprev) {
  values.resize(Sym->get_combs());
  for (size_t i = 0; i < Sym->get_combs(); i++) {
    const bool combination_allowed = Sym->triangle_inequality(I, InVec[i+1], Sym->QN[i+1]);
    values[i] = combination_allowed ? size_subspace(diagprev, InVec[i+1]) : 0;
  }
}

// *********************************** NRG RUN **********************************
// 
// Formatted output of the computed expectation values
class ExpvOutput {
 private:
   ofstream F;                // output stream
   map<string, t_expv> &m;    // reference to the name->value mapping
   const list<string> fields; // list of fields to be output (may be a subset of the fields actually present in m)
   const Params &P;
   void field_numbers() {     // Consecutive numbers for the columns
     F << "#";
     formatted_output(F, 1, P);
     for (size_t ctr = 1; ctr <= fields.size(); ctr++) formatted_output(F, 1 + ctr, P);
     F << endl;
   }
   // Label and field names. Label is the first column (typically the temperature).
   void field_names(string labelname = "T") {
     F << "#";
     formatted_output(F, labelname, P);
     for (const auto &op : fields) formatted_output(F, op, P);
     F << endl;
   }
 public:
   // Output the current values for the label and for all the fields
   void field_values(double labelvalue, bool cout_dump = true) {
     F << ' ';
     formatted_output(F, labelvalue, P);
     for (const auto &op : fields) formatted_output(F, m[op], P);
     F << endl;
     if (cout_dump)
       for (const auto &op: fields)
         std::cout << "<" << op << ">=" << m[op] << std::endl;
   }
   ExpvOutput(const string &fn, map<string, t_expv> &m_, const list<string> fields_, const Params &P_) : m(m_), fields(std::move(fields_)), P(P_) {
     F.open(fn);
     field_numbers();
     field_names();
   }
};

void open_files(speclist &sl, BaseSpectrum &spec, SPECTYPE spectype, axis a) {
  const string fn = spec.prefix + "_" + spectype->name() + "_dens_" + spec.name; // no suffix (.dat vs. .bin)
  switch (a) {
    case axis::RealFreq: spec.spec = make_shared<SpectrumRealFreq>(spec.name, fn, spectype); break;
    case axis::Temp: spec.spec = make_shared<SpectrumTemp>(spec.name, fn, spectype); break;
    case axis::Matsubara: spec.spec = make_shared<SpectrumMatsubara>(spec.name, fn, spectype, spec.mt); break;
    case axis::Matsubara2: spec.spec = make_shared<SpectrumMatsubara2>(spec.name, fn, spectype, spec.mt); break;
    default: my_assert_not_reached();
  }
  spec.spectype = spectype;
  spec.a        = a;
  sl.push_back(spec);
}

// open_files_spec() opens the output files and establishes the data structures
// for storing spectral information.
void open_files_spec(const RUNTYPE &runtype, speclist &sl, BaseSpectrum &spec) {
  if (spec.prefix == "gt") {
    if (runtype == RUNTYPE::NRG) open_files(sl, spec, make_shared<SPEC_GT>(), axis::Temp);
    return;
  }
  if (spec.prefix == "i1t") {
    if (runtype == RUNTYPE::NRG) open_files(sl, spec, make_shared<SPEC_I1T>(), axis::Temp);
    return;
  }
  if (spec.prefix == "i2t") {
    if (runtype == RUNTYPE::NRG) open_files(sl, spec, make_shared<SPEC_I2T>(), axis::Temp);
    return;
  }
  if (spec.prefix == "chit") {
    if (runtype == RUNTYPE::NRG) open_files(sl, spec, make_shared<SPEC_CHIT>(), axis::Temp);
    return;
  }
  // If we did not return from this funciton by this point, what we
  // are computing is the spectral function. There are several
  // possibilities in this case, all of which may be enabled at the
  // same time.
  if (runtype == RUNTYPE::NRG) {
    if (P.finite) open_files(sl, spec, make_shared<SPEC_FT>(), axis::RealFreq);
    if (P.finitemats) open_files(sl, spec, make_shared<SPEC_FTmats>(), axis::Matsubara);
  }
  if (runtype == RUNTYPE::DMNRG) {
    if (P.dmnrg) open_files(sl, spec, make_shared<SPEC_DMNRG>(), axis::RealFreq);
    if (P.dmnrgmats) open_files(sl, spec, make_shared<SPEC_DMNRGmats>(), axis::Matsubara);
    if (P.cfs) open_files(sl, spec, make_shared<SPEC_CFS>(), axis::RealFreq);
    if (P.cfsgt) open_files(sl, spec, make_shared<SPEC_CFSgt>(), axis::RealFreq);
    if (P.cfsls) open_files(sl, spec, make_shared<SPEC_CFSls>(), axis::RealFreq);
    if (P.fdm) open_files(sl, spec, make_shared<SPEC_FDM>(), axis::RealFreq);
    if (P.fdmgt) open_files(sl, spec, make_shared<SPEC_FDMgt>(), axis::RealFreq);
    if (P.fdmls) open_files(sl, spec, make_shared<SPEC_FDMls>(), axis::RealFreq);
    if (P.fdmmats) open_files(sl, spec, make_shared<SPEC_FDMmats>(), axis::Matsubara);
  }
}

void open_files_spec3(const RUNTYPE &runtype, speclist &sl, BaseSpectrum &spec) {
  if (runtype == RUNTYPE::DMNRG && P.fdm && P.v3mm) // both options, fdm and v3mm
    open_files(sl, spec, make_shared<SPEC_FDM_v3mm>(), axis::Matsubara2);
}

class Oprecalc {
 public:
   // The following lists hold the names of operators which need to be recomputed. The default behavior is to
   // recompute all the operators that are required to calculate the requested spectral densities, see function
   // open_files(). In addition, singlet operators are always recomputed in the first NRG run, so that we can
   // calculate the expectation values.
   set<string> s, p, g, d, v, t, q, ot;

   void report(ostream &F, const string &name, const set<string> &x) {
     F << name << "=[";
     for (const auto &i : x) F << i << ' ';
     F << "]" << endl;
   }

   void report(ostream &F = cout) {
     F << "Computing the following operators:" << endl;
     report(F, "s", s);
     report(F, "p", p);
     report(F, "g", g);
     report(F, "d", d);
     report(F, "v", v);
     report(F, "t", t);
     report(F, "q", q);
     report(F, "ot", ot);
   }
   
   bool do_s(const string &name, const Params &P, const Step &step) {
     if (step.nrg()) return true;                                          // for computing <O> 
     if (step.dmnrg() && P.fdmexpv && step.N() <= P.fdmexpvn) return true; // for computing <O> using FDM algorithm
     return s.count(name);
   }
   
   bool do_g(const string &name, const Params &P, const Step &step) {
     if (step.nrg()) return true;                                          // for computing <O>
     if (step.dmnrg() && P.fdmexpv && step.N() <= P.fdmexpvn) return true; // for computing <O> using FDM algorithm
     return g.count(name);
   }
   
   // Wrapper routine for recalculations
   template <typename RecalcFnc>
     MatrixElements recalc_common(const MatrixElements &mold, RecalcFnc recalc_fnc, const Step &step, const DiagInfo &diag, const QSrmax &qsrmax, const std::string name, const string &tip) {
       nrglog('0', "Recalculate " << tip << " " << name);
       auto mnew = recalc_fnc(diag, qsrmax, mold);
       if (tip == "g") Sym->recalc_global(step, diag, qsrmax, name, mnew);
       return mnew;
     }
   
   template <typename ... Args>
     MatrixElements recalc_or_clear(bool recalc, Args&& ... args) {
       return recalc ? recalc_common(std::forward<Args>(args)...) : MatrixElements();
     }

   // Recalculate operator matrix representations
   ATTRIBUTE_NO_SANITIZE_DIV_BY_ZERO // avoid false positives
     void recalculate_operators(const Step &step, const DiagInfo &diag, const QSrmax &qsrmax, IterInfo &a, const Params &P) {
       for (auto &[name, m] : a.ops)
         m = recalc_or_clear(do_s(name, P, step), m, [](const auto &... pr) { return recalc_singlet(pr..., 1);       }, step, diag, qsrmax, name, "s");
       for (auto &[name, m] : a.opsp)
         m = recalc_or_clear(p.count(name),       m, [](const auto &... pr) { return recalc_singlet(pr..., -1);      }, step, diag, qsrmax, name, "p");
       for (auto &[name, m] : a.opsg) 
         m = recalc_or_clear(do_g(name, P, step), m, [](const auto &... pr) { return recalc_singlet(pr..., 1);       }, step, diag, qsrmax, name, "g");
       for (auto &[name, m] : a.opd)
         m = recalc_or_clear(d.count(name),       m, [](const auto &... pr) { return Sym->recalc_doublet(pr...);     }, step, diag, qsrmax, name, "d");
       for (auto &[name, m] : a.opt)
         m = recalc_or_clear(t.count(name),       m, [](const auto &... pr) { return Sym->recalc_triplet(pr...);     }, step, diag, qsrmax, name, "t");
       for (auto &[name, m] : a.opot)
         m = recalc_or_clear(ot.count(name),      m, [](const auto &... pr) { return Sym->recalc_orb_triplet(pr...); }, step, diag, qsrmax, name, "ot");
       for (auto &[name, m] : a.opq)
         m = recalc_or_clear(q.count(name),       m, [](const auto &... pr) { return Sym->recalc_quadruplet(pr...);  }, step, diag, qsrmax, name, "q");
     }

   // Construct the suffix of the filename for spectral density files: 'A_?-A_?'.
   // If SPIN == 1 or SPIN == -1, '-u' or '-d' is appended to the string.
   string sdname(const string &a, const string &b, int spin = 0) {
     return a + "-" + b + (spin == 0 ? "" : (spin == 1 ? "-u" : "-d"));
   }

   void loopover(const RUNTYPE &runtype, 
                 const CustomOp &set1, const CustomOp &set2,
                 const string_token &stringtoken, speclist &spectra, const string &prefix,
                 set<string> &rec1, set<string> &rec2, matstype mt, int spin = 0) {
    for (const auto &[name1, op1] : set1) {
      for (const auto &[name2, op2] : set2) {
        if (const auto spname = sdname(name1, name2, spin); stringtoken.find(spname)) {
          BaseSpectrum spec(op1, op2);
          spec.name   = spname;
          spec.prefix = prefix;
          spec.mt     = mt;
          spec.spin   = spin;
          open_files_spec(runtype, spectra, spec);
          rec1.insert(name1);
          rec2.insert(name2);
        }
      }
    }
  }
   
  void loopover3(const RUNTYPE &runtype, 
                 const CustomOp &set1, const CustomOp &set2, const CustomOp &set3,
                 const string_token &stringtoken, speclist &spectra, const string &prefix, 
                 set<string> &rec1, set<string> &rec2, set<string> &rec3, matstype mt) {
    for (const auto &[name1, op1] : set1) {
      for (const auto &[name2, op2] : set2) {
        for (const auto &[name3, op3] : set3) {
          if (const auto spname = name1 + "-" + name2 + "-" + name3; stringtoken.find(spname)) {
            BaseSpectrum spec(op1, op2, op3);
            spec.name   = spname;
            spec.prefix = prefix;
            spec.mt     = mt;
            open_files_spec3(runtype, spectra, spec);
            rec1.insert(name1);
            rec2.insert(name2);
            rec3.insert(name3);
          }
        }
      }
    }
  }

  // Reset lists of operators which need to be iterated
  Oprecalc(const RUNTYPE &runtype, const IterInfo &a, const Params &P) {
    // Correlators (singlet operators of all kinds)
    string_token sts(P.specs);
    loopover(runtype, a.ops,  a.ops,  sts, spectraS, "corr", s, s, matstype::bosonic);
    loopover(runtype, a.opsp, a.opsp, sts, spectraS, "corr", p, p, matstype::bosonic);
    loopover(runtype, a.opsg, a.opsg, sts, spectraS, "corr", g, g, matstype::bosonic);
    loopover(runtype, a.ops,  a.opsg, sts, spectraS, "corr", s, g, matstype::bosonic);
    loopover(runtype, a.opsg, a.ops,  sts, spectraS, "corr", g, s, matstype::bosonic);
    // Global susceptibilities (global singlet operators)
    string_token stchit(P.specchit);
    loopover(runtype, a.ops,  a.ops,  stchit, spectraCHIT, "chit", s, s, matstype::bosonic);
    loopover(runtype, a.ops,  a.opsg, stchit, spectraCHIT, "chit", s, g, matstype::bosonic);
    loopover(runtype, a.opsg, a.ops,  stchit, spectraCHIT, "chit", g, s, matstype::bosonic);
    loopover(runtype, a.opsg, a.opsg, stchit, spectraCHIT, "chit", g, g, matstype::bosonic);
    // Dynamic spin susceptibilities (triplet operators)
    string_token stt(P.spect);
    loopover(runtype, a.opt, a.opt, stt, spectraT, "spin", t, t, matstype::bosonic);
    string_token stot(P.specot);
    loopover(runtype, a.opot, a.opot, stot, spectraOT, "orbspin", ot, ot, matstype::bosonic);
    const int varmin = (Sym->isfield() ? -1 : 0);
    const int varmax = (Sym->isfield() ? +1 : 0);
    // Spectral functions (doublet operators)
    string_token std(P.specd);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(runtype, a.opd, a.opd, std, spectraD, "spec", d, d, matstype::fermionic, SPIN);
    string_token stgt(P.specgt);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(runtype, a.opd, a.opd, stgt, spectraGT, "gt", d, d, matstype::fermionic, SPIN);
    string_token sti1t(P.speci1t);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(runtype, a.opd, a.opd, sti1t, spectraI1T, "i1t", d, d, matstype::fermionic, SPIN);
    string_token sti2t(P.speci2t);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(runtype, a.opd, a.opd, sti2t, spectraI2T, "i2t", d, d, matstype::fermionic, SPIN);
    // Spectral functions (quadruplet operators)
    string_token stq(P.specq);
    loopover(runtype, a.opq, a.opq, stq, spectraQ, "specq", q, q, matstype::fermionic);
    // Vertex functions
    string_token stv3(P.specv3);
    loopover3(runtype, a.opd, a.opd, a.ops, stv3, spectraV3, "specv3", d, d, s, matstype::fb);
    report();
    cout << endl << "Computing the following spectra:" << endl;
    for (const auto &i : allspectra) i->about();
  }
   
   ~Oprecalc() {
     TIME("broaden");
     for (auto &i : allspectra) i->clear();
   }
   
};

// Store eigenvalue & quantum numbers information (flow diagrams)
class Annotated {
 private:
   std::ofstream F;
   
   // scaled = true -> output scaled energies (i.e. do not multiply by the rescale factor)
   inline t_eigen scaled_energy(t_eigen e, const Step &step, const Stats &stats, bool scaled = true, bool absolute = false) {
     return e * (scaled ? 1.0 : step.scale()) + (absolute ? stats.total_energy : 0.0); // XXX stats.
   }
   
   const Params &P;

 public:
   Annotated(const Params &P_) : P(P_) {}
   
   void dump(const Step &step, const DiagInfo &diag, const Stats &stats) {
     if (P.dumpannotated && !F.is_open()) { // open output file
       F.open(FN_ANNOTATED);
       F << setprecision(P.dumpprecision);
     }
     std::vector<pair<t_eigen, Invar>> seznam;
     for (const auto &[I, eig] : diag)
       for (const auto e : eig.value_zero)
         seznam.emplace_back(e, I);
     sort(begin(seznam), end(seznam));
     size_t len = std::min<size_t>(seznam.size(), P.dumpannotated);
     // If states are clustered, we dump the full cluster
     for (size_t i = len; i < seznam.size() - 1; i++) {
       // If the next state has an energy within P.grouptol, add it to the list.
       if (my_fcmp(seznam[i].first, seznam[i - 1].first, P.grouptol) == 0)
         len++;
       else
         break;
     }
     my_assert(len <= seznam.size());
     auto scale = [&step, &stats, this](auto x) { return scaled_energy(x, step, stats, P.dumpscaled, P.dumpabs); };
     if (P.dumpgroups) {
       // Group by degeneracies
       for (size_t i = 0; i < len;) { // i increased in the while loop below
         const auto [e0, I0] = seznam[i];
         F << scale(e0);
         std::vector<string> QNstrings;
         size_t total_degeneracy = 0; // Total number of levels (incl multiplicity)
         while (i < len && my_fcmp(seznam[i].first, e0, P.grouptol) == 0) {
           const auto [e, I] = seznam[i];
           QNstrings.push_back(to_string(I));
           total_degeneracy += Sym->mult(I);
           i++;
         }
         sort(begin(QNstrings), end(QNstrings));
         for (const auto &j : QNstrings) F << " (" << j << ")";
         F << " [" << total_degeneracy << "]" << endl;
       }
     } else {
       seznam.resize(len); // truncate!
       for (const auto &[e, I] : seznam) 
         F << scale(e) << " " << I << endl;
     }
     F << std::endl; // Consecutive iterations are separated by an empty line
   }
};

// Handle all output
struct Output {
  RUNTYPE runtype;
  const Params &P;
  Annotated annotated;
  ofstream Fenergies;  // all energies (different file for NRG and for DMNRG)
  unique_ptr<ExpvOutput> custom;
  unique_ptr<ExpvOutput> customfdm;
  
  Output(const RUNTYPE &runtype_, const IterInfo &iterinfo, Stats &stats, const Params &P_) : runtype(runtype_), P(P_), annotated(P) {
    // We dump all energies to separate files for NRG and DM-NRG runs. This is a very convenient way to check if both
    // runs produce the same results.
    if (P.dumpenergies) Fenergies.open(runtype == RUNTYPE::NRG ? FN_ENERGIES_NRG : FN_ENERGIES_DMNRG);
    list<string> ops;
    for (const auto &[name, m] : iterinfo.ops)  ops.push_back(name);
    for (const auto &[name, m] : iterinfo.opsg) ops.push_back(name);
    if (runtype == RUNTYPE::NRG)
      custom = make_unique<ExpvOutput>(FN_CUSTOM, stats.expv, ops, P);
    else if (runtype == RUNTYPE::DMNRG && P.fdmexpv) 
      customfdm = make_unique<ExpvOutput>(FN_CUSTOMFDM, stats.fdmexpv, ops, P);
  }

  // Dump all energies in diag to a file
  void dump_all_energies(const DiagInfo &diag, int N) {
    if (!Fenergies) return;
    Fenergies << endl << "===== Iteration number: " << N << endl;
    for (const auto &[i, eig]: diag) {
      Fenergies << "Subspace: " << i << std::endl;
      for (const auto &x : eig.value_zero)
        Fenergies << x << ' ';
      Fenergies << std::endl;
    }
  }
};

CONSTFNC t_expv calc_trace_singlet(const DiagInfo &diag, const MatrixElements &n) {
  matel_bucket tr; // note: t_matel = t_expv
  for (const auto &[i, eig] : diag) {
    const auto & nI = n.at(Twoinvar{i,i});
    const size_t dim = eig.getnr();
    my_assert(dim == nI.size2());
    matel_bucket sum;
    for (size_t r = 0; r < dim; r++) sum += exp(-P.betabar * eig.value_zero(r)) * nI(r, r);
    tr += Sym->mult(i) * t_matel(sum);
  }
  return tr;
}

// Measure thermodynamic expectation values of singlet operators
void measure_singlet(const Step &step, Stats &stats, const DiagInfo &diag, const IterInfo &a, Output &output, const Params &P) {
  bucket Z;
  for (const auto &[I, eig] : diag)
    for (const auto &x : eig.value_zero)
      Z += Sym->mult(I) * exp(-P.betabar * x);
  for (const auto &[name, m] : a.ops)   stats.expv[name] = calc_trace_singlet(diag, m) / Z;
  for (const auto &[name, m] : a.opsg)  stats.expv[name] = calc_trace_singlet(diag, m) / Z;
  output.custom->field_values(step.Teff());
}

CONSTFNC t_expv calc_trace_fdm_kept(const Step &step, const DiagInfo &diag, const MatrixElements &n, const DensMatElements &rhoFDM, const AllSteps &dm) {
  matel_bucket tr;
  for (const auto &[i, eig] : diag) {
    const auto & nI = n.at(Twoinvar{i,i});
    const auto ret  = dm[step.N()].at(i).kept;
    const auto & mat = rhoFDM.at(i);
    matel_bucket sum;
    for (auto k = 0; k < ret; k++) // over kept states ONLY
      for (auto j = 0; j < ret; j++) 
        sum += mat(k, j) * nI(j, k);
    tr += Sym->mult(i) * t_matel(sum);
  }
  return tr;
}

// Expectation values using the FDM algorithm
void measure_singlet_fdm(const Step &step, Stats &stats, const DiagInfo &diag, const IterInfo &a, Output &output, const DensMatElements &rhoFDM, const AllSteps &dm) {
  for (const auto &[name, m] : a.ops)  stats.fdmexpv[name] = calc_trace_fdm_kept(step, diag, m, rhoFDM, dm);
  for (const auto &[name, m] : a.opsg) stats.fdmexpv[name] = calc_trace_fdm_kept(step, diag, m, rhoFDM, dm);
  output.customfdm->field_values(P.T);
}

void dump_diagonal_matrix(const Matrix &m, size_t max_nr, ostream &F)
{
  for (auto r = 0; r < std::min<size_t>(m.size1(), max_nr); r++)
    F << m(r,r) << ' ';
  F << std::endl;
}

void dump_diagonal_op(const DiagInfo &diag, const std::string name, const MatrixElements &n, ostream &F) {
  F << "Diagonal matrix elements of operator " << name << std::endl;
  for (const auto &[I, eig] : diag) {
    F << I << ": ";
    dump_diagonal_matrix(n.at(Twoinvar{I,I}), P.dumpdiagonal, F);
  }
}

void dump_diagonal(const DiagInfo &diag, const IterInfo &a, ostream &F = std::cout)
{
  for (const auto &[name, m] : a.ops)  dump_diagonal_op(diag, name, m, F);
  for (const auto &[name, m] : a.opsg) dump_diagonal_op(diag, name, m, F);
}

// DM-NRG: initialization of the density matrix -----------------------------

// Calculate grand canonical partition function at current NRG energy
// shell. This is not the same as the true partition function of the
// full problem! Instead this is the Z_N that is used to initialize
// the density matrix, i.e. rho = 1/Z_N \sum_{l} exp{-beta E_l} |l;N>
// <l;N|.  calc_grand_canonical_Z() is also used to calculate
// stats.Zft, that is used to compute the spectral function with the
// conventional approach, as well as stats.Zgt for G(T) calculations,
// stats.Zchit for chi(T) calculations.
double calc_grand_canonical_Z(const Step &step, const DiagInfo &diag, double factor = 1.0) {
  bucket ZN;
  for (const auto &[i, eig]: diag) 
    for (const auto &x : eig.value_zero)
      ZN += Sym->mult(i) * exp(-x * step.scT() * factor);
  my_assert(ZN >= 1.0);
  return ZN;
}

Matrix diagonal_exp(const Eigen &eig, double factor = 1.0)
{
  const auto dim = eig.getnr();
  Matrix m(dim, dim);
  m.clear();
  for (auto i = 0; i < dim; i++) 
      m(i, i) = exp(-eig.value_zero(i) * factor);
  return m;
}

// Calculate rho_N, the density matrix at the last NRG iteration. It is
// normalized to 1. Note: in CFS approach, we consider all states in the
// last iteration to be "discarded".
// For the details on the full Fock space approach see:
// F. B. Anders, A. Schiller, Phys. Rev. Lett. 95, 196801 (2005).
// F. B. Anders, A. Schiller, Phys. Rev. B 74, 245113 (2006).
// R. Peters, Th. Pruschke, F. B. Anders, Phys. Rev. B 74, 245114 (2006).
DensMatElements init_rho(const Step &step, const DiagInfo &diag) {
  DensMatElements rho;
  for (const auto &[I, eig]: diag)
    rho[I] = diagonal_exp(eig, step.scT()) / calc_grand_canonical_Z(step, diag);
  my_assert(num_equal(trace(rho), 1.0, 1e-8)); // NOLINT
  return rho;
}

/*** Truncation ***/

// Determine the number of states to be retained.
// Returns Emax - the highest energy to still be retained.
t_eigen highest_retained_energy(const DiagInfo &diag) {
  auto energies = sort_energies(diag);
  my_assert(energies.front() == 0.0); // check for the subtraction of Egs
  const size_t totalnumber = energies.size();
  size_t nrkeep;
  if (P.keepenergy <= 0.0) {
    nrkeep = P.keep;
  } else {
    double keepenergy = P.keepenergy;
    // We add 1 for historical reasons. We thus keep states with E<=Emax,
    // and one additional state which has E>Emax.
    nrkeep = 1 + count_if(begin(energies), end(energies), [=](double e) { return e <= keepenergy; });
    nrkeep = std::clamp<size_t>(nrkeep, P.keepmin, P.keep);
  }
  // Check for near degeneracy and ensure that the truncation occurs in a
  // "gap" between nearly-degenerate clusters of eigenvalues.
  if (P.safeguard > 0.0) {
    size_t cnt_extra = 0;
    while (nrkeep < totalnumber && (energies[nrkeep] - energies[nrkeep - 1]) <= P.safeguard && cnt_extra < P.safeguardmax) {
      nrkeep++;
      cnt_extra++;
    }
    if (cnt_extra) debug("Safeguard: keep additional " << cnt_extra << " states");
  }
  nrkeep = std::clamp<size_t>(nrkeep, 1, totalnumber);
  return energies[nrkeep - 1];
}

// Compute the number of states to keep in each subspace. Returns true if an insufficient number of states has been
// obtained in the diagonalization and we need to compute more states.
bool truncate_prepare(const Step &step, DiagInfo &diag, const Params &P) {
  const auto Emax = highest_retained_energy(diag);
  for (auto &[I, eig] : diag)
    diag[I].truncate_prepare_subspace(step.last() && P.keep_all_states_in_last_step() ? eig.getnr() :
                                      std::count_if(begin(eig.value_zero), end(eig.value_zero), [Emax](double e) { return e <= Emax; }));             
  const auto nrkept = std::accumulate(begin(diag), end(diag), 0, 
                                      [](int n, const auto &d) { const auto &[I, eig] = d; return n+eig.getnrkept(); });
  const auto nrkeptmult = std::accumulate(begin(diag), end(diag), 0, 
                                          [](int n, const auto &d) { const auto &[I, eig] = d; return n+Sym->mult(I)*eig.getnrkept(); });
  nrgdump3(Emax, nrkept, nrkeptmult) << endl;
  const auto notenough = std::any_of(begin(diag), end(diag), 
                                     [Emax](const auto &d) { const auto &[I, eig] = d; return eig.getnr() == eig.getnrkept() && eig.value_zero(eig.getnr()-1) != Emax &&
                                         eig.getnr() < eig.getdim(); });
  return notenough;
}

// Calculate partial statistical sums, ZnD*, and the grand canonical Z
// (stats.ZZG), computed with respect to absolute energies.
// calc_ZnD() must be called before the second NRG run.
void calc_ZnD(const AllSteps &dm, Stats &stats) {
  mpf_set_default_prec(400); // this is the number of bits, not decimal digits!
  for (size_t N = P.Ninit; N < P.Nlen; N++) { // here size_t, because Ninit>=0
    my_mpf ZnDG, ZnDN; // arbitrary-precision accumulators to avoid precision loss
    mpf_set_d(ZnDG, 0.0);
    mpf_set_d(ZnDN, 0.0);
    for (const auto &[I, ds] : dm[N])
      for (size_t i = ds.min(); i < ds.max(); i++) {
        my_mpf g, n;
        mpf_set_d(g, Sym->mult(I) * exp(-ds.absenergyG[i]/P.T)); // absenergyG >= 0.0
        mpf_set_d(n, Sym->mult(I) * exp(-ds.absenergyN[i]/P.T)); // absenergyN >= 0.0
        mpf_add(ZnDG, ZnDG, g);
        mpf_add(ZnDN, ZnDN, n);
      }
    mpf_set(stats.ZnDG[N], ZnDG);
    mpf_set(stats.ZnDN[N], ZnDN);
    stats.ZnDNd[N] = mpf_get_d(stats.ZnDN[N]);
  }
  // Note: for ZBW, Nlen=Nmax+1. For Ninit=Nmax=0, index 0 will thus be included here.
  my_mpf ZZG;
  mpf_set_d(ZZG, 0.0);
  for (size_t N = P.Ninit; N < P.Nlen; N++) {
    my_mpf a;
    mpf_set(a, stats.ZnDG[N]);
    my_mpf b;
    mpf_set_d(b, P.combs);
    mpf_pow_ui(b, b, P.Nlen - N - 1);
    my_mpf c;
    mpf_mul(c, a, b);
    mpf_add(ZZG, ZZG, c);
  }
  stats.ZZG = mpf_get_d(ZZG);
  bucket sumwn;
  for (size_t N = P.Ninit; N < P.Nlen; N++) {
    // This is w_n defined after Eq. (8) in the WvD paper.
    const double wn = pow(double(P.combs), int(P.Nlen - N - 1)) * mpf_get_d(stats.ZnDG[N]) / stats.ZZG;
    stats.wn[N] = wn;
    sumwn += wn;
    const double w = pow(double(P.combs), int(P.Nlen - N - 1)) / stats.ZZG;
    stats.wnfactor[N] = w; // These ratios enter the terms for the spectral function.
  }
  cout << "ZZG=" << HIGHPREC(stats.ZZG) << endl;
  cout << "sumwn=" << sumwn << " sumwn-1=" << sumwn - 1.0 << endl;
  if (logletter('w')) {
    for (size_t N = P.Ninit; N < P.Nlen; N++) 
      cout << "ZG[" << N << "]=" << HIGHPREC(mpf_get_d(stats.ZnDG[N])) << endl;
    for (size_t N = P.Ninit; N < P.Nlen; N++) 
      cout << "ZN[" << N << "]=" << HIGHPREC(mpf_get_d(stats.ZnDN[N])) << endl;
    for (size_t N = P.Ninit; N < P.Nlen; N++) 
      cout << "w[" << N << "]=" << HIGHPREC(stats.wn[N]) << endl;
    for (size_t N = P.Ninit; N < P.Nlen; N++)
      cout << "wfactor[" << N << "]=" << HIGHPREC(stats.wnfactor[N]) << endl;
  }
  my_assert(num_equal(sumwn, 1.0));  // Check the sum-rule.
}

// TO DO: use Boost.Multiprecision instead of low-level GMP calls
// https://www.boost.org/doc/libs/1_72_0/libs/multiprecision/doc/html/index.html
void fdm_thermodynamics(const AllSteps &dm, Stats &stats)
{
  stats.td_fdm.T = P.T;
  stats.Z_fdm = stats.ZZG*exp(-stats.GS_energy/P.T); // this is the true partition function
  stats.td_fdm.F = stats.F_fdm = -log(stats.ZZG)*P.T+stats.GS_energy; // F = -k_B*T*log(Z)
  // We use multiple precision arithmetics to ensure sufficient accuracy in the calculation of
  // the variance of energy and thus the heat capacity.
  my_mpf E, E2;
  mpf_set_d(E, 0.0);
  mpf_set_d(E2, 0.0);
  for (size_t N = P.Ninit; N < P.Nlen; N++)
    if (stats.wn[N] > 1e-16) 
      for (const auto &[I, ds] : dm[N]) 
        for (size_t i = ds.min(); i < ds.max(); i++) {
          my_mpf weight;
          mpf_set_d(weight, stats.wn[N] * Sym->mult(I) * exp(-ds.absenergyN[i]/P.T));
          mpf_div(weight, weight, stats.ZnDN[N]);
          my_mpf e;
          mpf_set_d(e, ds.absenergy[i]);
          my_mpf e2;
          mpf_mul(e2, e, e);
          mpf_mul(e, e, weight);
          mpf_mul(e2, e2, weight);
          mpf_add(E, E, e);
          mpf_add(E2, E2, e2);
        }
  stats.td_fdm.E = stats.E_fdm = mpf_get_d(E);
  my_mpf sqrE;
  mpf_mul(sqrE, E, E);
  my_mpf varE;
  mpf_sub(varE, E2, sqrE);
  stats.td_fdm.C = stats.C_fdm = mpf_get_d(varE)/pow(double(P.T),2);
  stats.td_fdm.S = stats.S_fdm = (stats.E_fdm-stats.F_fdm)/P.T;
  cout << endl;
  cout << "Z_fdm=" << HIGHPREC(stats.Z_fdm) << endl;
  cout << "F_fdm=" << HIGHPREC(stats.F_fdm) << endl;
  cout << "E_fdm=" << HIGHPREC(stats.E_fdm) << endl;
  cout << "C_fdm=" << HIGHPREC(stats.C_fdm) << endl;
  cout << "S_fdm=" << HIGHPREC(stats.S_fdm) << endl;
  cout << endl;
  stats.td_fdm.save_values();
}

// Actually truncate matrices. Drop subspaces with no states kept.
void truncate_perform(DiagInfo &diag) {
  for (auto &[I, eig] : diag) eig.truncate_perform(); // Truncate subspace to appropriate size
}

// Recalculates irreducible matrix elements of a singlet operator, as well as odd-parity spin-singlet operator (for
// parity -1). Generic implementation, valid for all symmetry types.
MatrixElements recalc_singlet(const DiagInfo &diag, const QSrmax &qsrmax, const MatrixElements &nold, int parity) {
  MatrixElements nnew;
  std::vector<Recalc> recalc_table(Sym->get_combs());
  my_assert(Sym->islr() ? parity == 1 || parity == -1 : parity == 1);
  for (const auto I : diag | boost::adaptors::map_keys) {
    const Invar I1 = I;
    const Invar Ip = (parity == -1 ? I.InvertParity() : I);
    for (size_t i = 1; i <= P.combs; i++) {
      Recalc r;
      r.i1 = r.ip = i;
      r.factor    = 1.0;
      const auto anc = Sym->ancestor(I, i);
      r.IN1 = anc;
      r.INp = (parity == -1 ? anc.InvertParity() : anc);
      recalc_table[i - 1] = r; // mind the -1 shift!
    }
    const auto Iop = (parity == -1 ? (Sym->InvarSinglet).InvertParity() : Sym->InvarSinglet);
    recalc_general(diag, qsrmax, nold, nnew, I1, Ip, &recalc_table[0], P.combs, Iop);
  }
  return nnew;
}

/* We trim the matrices containing the irreducible matrix elements of the
 operators to the sizes that are actually required in the next iterations.
 This saves memory and leads to better cache usage in recalc_general()
 recalculations. Note: this is only needed for strategy=all; copying is
 avoided for strategy=kept. */
void trim_matel(DiagInfo &diag, MatrixElements &op) {
  for (auto &[II, mat] : op) {
    const auto &[I1, I2] = II;
    // Current matrix dimensions
    const auto size1 = mat.size1();
    const auto size2 = mat.size2();
    if (size1 == 0 || size2 == 0) continue;
    // Target matrix dimensions
    const auto nr1 = diag[I1].getnr();
    const auto nr2 = diag[I2].getnr();
    my_assert(nr1 <= size1 && nr2 <= size2);
    if (nr1 == size1 && nr2 == size2) // Trimming not necessary!!
      continue;
    ublas::matrix_range<Matrix> m2(mat, ublas::range(0, nr1), ublas::range(0, nr2));
    Matrix m2new = m2;
    mat.swap(m2new);
  }
}

void trim_op(DiagInfo &diag, CustomOp &allops) {
  for (auto &[name, op] : allops) 
    trim_matel(diag, op);
}

void trim_matrices(DiagInfo &diag, IterInfo &a) {
  trim_op(diag, a.ops);
  trim_op(diag, a.opsp);
  trim_op(diag, a.opsg);
  trim_op(diag, a.opd);
  trim_op(diag, a.opt);
  trim_op(diag, a.opot);
  trim_op(diag, a.opq);
}

void clear_eigenvectors(DiagInfo &diag) {
  for (auto &[i, eig] : diag)
    for (auto &j : eig.blocks) 
      j = Matrix(0, 0);
}

auto CorrelatorFactorFnc   = [](const Invar &Ip, const Invar &I1) { return Sym->mult(I1); };
auto SpecdensFactorFnc     = [](const Invar &Ip, const Invar &I1) { return Sym->specdens_factor(Ip, I1); };
auto SpecdensquadFactorFnc = [](const Invar &Ip, const Invar &I1) { return Sym->specdensquad_factor(Ip, I1); };
auto SpinSuscFactorFnc     = [](const Invar &Ip, const Invar &I1) { return Sym->dynamicsusceptibility_factor(Ip, I1); };
auto OrbSuscFactorFnc      = [](const Invar &Ip, const Invar &I1) { return Sym->dynamic_orb_susceptibility_factor(Ip, I1); };
auto TrivialCheckSpinFnc   = [](const Invar &Ip, const Invar &I1, int SPIN) { return true; };
auto SpecdensCheckSpinFnc  = [](const Invar &I1, const Invar &Ip, int SPIN) { return Sym->check_SPIN(I1, Ip, SPIN); };

// Calculate spectral densities
void spectral_densities(const Step &step, const DiagInfo &diag, DensMatElements &rho, DensMatElements &rhoFDM, const Stats &stats) {
  TIME("spec");
  for (auto &i : spectraS)    calc_generic(i, step, diag, CorrelatorFactorFnc,   TrivialCheckSpinFnc,  rho, rhoFDM, stats);
  for (auto &i : spectraCHIT) calc_generic(i, step, diag, CorrelatorFactorFnc,   TrivialCheckSpinFnc,  rho, rhoFDM, stats);
  for (auto &i : spectraD)    calc_generic(i, step, diag, SpecdensFactorFnc,     SpecdensCheckSpinFnc, rho, rhoFDM, stats);
  for (auto &i : spectraT)    calc_generic(i, step, diag, SpinSuscFactorFnc,     TrivialCheckSpinFnc,  rho, rhoFDM, stats);
  for (auto &i : spectraOT)   calc_generic(i, step, diag, OrbSuscFactorFnc,      TrivialCheckSpinFnc,  rho, rhoFDM, stats);
  for (auto &i : spectraQ)    calc_generic(i, step, diag, SpecdensquadFactorFnc, TrivialCheckSpinFnc,  rho, rhoFDM, stats);
  for (auto &i : spectraGT)   calc_generic(i, step, diag, SpecdensFactorFnc,     SpecdensCheckSpinFnc, rho, rhoFDM, stats);
  for (auto &i : spectraI1T)  calc_generic(i, step, diag, SpecdensFactorFnc,     SpecdensCheckSpinFnc, rho, rhoFDM, stats);
  for (auto &i : spectraI2T)  calc_generic(i, step, diag, SpecdensFactorFnc,     SpecdensCheckSpinFnc, rho, rhoFDM, stats);
  // no CheckSpinFnc!! One must use A_u_d, etc. objects for sym=QSZ.
  for (auto &i : spectraV3)   calc_generic3(i, step, diag, SpecdensFactorFnc,                          rho, rhoFDM, stats);
}

void operator_sumrules(const DiagInfo &diag, const IterInfo &a) {
  // We check sum rules wrt some given spin (+1/2, by convention). For non-spin-polarized calculations, this is
  // irrelevant (0).
  const int SPIN = (Sym->isfield() ? 1 : 0);
  for (const auto &[name, m] : a.opd)
    cout << "norm[" << name << "]=" << norm(m, diag, SpecdensFactorFnc, SPIN) << std::endl;
  for (const auto &[name, m] : a.opq)
    cout << "norm[" << name << "]=" << norm(m, diag, SpecdensquadFactorFnc, 0) << std::endl;
}

// We calculate thermodynamic quantities before truncation to make better use of the available states. Here we
// compute quantities which are defined for all symmetry types. Other calculations are performed by calculate_TD
// member functions defined in symmetry.cc.
void calculate_TD(const Step &step, const DiagInfo &diag, Stats &stats, Output &output, double additional_factor = 1.0) {
  // Rescale factor for energies. The energies are expressed in
  // units of omega_N, thus we need to appropriately rescale them to
  // calculate the Boltzmann weights at the temperature scale Teff
  // (Teff=scale/betabar).
  double rescale_factor = P.betabar * additional_factor;
  bucket Z, E, E2; // Statistical sum, Tr[beta H], Tr[(beta H)^2]
  for (const auto &[i, eig] : diag) {
    bucket sumZ, sumE, sumE2;
    for (const auto &x : eig.value_zero) {
      const double betaE = rescale_factor * x;
      const double expo  = exp(-betaE);
      sumE += betaE * expo;
      sumE2 += sqr(betaE) * expo;
      sumZ += expo;
    }
    const int multip = Sym->mult(i);     // take into account the multiplicity
    Z += multip * sumZ;
    E += multip * sumE;
    E2 += multip * sumE2;
  }
  stats.Z = Z;
  stats.td.T  = step.Teff();
  stats.td.E  = stats.E = E / Z;                   // beta <H>
  stats.td.E2 = stats.E2 = E2 / Z;                 // beta^2 <H^2>
  stats.td.C  = stats.C = stats.E2 - sqr(stats.E); // C/k_B=beta^2(<H^2>-<H>^2)
  stats.td.F  =  stats.F = -log(Z);                // F/(k_B T)=-ln(Z)
  stats.td.S  = stats.S = stats.E - stats.F;       // S/k_B=beta<H>+ln(Z)
  Sym->calculate_TD(step, diag, stats, rescale_factor);  // symmetry-specific calculation routine
  stats.td.save_values();
}

inline bool need_rho() { return P.cfs || P.dmnrg; }
inline bool need_rhoFDM() { return P.fdm; }

void calculate_spectral_and_expv(const Step &step, Stats &stats, Output &output, const DiagInfo &diag, const IterInfo &iterinfo, const AllSteps &dm, const Params &P) {
  // Zft is used in the spectral function calculations using the
  // conventional approach. We calculate it here, in order to avoid
  // recalculations later on.
  stats.Zft = calc_grand_canonical_Z(step, diag);
  if (string(P.specgt) != "" || string(P.speci1t) != "" || string(P.speci2t) != "")
    stats.Zgt = calc_grand_canonical_Z(step, diag, 1.0/(P.gtp*step.scT()) ); // exp(-x*gtp)
  if (string(P.specchit) != "") 
    stats.Zchit = calc_grand_canonical_Z(step, diag, 1.0/(P.chitp*step.scT()) ); // exp(-x*chitp)
  DensMatElements rho, rhoFDM;
  if (step.dmnrg()) {
      if (need_rho()) {
        rho = loadRho(step.ndx(), FN_RHO, P);
        if (P.checkrho)
          check_trace_rho(rho); // Check if Tr[rho]=1, i.e. the normalization
      }
      if (need_rhoFDM()) 
        rhoFDM = loadRho(step.ndx(), FN_RHOFDM, P);
  }
  spectral_densities(step, diag, rho, rhoFDM, stats);
  if (step.nrg()) measure_singlet(step, stats, diag, iterinfo, output, P);
  if (step.nrg() && P.dumpdiagonal) dump_diagonal(diag, iterinfo);
  if (step.dmnrg() && P.fdmexpv && step.N() == P.fdmexpvn) measure_singlet_fdm(step, stats, diag, iterinfo, output, rhoFDM, dm);
}

// Perform calculations of physical quantities. Called prior to NRG iteration (if calc0=true) and after each NRG
// step.
void perform_measurements(const Step &step, const DiagInfo &diag, Stats &stats, Output &output) {
  output.dump_all_energies(diag, step.ndx());
  if (step.nrg()) calculate_TD(step, diag, stats, output);
  if (step.nrg()) output.annotated.dump(step, diag, stats);
}

// Make a list of subspaces for the new iteration. Generic implementation: use the quantum number differences in
// array In[] (obtained by a call to function input_subspaces), make a list of all possible subspaces and remove
// duplicates.
auto make_subspaces_list(const DiagInfo &diagprev) {
  list<Invar> subspaces;
  for (const auto &[I, eig] : diagprev)
    if (eig.getnr()) {
      auto input = Sym->input_subspaces(); // make a new copy of subspaces list
      for (size_t i = 1; i <= P.combs; i++) {
        input[i].inverse(); // IMPORTANT!
        input[i].combine(I);
        if (Sym->Invar_allowed(input[i])) 
          subspaces.push_back(input[i]);
      }
    }
  subspaces.sort();
  subspaces.unique();
  return subspaces;
}

/* Define recalculation strategy
 all: Recalculate using all vectors
 kept: Recalculate using vectors kept after truncation
 VERY IMPORTANT: Override in the case of CFS (in the second run) */
bool do_recalc_kept(const Step &step, const Params &P) { return string(P.strategy) == "kept" && !(P.cfs_flags() && step.dmnrg()) && !P.ZBW; }
bool do_recalc_all(const Step &step, const Params &P) { return !do_recalc_kept(step, P) && !P.ZBW; }
bool do_no_recalc(const Step &step, const Params &P) { return P.ZBW; }

Matrix prepare_task_for_diag(const Step &step, const Invar &I, const Opch &opch, const DiagInfo &diagprev, const Params &P) {
  const auto anc = Sym->ancestors(I);
  const Rmaxvals rm{I, anc, diagprev};
  const size_t dim = rm.total();
  Matrix h(dim, dim);
  h.clear();
  const double scalefactor = (!P.substeps ? sqrt(P.Lambda) : pow(P.Lambda, 1. / (2. * P.channels))); // NOLINT
  // H_{N+1}=\lambda^{1/2} H_N+\xi_N (hopping terms)
  for (size_t i = 1; i <= P.combs; i++) {
    const size_t offset = rm.offset(i);
    for (size_t r = 0; r < rm.rmax(i); r++) 
      h(offset + r, offset + r) = scalefactor * diagprev.at(anc[i]).value_zero(r);
  }
  // Symmetry-type-specific matrix initialization steps.
  Sym->makematrix(h, step, rm, I, anc, opch);
  if (logletter('m')) dump_matrix(h);
  return h;
}

DiagInfo diagonalisations_OpenMP(const Step &step, const Opch &opch, const DiagInfo &diagprev, const std::vector<Invar> &tasks, const Params &P) {
  DiagInfo diagnew;
  size_t nr = tasks.size();
  size_t itask = 0;
  // cppcheck-suppress unreadVariable symbolName=nth
  int nth = P.diagth; // NOLINT
#pragma omp parallel for schedule(dynamic) num_threads(nth)
  for (itask = 0; itask < nr; itask++) {
    const Invar I  = tasks[itask];
    auto h = prepare_task_for_diag(step, I, opch, diagprev, P);
    int thid = omp_get_thread_num();
#pragma omp critical
    { nrglog('(', "Diagonalizing " << I << " size=" << h.size1() << " (task " << itask + 1 << "/" << nr << ", thread " << thid << ")"); }
    Eigen e = diagonalise(h);
#pragma omp critical
    { diagnew[I] = e; }
  }
  return diagnew;
}

#ifdef NRG_MPI
const int TAG_EXIT           = 2;
const int TAG_DIAG           = 3;
const int TAG_SYNC           = 4;
const int TAG_MATRIX         = 5;
const int TAG_INVAR          = 6;
const int TAG_EIGEN          = 7;
const int TAG_MATRIX_SIZE    = 8;
const int TAG_MATRIX_LINE    = 9;
const int TAG_EIGEN_INT      = 10;
const int TAG_EIGEN_VEC      = 11;
const int TAG_EIGEN_RMAXVALS = 12;

void mpi_sync_params();

void check_status(mpi::status status) {
  if (status.error()) {
    cout << "MPI communication error. rank=" << mpiw->rank() << endl;
    mpienv->abort(1);
  }
}

// NOTE: MPI is limited to message size of 2GB (or 4GB). For big
// problems we thus need to send objects line by line.

//#define MPI_WHOLEMATRIX
#define MPI_LINEBYLINE

#ifdef MPI_WHOLEMATRIX
#define mpi_send_matrix mpi_send_matrix_wholematrix
#define mpi_receive_matrix mpi_receive_matrix_wholematrix
#define mpi_send_eigen mpi_send_eigen_whole
#define mpi_receive_eigen mpi_receive_eigen_whole
#endif

#ifdef MPI_LINEBYLINE
#define mpi_send_matrix mpi_send_matrix_linebyline
#define mpi_receive_matrix mpi_receive_matrix_linebyline
#define mpi_send_eigen mpi_send_eigen_linebyline
#define mpi_receive_eigen mpi_receive_eigen_linebyline
#endif

void mpi_send_matrix_wholematrix(int dest, Matrix &m) { 
  mpiw->send(dest, TAG_MATRIX, m); 
}

auto mpi_receive_matrix_wholematrix(int source) {
  Matrix m;
  check_status(mpiw->recv(source, TAG_MATRIX, m));
  return m;
}

void mpi_send_matrix_linebyline(int dest, const Matrix &m) {
  auto size1 = m.size1();
  mpiw->send(dest, TAG_MATRIX_SIZE, size1);
  auto size2 = m.size2();
  mpiw->send(dest, TAG_MATRIX_SIZE, size2);
  nrglog('M', "Sending matrix of size " << size1 << " x " << size2 << " line by line to " << dest);
  for (size_t i = 0; i < size1; i++) {
    ublas::vector<t_matel> vec = ublas::matrix_row<const Matrix>(m, i);
    mpiw->send(dest, TAG_MATRIX_LINE, vec);
  }
}

auto mpi_receive_matrix_linebyline(int source) {
  size_t size1;
  check_status(mpiw->recv(source, TAG_MATRIX_SIZE, size1));
  size_t size2;
  check_status(mpiw->recv(source, TAG_MATRIX_SIZE, size2));
  nrglog('M', "Receiving matrix of size " << size1 << " x " << size2 << " line by line from " << source);
  Matrix m(size1, size2);
  for (auto i = 0; i < size1; i++) {
    ublas::vector<t_matel> vec;
    check_status(mpiw->recv(source, TAG_MATRIX_LINE, vec));
    my_assert(vec.size() == size2);
    ublas::matrix_row<Matrix>(m, i) = vec;
  }
  return m;
}

void mpi_send_eigen_whole(int dest, const Eigen &eig) { 
  mpiw->send(dest, TAG_EIGEN, eig); 
}

auto mpi_receive_eigen_whole(int source) {
  Eigen eig;
  check_status(mpiw->recv(source, TAG_EIGEN, eig));
  return eig;
}

void mpi_send_eigen_linebyline(int dest, const Eigen &eig) {
  Eigen eigmock; // empty Eigen
  mpiw->send(dest, TAG_EIGEN, eigmock);
  nrglog('M', "Sending eigen from " << mpiw->rank() << " to " << dest);
  mpiw->send(dest, TAG_EIGEN_VEC, eig.value_orig);
  mpi_send_matrix_linebyline(dest, eig.matrix);
}

auto mpi_receive_eigen_linebyline(int source) {
  nrglog('M', "Receiving eigen from " << source << " on " << mpiw->rank());
  Eigen eigmock;
  check_status(mpiw->recv(source, TAG_EIGEN, eigmock));
  Eigen eig;
  check_status(mpiw->recv(source, TAG_EIGEN_VEC, eig.value_orig));
  eig.matrix = mpi_receive_matrix_linebyline(source);
  return eig;
}

// Read results from a slave process.
std::pair<Invar, Eigen> read_from(int source) {
  nrglog('M', "Reading results from " << source);
  auto eig = mpi_receive_eigen(source);
  Invar Irecv;
  check_status(mpiw->recv(source, TAG_INVAR, Irecv));
  nrglog('M', "Received results for subspace " << Irecv << " [nr=" << eig.getnr() << ", dim=" << eig.getdim() << "]");
  my_assert(eig.value_orig.size() == eig.matrix.size1());
  my_assert(eig.matrix.size1() <= eig.matrix.size2());
  return {Irecv, eig};
}

DiagInfo diagonalisations_MPI(const Step &step, const Opch &opch, const DiagInfo &diagprev, const std::vector<Invar> &tasks, const Params &P) {
  DiagInfo diagnew;
  mpi_sync_params(); // Synchronise parameters
  list<Invar> todo; // List of all the tasks to handle
  copy(begin(tasks), end(tasks), back_inserter(todo));
  list<Invar> done; // List of finished tasks.
  // List of the available computation nodes (including the master,
  // which is always at the very beginnig of the deque).
  deque<int> nodes;
  for (auto i = 0; i < mpiw->size(); i++) nodes.push_back(i);
  nrglog('M', "nrtasks=" << tasks.size() << " nrnodes=" << mpiw->size());
  while (!todo.empty()) {
    my_assert(!nodes.empty());
    // i is the node to which the next job will be scheduled
    int i;
    if (todo.size() == 1) {
      // If a single task is left undone, do it on the master node
      // to avoid the unnecessary network copying.
      i = 0;
    } else {
      i = nodes.back();
      nodes.pop_back();
    }
    Invar I;
    if (i == 0) {
      // On master, we take short jobs from the end of the list.
      I = todo.back();
      todo.pop_back();
    } else {
      // On slaves, we take long jobs from the beginning of the
      // list.
      I = todo.front();
      todo.pop_front();
    }
    auto h = prepare_task_for_diag(step, I, opch, diagprev, P);
    nrglog('M', "Scheduler: job " << I << " (dim=" << h.size1() << ")" << " on node " << i);
    if (i == 0) {
      // On master, diagonalize immediately.
      diagnew[I] = diagonalise(h);
      nodes.push_back(0);
      done.push_back(I);
    } else {
      mpiw->send(i, TAG_DIAG, 0);
      mpi_send_matrix(i, h);
      mpiw->send(i, TAG_INVAR, I);
    }
    // Check for terminated jobs
    while (auto status = mpiw->iprobe(mpi::any_source, TAG_EIGEN)) {
      nrglog('M', "Receiveing results from " << status->source());
      auto [Irecv, eig] = read_from(status->source());
      diagnew[Irecv] = eig;
      done.push_back(Irecv);
      // The node is now available for new tasks!
      nodes.push_back(status->source());
    }
  }
  // Keep reading results sent from the slave processes until all tasks have been completed.
  while (done.size() != tasks.size()) {
    auto status = mpiw->probe(mpi::any_source, TAG_EIGEN);
    auto [Irecv, eig]  = read_from(status.source());
    diagnew[Irecv] = eig;
    done.push_back(Irecv);
  }
  return diagnew;
}
#endif

// Build matrix H(ri;r'i') in each subspace and diagonalize it
DiagInfo diagonalisations(const Step &step, const Opch &opch, const DiagInfo &diagprev, 
                          const std::vector<Invar> &tasks, double diagratio, const Params &P) {
  TIME("diag");
  sP.init(P, diagratio);
#ifdef NRG_MPI
  return diagonalisations_MPI(step, opch, diagprev, tasks, P);
#else
  return diagonalisations_OpenMP(step, opch, diagprev, tasks, P);
#endif
}

// Determine the structure of matrices in the new NRG shell
QSrmax get_qsrmax(const DiagInfo &diagprev) {
  QSrmax qsrmax;
  for (const auto &I : make_subspaces_list(diagprev))
    qsrmax[I] = Rmaxvals{I, Sym->ancestors(I), diagprev};
  return qsrmax;
}

// List of invariant subspaces in which diagonalisations need to be performed
std::vector<Invar> task_list(const QSrmax &qsrmax) {
  std::vector<Invar> tasks;
  for (const auto &[I, rm] : qsrmax)
    if (rm.total()) tasks.push_back(I);
  std::vector<pair<size_t, Invar>> tasks_with_sizes;
  for (const auto &I : tasks) 
    tasks_with_sizes.emplace_back(qsrmax.at(I).total(), I);
  // Sort in the *decreasing* order!
  sort(rbegin(tasks_with_sizes), rend(tasks_with_sizes));
  auto nr       = tasks_with_sizes.size();
  auto min_size = tasks_with_sizes.back().first;
  auto max_size = tasks_with_sizes.front().first;
  cout << "Stats: nr=" << nr << " min=" << min_size << " max=" << max_size << endl;
  if (logletter('S'))   // report matrix sizes
    for (const auto &[size, I] : tasks_with_sizes) 
      cout << "size(" << I << ")=" << size << endl;
  std::vector<Invar> sorted_tasks;
  transform(cbegin(tasks_with_sizes), cend(tasks_with_sizes), std::back_inserter(sorted_tasks), [](const auto &p) { return p.second; });
  return sorted_tasks;
}

// Recalculate irreducible matrix elements for Wilson chains.
void recalc_irreducible(const Step &step, const DiagInfo &diag, const QSrmax &qsrmax, Opch &opch) {
  TIME("recalc f");
  if (!P.substeps) {
    opch = Sym->recalc_irreduc(step, diag, qsrmax);
  } else {
    const auto [N, M] = step.NM();
    for (size_t i = 0; i < P.channels; i++)
      if (i == M) {
        opch[i] = Sym->recalc_irreduc_substeps(step, diag, qsrmax, i);
      } else {
        for (size_t j = 0; j < P.perchannel; j++) 
          opch[i][j] = Sym->recalc_doublet(diag, qsrmax, opch[i][j]);
      }
  }
}

void dump_f(const Opch &opch) {
  cout << endl;
  for (size_t i = 0; i < P.channels; i++)
    for (size_t j = 0; j < P.perchannel; j++) {
      cout << "<f> dump, i=" << i << " j=" << j << endl;
      dump_matrix_elements(opch[i][j]);
    }
  cout << endl;
}

// Used in evaluation of vertex functions to speed up the computation.
void calc_boltzmann_factors(DiagInfo &diag) {
  for (auto &[i, eig] : diag) {
    const auto len = eig.absenergyG.size();
    eig.boltzmann.resize(len);
    for (size_t j = 0; j < len; j++) 
      eig.boltzmann[j] = exp(-eig.absenergyG[j] / P.T);
  }
}

// NRG diagonalisation driver: calls diagionalisations() or load_transformations(), as necessary, and performs
// the truncation. All other calculations are done in after_diag(). Called from iterate().
DiagInfo do_diag(const Step &step, IterInfo &iterinfo, Stats &stats, const DiagInfo &diagprev, QSrmax &qsrmax, const Params &P) {
  step.infostring();
  Sym->show_coefficients(step);
  auto tasks = task_list(qsrmax);
  double diagratio = P.diagratio;
  DiagInfo diag;
  bool notenough;
  do {
    if (step.nrg()) {
      if (!(P.resume && int(step.ndx()) <= P.laststored))
        diag = diagonalisations(step, iterinfo.opch, diagprev, tasks, diagratio, P); // compute in first run
      else
        diag = load_transformations(step.ndx(), P); // or read from disk
    }
    if (step.dmnrg()) {
      diag = load_transformations(step.ndx(), P); // read from disk in second run
      // IMPORTANT: subtract the absolute (!) GS energy in the
      // abs_energy vector. The overall (all shells, all invariant
      // subspaces) lowest abs_energy will thus be equal to zero.
      shift_abs_energies(stats, diag);
      calc_boltzmann_factors(diag);
      if (P.removefiles) remove_transformation_files(step.ndx(), P);
    }
    stats.find_groundstate(diag);
    if (step.nrg()) // should be done only once!
      subtract_groundstate_energy(stats, diag);
    auto cluster_mapping = find_clusters(sort_energies(diag), P.fixeps);
    fix_splittings(diag, cluster_mapping);
    notenough = truncate_prepare(step, diag, P);
    if (notenough) {
      cout << "Insufficient number of states computed." << endl;
      if (P.restart) {
        diagratio = min(diagratio * P.restartfactor, 1.0);
        cout << endl
             << "Restarting this iteration step. "
             << "diagratio=" << diagratio << endl
             << endl;
      }
    }
  } while (step.nrg() && P.restart && notenough);
  return diag;
}

// Absolute energies. Must be called in the first NRG run after
// stats.total_energy has been updated, but before
// store_transformations(). These energies are initially not
// referrenced to absolute 0. This is done in the second NRG run in
// shift_abs_energies(diag).
void calc_abs_energies(const Step &step, DiagInfo &diag, const Stats &stats) {
  for (auto &[i, eig] : diag) {
    eig.absenergy = eig.value_zero;
    for (auto &x : eig.absenergy) 
      x = x * step.scale() + stats.total_energy;
    eig.absenergyG = eig.value_zero;
    for (auto &x : eig.absenergyG) 
      x = x * step.scale() + stats.total_energy;
    eig.absenergyN = eig.value_zero; // unscaled energies, referenced to the lowest energy in current NRG step (not modified later on)
    for (auto &x : eig.absenergyN) 
      x *= step.scale();
  }
}

void store_to_dm(const Step &step, const DiagInfo &diag, const QSrmax &qsrmax, AllSteps &dm)
{
  size_t nrall  = 0;
  size_t nrkept = 0;
  for (const auto &[I, eig]: diag) { // XXX - there should be only 1 copy
    const auto f = qsrmax.find(I);
    dm[step.ndx()][I] = { eig.getnr(), eig.getdim(),
                          f != qsrmax.cend() ? f->second : Rmaxvals{},
                          eig.value_zero, eig.absenergy, eig.absenergyG, eig.absenergyN, step.last() };
    nrall += eig.getdim();
    nrkept += eig.getnr();
  }
  double ratio = double(nrkept) / nrall;
  cout << "Kept: " << nrkept << " out of " << nrall << ", ratio=" << setprecision(3) << ratio << endl;
}

// Perform processing after a successful NRG step. Also called from doZBW() as a final step.
void after_diag(const Step &step, IterInfo &iterinfo, Stats &stats, DiagInfo &diag, Output &output, QSrmax &qsrmax, AllSteps &dm, Oprecalc &oprecalc) {
  // XXX: move find_groundstate & subtraction here!
  stats.total_energy += stats.Egs * step.scale(); // stats.Egs has already been initialized
  cout << "Total energy=" << HIGHPREC(stats.total_energy) << "  Egs=" << HIGHPREC(stats.Egs) << endl;
  stats.rel_Egs[step.ndx()] = stats.Egs;
  stats.abs_Egs[step.ndx()] = stats.Egs * step.scale();
  stats.energy_offsets[step.ndx()] = stats.total_energy;
  if (step.nrg()) 
    calc_abs_energies(step, diag, stats);
  if (step.nrg() && P.dm && !(P.resume && int(step.ndx()) <= P.laststored))
    save_transformations(step.ndx(), diag, P);
  perform_measurements(step, diag, stats, output); // Measurements are performed before the truncation!
  if (!P.ZBW)
    split_in_blocks(diag, qsrmax);
  if (do_recalc_all(step, P)) { // Either ...
    oprecalc.recalculate_operators(step, diag, qsrmax, iterinfo, P);
    calculate_spectral_and_expv(step, stats, output, diag, iterinfo, dm, P);
  }
  if (!P.ZBW) 
    truncate_perform(diag);            // Actual truncation occurs at this point
  store_to_dm(step, diag, qsrmax, dm); // Store information about subspaces and states for DM algorithms
  if (!step.last()) {
    recalc_irreducible(step, diag, qsrmax, iterinfo.opch);
    if (P.dump_f) dump_f(iterinfo.opch);
  }
  if (do_recalc_kept(step, P)) { // ... or ...
    oprecalc.recalculate_operators(step, diag, qsrmax, iterinfo, P);
    calculate_spectral_and_expv(step, stats, output, diag, iterinfo, dm, P);
  }
  if (do_no_recalc(step, P)) { // ... or this
    calculate_spectral_and_expv(step, stats, output, diag, iterinfo, dm, P);
  }
  if (P.checksumrules) operator_sumrules(diag, iterinfo);
}

// Perform one iteration step
DiagInfo iterate(const Step &step, IterInfo &iterinfo, Stats &stats, const DiagInfo &diagprev, Output &output, AllSteps &dm, Oprecalc &oprecalc, const Params &P) {
  QSrmax qsrmax = get_qsrmax(diagprev);
  auto diag = do_diag(step, iterinfo, stats, diagprev, qsrmax, P);
  after_diag(step, iterinfo, stats, diag, output, qsrmax, dm, oprecalc);
  trim_matrices(diag, iterinfo);
  clear_eigenvectors(diag);
  time_mem::memory_time_brief_report();
  return diag;
}

void docalc0ht(Step &step, const DiagInfo &diag0, Stats &stats, Output &output, unsigned int extra_steps, const Params &P) {
  for (int i = -int(extra_steps); i <= -1; i++) {
    step.set(P.Ninit - 1 + i);
    double E_rescale_factor = pow(P.Lambda, i / 2.0); // NOLINT
    calculate_TD(step, diag0, stats, output, E_rescale_factor);
  }
}

// Perform calculations with quantities from 'data' file
void docalc0(Step &step, const IterInfo &iterinfo, const DiagInfo &diag0, Stats &stats, Output &output, const Params &P) {
  step.set(P.Ninit - 1); // in the usual case with Ninit=0, this will result in N=-1
  cout << endl << "Before NRG iteration";
  cout << " (N=" << step.N() << ")" << endl;
  perform_measurements(step, diag0, stats, output);
  AllSteps empty_dm{};
  calculate_spectral_and_expv(step, stats, output, diag0, iterinfo, empty_dm, P);
  if (P.checksumrules) operator_sumrules(diag0, iterinfo);
}

// doZBW() takes the place of iterate() called from main_loop() in the case of zero-bandwidth calculation.
// It replaces do_diag() and calls after_diag() as the last step.
DiagInfo nrg_ZBW(Step &step, IterInfo &iterinfo, Stats &stats, const DiagInfo &diag0, Output &output, AllSteps &dm, Oprecalc &oprecalc, const Params &P) {
  cout << endl << "Zero bandwidth calculation" << endl;
  step.set_ZBW();
  // --- begin do_diag() equivalent
  DiagInfo diag;
  if (step.nrg()) 
    diag = diag0;
  if (step.dmnrg()) {
    diag = load_transformations(step.ndx(), P);
    shift_abs_energies(stats, diag);
    calc_boltzmann_factors(diag); // !!
    if (P.removefiles) remove_transformation_files(step.ndx(), P);
  }
  stats.find_groundstate(diag);
  if (step.nrg())      
    subtract_groundstate_energy(stats, diag);
  truncate_prepare(step, diag, P); // determine # of kept and discarded states
  // --- end do_diag() equivalent
  QSrmax empty_qsrmax{};
  after_diag(step, iterinfo, stats, diag, output, empty_qsrmax, dm, oprecalc);
  return diag;
}

// ****************************  Main NRG loop ****************************

DiagInfo nrg_loop(Step &step, IterInfo &iterinfo, Stats &stats, const DiagInfo &diag0, Output &output, AllSteps &dm, Oprecalc &oprecalc, const Params &P) {
  DiagInfo diag = diag0;
  for (step.init(); !step.end(); step++)
    diag = iterate(step, iterinfo, stats, diag, output, dm, oprecalc, P);
  step.set(step.lastndx()); // TO DO: remove this, after step is no longer global...
  return diag;
}

// Total number of states (symmetry taken into account)
size_t count_states(const DiagInfo &diag) {
  size_t states = 0;
  for (const auto &[i, eig]: diag) 
    states += Sym->mult(i) * eig.getnr();
  return states;
}

// Count non-empty subspaces
size_t count_subspaces(const DiagInfo &diag) {
  size_t subspaces = 0;
  for (const auto &[i, eig]: diag) 
    if (eig.getnr()) 
      subspaces++;
  return subspaces;
}

// Dump information about states (e.g. before the start of the iteration).
void states_report(const DiagInfo &diag, ostream &fout = cout) {
  fout << "Number of invariant subspaces: " << count_subspaces(diag) << endl;
  for (const auto &[i, eig]: diag) 
    if (eig.getnr()) 
      fout << "(" << i << ") " << eig.getnr() << " states: " << eig.value_orig << endl;
  fout << "Number of states (multiplicity taken into account): " << count_states(diag) << endl << endl;
}

// Save a dump of all subspaces, with dimension info, etc.
void dump_subspaces(const AllSteps &dm, const Params &P) {
  ofstream O(FN_SUBSPACES);
  for (size_t N = P.Ninit; N < P.Nmax; N++) {
    O << "Iteration " << N << endl;
    O << "len_dm=" << dm[N].size() << endl;
    for (const auto &[I, DS] : dm[N])
      O << "I=" << I << " len=" << DS.eigenvalue.size() << " kept=" << DS.kept << " total=" << DS.total << endl;
    O << endl;
  }
}

DiagInfo run_nrg(Step &step, IterInfo &iterinfo, Stats &stats, const DiagInfo &diag0, AllSteps &dm, const Params &P) {
  states_report(diag0);
  auto oprecalc = Oprecalc(step.runtype, iterinfo, P);
  auto output = Output(step.runtype, iterinfo, stats, P);
  // If calc0=true, a calculation of TD quantities is performed before starting the NRG iteration.
  if (step.nrg() && P.calc0 && !P.ZBW) {
    docalc0ht(step, diag0, stats, output, P.tdht, P);
    docalc0(step, iterinfo, diag0, stats, output, P);
  }
  DiagInfo diag = P.ZBW ? nrg_ZBW(step, iterinfo, stats, diag0, output, dm, oprecalc, P) : nrg_loop(step, iterinfo, stats, diag0, output, dm, oprecalc, P);
  cout << endl << "Total energy: " << HIGHPREC(stats.total_energy) << endl;
  stats.GS_energy = stats.total_energy;
  if (P.dumpsubspaces) dump_subspaces(dm, P);
  cout << endl << "** Iteration completed." << endl << endl;
  return diag;
}

/************************ MAIN ****************************************/

void print_about_message(ostream &s) {
  s << "NRG Ljubljana - (c) rok.zitko@ijs.si" << endl;
  s << "Timestamp: " << __TIMESTAMP__ << endl;
  s << "Compiled on " << __DATE__ << " at " << __TIME__ << endl << endl;
}

std::unique_ptr<Symmetry> get(std::string sym_string, const Params &P, Allfields &allfields)
{
  if (sym_string == "QS")
    return std::make_unique<SymmetryQS>(P, allfields);
  if (sym_string == "QSZ")
    return std::make_unique<SymmetryQSZ>(P, allfields);  
  throw std::runtime_error("Unknown symmetry " + sym_string);
}

// Called immediately after parsing the information about the number of channels from the data file. This ensures
// that Invar can be parsed correctly.
void set_symmetry(const Params &P, Stats &stats) {
  my_assert(P.channels > 0 && P.combs > 0); // must be set at this point
  cout << "SYMMETRY TYPE: " << P.symtype.value() << endl;
  Sym = get(P.symtype.value(), P, stats.td.allfields);
  Sym->load();
}

void calculation(Params &P) {
  Stats stats(P);
  auto [diag0, iterinfo] = read_data(P, stats);
  Step step{P, RUNTYPE::NRG};
  AllSteps dm(P.Nlen);
  auto diag = run_nrg(step, iterinfo, stats, diag0, dm, P);
  if (string(P.stopafter) == "nrg") exit1("*** Stopped after the first sweep.");
  if (P.dm) {
    if (need_rho()) {
      auto rho = init_rho(step, diag);
      saveRho(step.lastndx(), FN_RHO, rho, P);
      if (!P.ZBW) calc_densitymatrix(rho, dm, P);
    }
    if (need_rhoFDM()) {
      shift_abs_energies_dm(stats, dm);
      calc_ZnD(dm, stats);
      fdm_thermodynamics(dm, stats);
      auto rhoFDM = init_rho_FDM(step.lastndx(), dm, stats, P);
      saveRho(step.lastndx(), FN_RHOFDM, rhoFDM, P);
      if (!P.ZBW) calc_fulldensitymatrix(step, rhoFDM, dm, stats, P);
    }
    if (string(P.stopafter) == "rho") exit1("*** Stopped after the DM calculation.");
    auto [diag0_dm, iterinfo_dm] = read_data(P, stats);
    Step step{P, RUNTYPE::DMNRG};
    run_nrg(step, iterinfo_dm, stats, diag0_dm, dm, P);
    my_assert(num_equal(stats.GS_energy, stats.total_energy));
  }
}

#ifdef NRG_MPI
void mpi_sync_params() {
  // Synchronize global parameters
  if (mpiw->rank() == 0)
    for (size_t i = 1; i < mpiw->size(); i++) mpiw->send(i, TAG_SYNC, 0);
  mpi::broadcast(*mpiw, sP, 0);
}
#endif

// Master process does most of the i/o and passes calculations to the slaves.
void run_nrg_master() {
  P.read_parameters(workdir);
  sP.init(P);
  calculation(P);
#ifdef NRG_MPI
  cout << "Master done. Terminating slave processes." << endl;
  for (int i = 1; i < mpiw->size(); i++) mpiw->send(i, TAG_EXIT, 0);
  cout << "Master exiting." << endl;
#endif
  if (P.done) { ofstream D("DONE"); } // Indicate completion by creating a flag file
}

#ifdef NRG_MPI
// Handle a diagonalisation request:
void slave_diag(const int master) {
  // 1. receive the matrix and the subspace identification
  auto m = mpi_receive_matrix(master);
  Invar I;
  check_status(mpiw->recv(master, TAG_INVAR, I));
  // 2. preform the diagonalisation
  Eigen eig = diagonalise(m);
  // 3. send back the results
  mpi_send_eigen(master, eig);
  mpiw->send(master, TAG_INVAR, I);
}

void run_nrg_slave() {
  constexpr auto master = 0;
  for (;;) {
    if (mpiw->iprobe(master, mpi::any_tag)) { // message can be received.
      int task;
      auto status = mpiw->recv(master, mpi::any_tag, task);
      check_status(status);
      nrglog('M', "Slave " << mpiw->rank() << " received message with tag " << status.tag());
      switch (status.tag()) {
        case TAG_SYNC:
          mpi_sync_params();
          break;
        case TAG_DIAG:
          slave_diag(master);
          break;
        case TAG_EXIT:
          return; // exit from run_slave()
        default: 
          cout << "MPI error: unknown tag on " << mpiw->rank() << endl; 
          break;
      }
    } else usleep(100); // sleep to reduce the load on the computer. (OpenMPI "feature" workaround)
  }
}
#else
void run_nrg_slave() {}
#endif
