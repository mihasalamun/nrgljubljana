/*
 "NRG Ljubljana" - Numerical renormalization group for multiple
 impurities and an arbitrary number of channels

 Copyright (C) 2005-2020 Rok Zitko

   This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
   details.

   You should have received a copy of the GNU General Public License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

   Contact information:
   Rok Zitko
   F1 - Theoretical physics
   "Jozef Stefan" Institute
   Jamova 39
   SI-1000 Ljubljana
   Slovenia

   rok.zitko@ijs.si
*/

#ifndef _nrg_general_h_
#define _nrg_general_h_

#include <utility>
#include <functional>
#include <iterator>
#include <algorithm>
#include <complex>
#include <numeric>
#include <limits>
#include <memory>
#include <string>
using namespace std::string_literals;
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <unordered_map>
#include <list>
#include <deque>
#include <set>
#include <stdexcept>

// C headers
#include <cassert>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstring>
#include <unistd.h>

#include <boost/range/irange.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/math/special_functions/sign.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/io/ios_state.hpp>
#include <boost/optional.hpp>

// ublas matrix & vector containers
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/vector_proxy.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/matrix_proxy.hpp>
#include <boost/numeric/ublas/symmetric.hpp>
#include <boost/numeric/ublas/operation.hpp>
using namespace boost::numeric;
using namespace boost::numeric::ublas; // keep this!

// Numeric bindings to BLAS/LAPACK
#include <boost/numeric/bindings/traits/ublas_vector.hpp>
#include <boost/numeric/bindings/traits/ublas_matrix.hpp>
#include <boost/numeric/bindings/atlas/cblas.hpp>
namespace atlas = boost::numeric::bindings::atlas;

// Serialization support (used for storing to files and for MPI)
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/complex.hpp>

// MPI support
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi/collectives.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/color.h>
#include <fmt/ranges.h>
using namespace fmt::literals;

#include <range/v3/all.hpp>

// Support for compiler dependant optimizations

#ifdef __GNUC__
#define PUREFNC __attribute__((pure))
#define CONSTFNC __attribute__((const))
#else
#define PUREFNC
#define CONSTFNC
#endif

#include "nrg-general.h"
#include "nrg-lib.h" // exposed interfaces for wrapping into a library
#include "portabil.h"
#include "debug.h"
#include "misc.h"
#include "openmp.h"
#include "mp.h"
#include "traits.h"
#include "workdir.h"
#include "params.h"
#include "numerics.h"
#include "io.h"
#include "time_mem.h"
#include "outfield.h"

// This is included in the library only. Should not be used if a cblas library is available.
#ifdef CBLAS_WORKAROUND
 #define ADD_
 #include "cblas_globals.c"
 #include "cblas_dgemm.c"
 #include "cblas_zgemm.c"
 #include "cblas_xerbla.c"
#endif

inline const size_t MAX_NDX = 1000; // max index number
inline const double WEIGHT_TOL = 1e-8; // where to switch to l'Hospital rule form

#include "invar.h"

// Result of a diagonalisation: eigenvalues and eigenvectorse
template <typename S> class Eigen {
public:
  using t_eigen = typename traits<S>::t_eigen;
  using Matrix = typename traits<S>::Matrix;
  using EVEC = ublas::vector<t_eigen>;
  EVEC value_orig;               // eigenvalues as computed
  Matrix matrix; // eigenvectors
  Eigen() {}
  Eigen(const size_t nr, const size_t dim) {
    my_assert(nr <= dim);
    value_orig.resize(nr);
    matrix.resize(nr, dim);
  }
  auto getnrcomputed() const { return value_orig.size(); } // number of computed eigenpairs
  auto getdim() const { return matrix.size2(); }           // valid also after the split_in_blocks_Eigen() call
  // Now add information about truncation and block structure of the eigenvectors
 private:
  long nrpost = -1;  // number of eigenpairs after truncation (-1: keep all)
 public:
  EVEC value_zero;   // eigenvalues with Egs subtracted
  auto getnrpost() const { return nrpost == -1 ? getnrcomputed() : nrpost; }
  auto getnrstored() const  { return value_zero.size(); }                   // number of stored states
  auto getnrall() const { return getnrcomputed(); }                         // all = all computed
  auto getnrkept() const { return getnrpost(); }
  auto getnrdiscarded() const { return getnrcomputed()-getnrpost(); }
  auto all() const { return range0(getnrcomputed()); }                           // iterator over all states
  auto kept() const { return range0(getnrpost()); }                              // iterator over kept states
  auto discarded() const { return boost::irange(getnrpost(), getnrcomputed()); } // iterator over discarded states
  auto stored() const { return range0(getnrstored()); }                          // iterator over all stored states
  // NOTE: "absolute" energy means that it is expressed in the absolute energy scale rather than SCALE(N).
  EVEC absenergy;      // absolute energies
  EVEC absenergyG;     // absolute energies (0 is the absolute ground state of the system) [SAVED TO FILE]
  EVEC absenergyN;     // absolute energies (referenced to the lowest energy in the N-th step)
  // 'blocks' contains eigenvectors separated according to the invariant
  // subspace from which they originate. This separation is required for
  // using the efficient BLAS routines when performing recalculations of
  // the matrix elements.
  std::vector<Matrix> blocks;
  // Truncate to nrpost states.
  void truncate_prepare_subspace(const size_t nrpost_) {
    nrpost = nrpost_;
    my_assert(nrpost <= getnrstored());
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
  }
  void subtract_Egs(const double Egs) {
    value_zero = value_orig;
    for (auto &x : value_zero) x -= Egs;
    my_assert(value_zero[0] >= 0);
  }
  void subtract_GS_energy(const double GS_energy) {
    for (auto &x : absenergyG) x -= GS_energy;
    my_assert(absenergyG[0] >= 0);
  }
  auto diagonal_exp(const double factor) const { // produce a diagonal matrix with exp(-factor*E) diagonal elements
    const auto dim = getnrstored();
    typename traits<S>::Matrix m(dim, dim, 0);
    for (const auto i: range0(dim)) 
      m(i, i) = exp(-value_zero(i) * factor);
    return m;
  }
  void save(boost::archive::binary_oarchive &oa) const {
    // RawEigen
    oa << value_orig;
    ::save(oa, matrix);
    // Eigen
    oa << value_zero << nrpost << absenergy << absenergyG << absenergyN;
  }  
  void load(boost::archive::binary_iarchive &ia) {
    // RawEigen
    ia >> value_orig;
    ::load(ia, matrix);
    // Eigen
    ia >> value_zero >> nrpost >> absenergy >> absenergyG >> absenergyN;
  } 
};

// Full information after diagonalizations (eigenspectra in all subspaces)
template<typename S>
class DiagInfo : public std::map<Invar, Eigen<S>> {
 public:
   using t_eigen = typename traits<S>::t_eigen;
   using Matrix  = typename traits<S>::Matrix;
   explicit DiagInfo() {}
   DiagInfo(std::ifstream &fdata, const size_t nsubs, const Params &P) {
     for (const auto i : range1(nsubs)) {
       Invar I;
       fdata >> I;
       auto energies = read_vector<double>(fdata);
       if (!P.data_has_rescaled_energies && !P.absolute)
         energies /= P.SCALE(P.Ninit); // rescale to the suitable energy scale
       (*this)[I].diagonal(energies);
     }
     my_assert(this->size() == nsubs);
   }
   auto subspaces() const { return *this | boost::adaptors::map_keys; }
   auto eigs() const { return *this | boost::adaptors::map_values; }
   auto eigs() { return *this | boost::adaptors::map_values; }
   auto find_groundstate() const {
     const auto [Iground, eig] = *ranges::min_element(*this, [](const auto a, const auto b) { return a.second.value_orig(0) < b.second.value_orig(0); });
     const auto Egs = eig.value_orig(0);
     return Egs;
   }
   void subtract_Egs(const t_eigen Egs) {
     ranges::for_each(this->eigs(), [Egs](auto &eig)       { eig.subtract_Egs(Egs); });
   }
   void subtract_GS_energy(const t_eigen GS_energy) {
     ranges::for_each(this->eigs(), [GS_energy](auto &eig) { eig.subtract_GS_energy(GS_energy); });
   }
   std::vector<t_eigen> sorted_energies() const {
     std::vector<t_eigen> energies;
     for (const auto &eig: this->eigs())
       energies.insert(energies.end(), eig.value_zero.begin(), eig.value_zero.end());
     return energies | ranges::move | ranges::actions::sort;
   }
   void dump_value_zero(std::ostream &F) const {
     for (const auto &[I, eig]: *this)
       F << "Subspace: " << I << std::endl << eig.value_zero << std::endl;
   }
   void truncate_perform() {
     for (auto &[I, eig] : *this) eig.truncate_perform(); // Truncate subspace to appropriate size
   }
   size_t size_subspace(const Invar &I) const {
     const auto f = this->find(I);
     return f != this->cend() ? f->second.getnrstored() : 0;
   }
   void clear_eigenvectors() {
     for (auto &eig : this->eigs())
       for (auto &m : eig.blocks) 
         m = Matrix(0, 0);
   }
   // Total number of states (symmetry taken into account)
   template <typename MF> auto count_states(MF && mult) const {
     return ranges::accumulate(*this, 0, [mult](auto n, const auto &x) { const auto &[I, eig] = x; return n + mult(I)*eig.getnrstored(); });
   }
   auto count_subspaces() const {    // Count non-empty subspaces
     return ranges::count_if(this->eigs(), [](const auto &eig) { return eig.getnrstored()>0; });
   }
   template<typename F, typename M> auto trace(F fnc, const double factor, M mult) const { // Tr[fnc exp(-factor*E)]
     auto b = 0.0;
     for (const auto &[I, eig] : *this)
       b += mult(I) * ranges::accumulate(eig.value_zero, 0.0, [fnc, factor](auto acc, const auto x) { 
         const auto betaE = factor * x; return acc + fnc(betaE) * exp(-betaE); });
     return b;
   }
   template <typename MF>
     void states_report(MF && mult) const {
       fmt::print("Number of invariant subspaces: {}\n", count_subspaces());
       for (const auto &[I, eig]: *this) 
         if (eig.getnrstored()) 
           fmt::print("({}) {} states: {}\n", I.str(), eig.getnrstored(), eig.value_orig);
       fmt::print("Number of states (multiplicity taken into account): {}\n\n", count_states(mult));
     }
   void save(const size_t N, const Params &P) const {
     const std::string fn = P.workdir.unitaryfn(N);
     std::ofstream MATRIXF(fn, std::ios::binary | std::ios::out);
     if (!MATRIXF) throw std::runtime_error(fmt::format("Can't open file {} for writing.", fn));
     boost::archive::binary_oarchive oa(MATRIXF);
     oa << this->size();
     for(const auto &[I, eig]: *this) {
       oa << I;
       eig.save(oa);
       if (MATRIXF.bad()) throw std::runtime_error(fmt::format("Error writing {}", fn)); // Check after each write.
     }
   }
   void load(const size_t N, const Params &P, const bool remove_files = false) {
     const std::string fn = P.workdir.unitaryfn(N);
     std::ifstream MATRIXF(fn, std::ios::binary | std::ios::in);
     if (!MATRIXF) throw std::runtime_error(fmt::format("Can't open file {} for reading", fn));
     boost::archive::binary_iarchive ia(MATRIXF);
     size_t nr; // Number of subspaces
     ia >> nr;
     for (const auto cnt : range0(nr)) {
       Invar inv;
       ia >> inv;
       (*this)[inv].load(ia);
       if (MATRIXF.bad()) throw std::runtime_error(fmt::format("Error reading {}", fn));
     }
     if (remove_files) remove(fn);
   }
   explicit DiagInfo(const size_t N, const Params &P, const bool remove_files = false) { load(N, P, remove_files); } // called from do_diag()
};

template<typename S>
class MatrixElements : public std::map<Twoinvar, typename traits<S>::Matrix> {
 public:
   MatrixElements() {}
   MatrixElements(std::ifstream &fdata, const DiagInfo<S> &diag) {
     size_t nf; // Number of I1 x I2 combinations
     fdata >> nf;
     for (const auto i : range0(nf)) {
       Invar I1, I2;
       fdata >> I1 >> I2;
       if (const auto it1 = diag.find(I1), it2 = diag.find(I2); it1 != diag.end() && it2 != diag.end())
         read_matrix(fdata, (*this)[{I1, I2}], it1->second.getnrstored(), it2->second.getnrstored());
       else
         throw std::runtime_error("Corrupted input file.");
     }
     my_assert(this->size() == nf);
   }
   // We trim the matrices containing the irreducible matrix elements of the operators to the sizes that are actually
   // required in the next iterations. This saves memory and leads to better cache usage in recalc_general()
   // recalculations. Note: this is only needed for strategy=all; copying is avoided for strategy=kept.
   void trim(const DiagInfo<S> &diag) {
     for (auto &[II, mat] : *this) {
       const auto &[I1, I2] = II;
       // Current matrix dimensions
       const auto size1 = mat.size1();
       const auto size2 = mat.size2();
       if (size1 == 0 || size2 == 0) continue;
       // Target matrix dimensions
       const auto nr1 = diag.at(I1).getnrstored();
       const auto nr2 = diag.at(I2).getnrstored();
       my_assert(nr1 <= size1 && nr2 <= size2);
       if (nr1 == size1 && nr2 == size2) // Trimming not necessary!!
         continue;
       ublas::matrix_range<typename traits<S>::Matrix> m2(mat, ublas::range(0, nr1), ublas::range(0, nr2));
       typename traits<S>::Matrix m2new = m2;
       mat.swap(m2new);
     }
   }
   std::ostream &insertor(std::ostream &os) const { 
     for (const auto &[II, mat] : *this)
       os << "----" << II << "----" << std::endl << mat << std::endl;
     return os;
   }
   friend std::ostream &operator<<(std::ostream &os, const MatrixElements<S> &m) { return m.insertor(os); }
   friend void dump_diagonal_op(const std::string &name, const MatrixElements<S> &m, const size_t max_nr, std::ostream &F) {
     F << "Diagonal matrix elements of operator " << name << std::endl;
     for (const auto &[II, mat] : m) {
       const auto & [I1, I2] = II;
       if (I1 == I2) {
         F << I1 << ": ";
         dump_diagonal_matrix(mat, max_nr, F);
       }
     }
   }
};

template<typename S>
class DensMatElements : public std::map<Invar, typename traits<S>::Matrix> {
 public:
   template <typename MF>
     auto trace(MF mult) const {
       return ranges::accumulate(*this, 0.0, [mult](double acc, const auto z) { const auto &[I, mat] = z; 
         return acc + mult(I) * trace_real(mat); });
     }
   void save(const size_t N, const Params &P, const std::string &prefix) const {
     const auto fn = P.workdir.rhofn(N, prefix);
     std::ofstream MATRIXF(fn, std::ios::binary | std::ios::out);
     if (!MATRIXF) throw std::runtime_error(fmt::format("Can't open file {} for writing.", fn));
     boost::archive::binary_oarchive oa(MATRIXF);
     oa << this->size();
     for (const auto &[I, mat] : *this) {
       oa << I;
       ::save(oa, mat);
       if (MATRIXF.bad()) throw std::runtime_error(fmt::format("Error writing {}", fn));  // Check each time
     }
     MATRIXF.close();
   }
   void load(const size_t N, const Params &P, const std::string &prefix, const bool remove_files) {
     const auto fn = P.workdir.rhofn(N, prefix);
     std::ifstream MATRIXF(fn, std::ios::binary | std::ios::in);
     if (!MATRIXF) throw std::runtime_error(fmt::format("Can't open file {} for reading", fn));
     boost::archive::binary_iarchive ia(MATRIXF);
     size_t nr;
     ia >> nr;
     for (const auto cnt : range0(nr)) {
       Invar inv;
       ia >> inv;
       ::load(ia, (*this)[inv]);
       if (MATRIXF.bad()) throw std::runtime_error(fmt::format("Error reading {}", fn));  // Check each time
     }
     MATRIXF.close();
     if (remove_files)
       if (remove(fn)) throw std::runtime_error(fmt::format("Error removing {}", fn));
   }
};

// Map of operator matrices
template<typename S>
struct CustomOp : public std::map<std::string, MatrixElements<S>> {
   void trim(const DiagInfo<S> &diag) {
     for (auto &op : *this | boost::adaptors::map_values) op.trim(diag);
   }
};

// Vector containing irreducible matrix elements of f operators.
template<typename S>
using OpchChannel = std::vector<MatrixElements<S>>;

// Each channel contains P.perchannel OpchChannel matrices.
template<typename S>
class Opch : public std::vector<OpchChannel<S>> {
 public:
   Opch() {}
   explicit Opch(const size_t nrch) { this->resize(nrch); }
   Opch(std::ifstream &fdata, const DiagInfo<S> &diag, const Params &P) {
     this->resize(P.channels);
     for (const auto i : range0(size_t(P.channels))) {
       (*this)[i] = OpchChannel<S>(P.perchannel);
       for (const auto j : range0(size_t(P.perchannel))) {
         char ch;
         size_t iread, jread;
         fdata >> ch >> iread >> jread;
         my_assert(ch == 'f' && i == iread && j == jread);
         (*this)[i][j] = MatrixElements<S>(fdata, diag);
       }
     }
   }
   void dump() {
     std::cout << std::endl;
     for (const auto &&[i, ch] : *this | ranges::views::enumerate)
       for (const auto &&[j, mat] : ch | ranges::views::enumerate)
         std::cout << fmt::format("<f> dump, i={} j={}\n", i, j) << mat << std::endl;
     std::cout << std::endl;
   }
};

template<typename S> class Symmetry;

// Dimensions of the invariant subspaces |r,1>, |r,2>, |r,3>, etc. The name "rmax" comes from the maximal value of
// the index "r" which ranges from 1 through rmax.

class Rmaxvals {
 private:
   std::vector<size_t> values;
 public:
   Rmaxvals() = default;
   template<typename S>
     Rmaxvals(const Invar &I, const InvarVec &In, const DiagInfo<S> &diagprev, std::shared_ptr<Symmetry<S>> Sym);
   auto combs() const { return values.size(); }
   auto rmax(const size_t i) const {
     my_assert(i < combs());
     return values[i];
   }
   auto exists(const size_t i) const {
     my_assert(i < combs());
     return values[i] > 0; 
   }
   auto offset(const size_t i) const {
     my_assert(i < combs());
     return ranges::accumulate(std::begin(values), std::begin(values) + i, size_t{0});
   }
   auto operator[](const size_t i) const { return rmax(i); }
   auto total() const { return ranges::accumulate(values, 0); } // total number of states
   // *** Mathematica interfacing: i1,j1 are 1-based
   bool offdiag_contributes(const size_t i1, const size_t j1) const { // i,j are 1-based (Mathematica interface)
     my_assert(1 <= i1 && i1 <= combs() && 1 <= j1 && j1 <= combs());
     my_assert(i1 != j1);
     return exists(i1-1) && exists(j1-1); // shift by 1
   }
   auto chunk(const size_t i1) const {
     return std::make_pair(offset(i1-1), rmax(i1-1));
   }
 private:
   friend std::ostream &operator<<(std::ostream &os, const Rmaxvals &rmax) {
     for (const auto &x : rmax.values) os << x << ' ';
     return os;
   }
   template <class Archive> void serialize(Archive &ar, const unsigned int version) { ar &values; }
   friend class boost::serialization::access;
};

class QSrmax : public std::map<Invar, Rmaxvals> {
 public:
   QSrmax() {}
   template<typename S> QSrmax(const DiagInfo<S> &, std::shared_ptr<Symmetry<S>>);
   // List of invariant subspaces in which diagonalisations need to be performed
   std::vector<Invar> task_list() const {
     std::vector<std::pair<size_t, Invar>> tasks_with_sizes;
     for (const auto &[I, rm] : *this)
       if (rm.total())
         tasks_with_sizes.emplace_back(rm.total(), I);
     ranges::sort(tasks_with_sizes, std::greater<>()); // sort in the *decreasing* order!
     auto nr       = tasks_with_sizes.size();
     auto min_size = tasks_with_sizes.back().first;
     auto max_size = tasks_with_sizes.front().first;
     std::cout << "Stats: nr=" << nr << " min=" << min_size << " max=" << max_size << std::endl;
     return tasks_with_sizes | ranges::views::transform( [](const auto &p) { return p.second; } ) | ranges::to<std::vector>();
   }
   void dump() const {
     for(const auto &[I, rm]: *this)
       std::cout << "rmaxvals(" << I << ")=" << rm << " total=" << rm.total() << std::endl;
   }
   auto at_or_null(const Invar &I) const {
     const auto i = this->find(I);
     return i == this->cend() ? Rmaxvals() : i->second;
   }
};

// Information about the number of states, kept and discarded, rmax, and eigenenergies. Required for the
// density-matrix construction.
template<typename S> 
struct DimSub {
  size_t kept  = 0;
  size_t total = 0;
  Rmaxvals rmax;
  Eigen<S> eig;
  bool is_last = false;
  auto min() const { return is_last ? 0 : kept; } // min(), max() return the range of D states to be summed over in FDM
  auto max() const { return total; }
  auto all() const { return boost::irange(min(), max()); }
};

// Full information about the number of states and matrix dimensions
// Example: dm[N].rmax[I] etc.
template<typename S>
using Subs = std::map<Invar, DimSub<S>>;

template<typename S>
class AllSteps : public std::vector<Subs<S>> {
 public:
   const size_t Nbegin, Nend; // range of valid indexes
   AllSteps(const size_t Nbegin, const size_t Nend) : Nbegin(Nbegin), Nend(Nend) { this->resize(Nend ? Nend : 1); } // at least 1 for ZBW
   auto Nall() const { return boost::irange(Nbegin, Nend); }
   void dump_absenergyG(std::ostream &F) const {
     for (const auto N : Nall()) {
       F << std::endl << "===== Iteration number: " << N << std::endl;
       for (const auto &[I, ds]: this->at(N))
         F << "Subspace: " << I << std::endl << ds.eig.absenergyG << std::endl;
     }
   }
   void dump_all_absolute_energies(std::string filename = "absolute_energies.dat"s) {
     std::ofstream F(filename);
     this->dump_absenergyG(F);
   }
   // Save a dump of all subspaces, with dimension info, etc.
   void dump_subspaces(const std::string filename = "subspaces.dat"s) const {
     std::ofstream O(filename);
     for (const auto N : Nall()) {
       O << "Iteration " << N << std::endl;
       O << "len_dm=" << this->at(N).size() << std::endl;
       for (const auto &[I, DS] : this->at(N))
         O << "I=" << I << " kept=" << DS.kept << " total=" << DS.total << std::endl;
       O << std::endl;
     }
   }
   void shift_abs_energies(const double GS_energy) {
     for (const auto N : Nall())
       for (auto &ds : this->at(N) | boost::adaptors::map_values)
         ds.eig.subtract_GS_energy(GS_energy);
   }
   void store(const size_t ndx, const DiagInfo<S> &diag, const QSrmax &qsrmax, const bool last) {
     my_assert(Nbegin <= ndx && ndx < Nend);
     for (const auto &[I, eig]: diag)
       (*this)[ndx][I] = { eig.getnrkept(), eig.getdim(), qsrmax.at_or_null(I), eig, last };
   }
};

class Step {
 private:
   // N denotes the order of the Hamiltonian. N=0 corresponds to H_0, i.e. the initial Hamiltonian
   int trueN; // "true N", sets the energy scale, it may be negative, trueN <= ndxN
   size_t ndxN; // "index N", iteration step, used as an array index, ndxN >= 0
   const Params &P; // reference to parameters (beta, T)
   
 public:
   const RUNTYPE runtype; // NRG vs. DM-NRG run
   void set(const int newN) {
     trueN = newN;
     ndxN = std::max(newN, 0);
   }
   void init() { set(P.Ninit); }
   Step(const Params &P_, const RUNTYPE runtype_) : P(P_), runtype(runtype_) { init(); }
   void next() { trueN++; ndxN++; }
   size_t N() const { return ndxN; }
   size_t ndx() const { return ndxN; }
   double energyscale() const { return P.SCALE(trueN+1); } // current energy scale in units of bandwidth D
   double scale() const { // scale factor as used in the calculation
     return P.absolute ? 1.0 : energyscale();
   }
   double unscale() const { // 'unscale' parameter for dimensionless quantities
     return P.absolute ? energyscale() : 1.0;
   }
   double Teff() const { return energyscale()/P.betabar; }  // effective temperature for thermodynamic calculations
   double TD_factor() const { return P.betabar / unscale(); }
   double scT() const { return scale()/P.T; } // scT = scale*P.T, scaled physical temperature that appears in the exponents in spectral function calculations (Boltzmann weights)
   std::pair<size_t, size_t> NM() const {
     const size_t N = ndxN / P.channels;
     const size_t M = ndxN - N*P.channels; // M ranges 0..channels-1
     return {N, M};
   }
   void infostring() const {
     auto info = fmt::format(" ***** [{}] Iteration {}/{} (scale {}) ***** ", runtype == RUNTYPE::NRG ? "NRG"s : "DM"s, 
                             ndxN+1, int(P.Nmax), energyscale());
     info += P.substeps ? fmt::format(" step {} substep {}", NM().first+1, NM().second+1) : "";
     fmt::print(fmt::emphasis::bold, "\n{}\n", info);
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
   // is the value that we use in building the matrix.
   int getnn() const { return ndxN; }
};

// Namespace for storing various statistical quantities calculated during iteration.
template<typename S>
class Stats {
 public:
   using t_eigen = typename traits<S>::t_eigen;
   using t_expv  = typename traits<S>::t_expv;
   t_eigen Egs{};
   
   // ** Thermodynamic quantities
   double Z{};
   double Zft{};   // grand-canonical partition function (at shell n)
   double Zgt{};   // grand-canonical partition function for computing G(T)
   double Zchit{}; // grand-canonical partition function for computing chi(T)
   TD td;
   
   //  ** Expectation values
   std::map<std::string, t_expv> expv;    // expectation values of custom operators
   std::map<std::string, t_expv> fdmexpv; // Expectation values computed using the FDM algorithm
   
   // ** Energies
   // "total_energy" is the total energy of the ground state at the current iteration. This is the sum of all the 
   // zero state energies (eigenvalue shifts converted to absolute energies) for all the iteration steps so far.
   t_eigen total_energy{};
   // GS_energy is the energy of the ground states in absolute units. It is equal to the value of the variable
   // "total_energy" at the end of the iteration.
   t_eigen GS_energy{};
   std::vector<double> rel_Egs;        // Values of 'Egs' for all NRG steps.
   std::vector<double> abs_Egs;        // Values of 'Egs' (multiplied by the scale, i.e. in absolute scale) for all NRG steps.
   std::vector<double> energy_offsets; // Values of "total_energy" for all NRG steps.
   
   // ** Containers related to the FDM-NRG approach
   // Consult A. Weichselbaum, J. von Delft, PRL 99, 076402 (2007).
   vmpf ZnDG;                    // Z_n^D=\sum_s^D exp(-beta E^n_s), sum over **discarded** states at shell n
   vmpf ZnDN;                    // Z'_n^D=Z_n^D exp(beta E^n_0)=\sum_s^D exp[-beta(E^n_s-E^n_0)]
   std::vector<double> ZnDNd;    // 
   std::vector<double> wn;       // Weights w_n. They sum to 1.
   std::vector<double> wnfactor; // wn/ZnDG
   double ZZG{};                 // grand-canonical partition function with energies referred to the ground state energy
   double Z_fdm{};               // grand-canonical partition function (full-shell) at temperature T
   double F_fdm{};               // free-energy at temperature T
   double E_fdm{};               // energy at temperature T
   double C_fdm{};               // heat capacity at temperature T
   double S_fdm{};               // entropy at temperature T
   TD_FDM td_fdm;

   explicit Stats(const Params &P, const std::string filename_td = "td"s, const std::string filename_tdfdm = "tdfdm"s) : 
     td(P, filename_td), rel_Egs(MAX_NDX), abs_Egs(MAX_NDX), energy_offsets(MAX_NDX), 
     ZnDG(MAX_NDX), ZnDN(MAX_NDX), ZnDNd(MAX_NDX), wn(MAX_NDX), wnfactor(MAX_NDX), td_fdm(P, filename_tdfdm) {}
};

// Wrapper class for NRG spectral-function algorithms
template<typename S>
class Algo {
 private:
 public:
   using t_coef = typename traits<S>::t_coef;
   using Matrix = typename traits<S>::Matrix;
   const Params &P;
   Algo() = delete;
   Algo(const Algo&) = delete;
   explicit Algo(const Params &P) : P(P) {}
   virtual ~Algo() {}
   virtual void begin(const Step &) = 0;
   virtual void calc(const Step &, const Eigen<S> &, const Eigen<S> &, const Matrix &, const Matrix &, 
                     const t_coef, const Invar &, const Invar &, const DensMatElements<S> &, const Stats<S> &stats) = 0;
   virtual void end(const Step &) = 0;
   virtual std::string rho_type() { return ""; } // what rho type is required
};

// Object of class IterInfo cotains full information about matrix representations when entering stage N of the NRG
// iteration.
template<typename S> 
class IterInfo {
 public:
   Opch<S> opch;     // f operators (channels)
   CustomOp<S> ops;  // singlet operators (even parity)
   CustomOp<S> opsp; // singlet operators (odd parity)
   CustomOp<S> opsg; // singlet operators [global op]
   CustomOp<S> opd;  // doublet operators (spectral functions)
   CustomOp<S> opt;  // triplet operators (dynamical spin susceptibility)
   CustomOp<S> opq;  // quadruplet operators (spectral functions for J=3/2)
   CustomOp<S> opot; // orbital triplet operators

   void dump_diagonal(const size_t max_nr, std::ostream &F = std::cout) const {
     if (max_nr) {
       for (const auto &[name, m] : ops)  dump_diagonal_op(name, m, max_nr, F);
       for (const auto &[name, m] : opsg) dump_diagonal_op(name, m, max_nr, F);
     }
   }
   void trim_matrices(const DiagInfo<S> &diag) {
     ops.trim(diag);
     opsp.trim(diag);
     opsg.trim(diag);
     opd.trim(diag);
     opt.trim(diag);
     opq.trim(diag);
     opot.trim(diag);
   }
};

#include "spectral.h"

#include "coef.h"
#include "tridiag.h"
#include "diag.h"
#include "symmetry.h"
#include "matrix.h"
#include "recalc.h"

// Operator sumrules
template<typename S, typename F> 
auto norm(const MatrixElements<S> &m, std::shared_ptr<Symmetry<S>> Sym, F factor_fnc, const int SPIN) {
  typename traits<S>::t_weight sum{};
  for (const auto &[II, mat] : m) {
    const auto & [I1, Ip] = II;
    if (!Sym->check_SPIN(I1, Ip, SPIN)) continue;
    sum += factor_fnc(Ip, I1) * frobenius_norm(mat);
  }
  return 2.0 * cmpl(sum).real(); // Factor 2: Tr[d d^\dag + d^\dag d] = 2 \sum_{i,j} A_{i,j}^2 !!
}

template<typename S>
void operator_sumrules(const IterInfo<S> &a, std::shared_ptr<Symmetry<S>> Sym) {
  // We check sum rules wrt some given spin (+1/2, by convention). For non-spin-polarized calculations, this is
  // irrelevant (0).
  const int SPIN = Sym->isfield() ? 1 : 0;
  for (const auto &[name, m] : a.opd)
    std::cout << "norm[" << name << "]=" << norm(m, Sym, Sym->SpecdensFactorFnc(), SPIN) << std::endl;
  for (const auto &[name, m] : a.opq)
    std::cout << "norm[" << name << "]=" << norm(m, Sym, Sym->SpecdensquadFactorFnc(), 0) << std::endl;
}

#include "read-input.h"

// #### GF's

enum class gf_type { bosonic, fermionic };

// Sign factor in GFs for bosonic/fermionic operators
inline constexpr auto S_BOSONIC   = +1;
inline constexpr auto S_FERMIONIC = -1;
inline int gf_sign(const gf_type gt) { return gt == gf_type::bosonic ? S_BOSONIC : S_FERMIONIC; }

#include "bins.h"
#include "matsubara.h"
#include "spectrum.h"

// Check if the trace of the density matrix equals 'ref_value'.
template<typename S>
void check_trace_rho(const DensMatElements<S> &m, std::shared_ptr<Symmetry<S>> Sym, const double ref_value = 1.0) {
  if (!num_equal(m.trace(Sym->multfnc()), ref_value))
    throw std::runtime_error("check_trace_rho() failed");
}

using FactorFnc = std::function<double(const Invar &, const Invar &)>;
using CheckFnc = std::function<bool(const Invar &, const Invar &, int)>;

// All information about calculating a spectral function: pointers to the operator data, raw spectral data
// acccumulators, algorithm, etc.
template <typename S>
class BaseSpectrum {
 public:
   const MatrixElements<S> &op1, &op2;
   int spin{};                      // -1 or +1, or 0 where irrelevant
   using spAlgo = std::shared_ptr<Algo<S>>;
   spAlgo algo;      // Algo_FDM, Algo_DMNRG,...
   FactorFnc ff;
   CheckFnc cf;
   BaseSpectrum(const MatrixElements<S> &op1, const MatrixElements<S> &op2, const int spin, spAlgo algo, FactorFnc ff, CheckFnc cf) :
     op1(op1), op2(op2), spin(spin), algo(algo), ff(ff), cf(cf) {}
   // Calculate (finite temperature) spectral function 1/Pi Im << op1^\dag(t) op2(0) >>. Required spin direction is
   // determined by 'SPIN'. For SPIN=0 both spin direction are equivalent. For QSZ, we need to differentiate the two.
   void calc(const Step &step, const DiagInfo<S> &diag,
             const DensMatElements<S> &rho, const DensMatElements<S> &rhoFDM, const Stats<S> &stats) {
     algo->begin(step);
     const auto & rho_here = algo->rho_type() == "rhoFDM" ? rhoFDM : rho;
     // Strategy: we loop through all subspace pairs and check whether they have non-zero irreducible matrix elements.
     for(const auto &[Ii, diagi]: diag)
       for(const auto &[Ij, diagj]: diag) {
         const Twoinvar II {Ij,Ii};
         if (op1.count(II) && op2.count(II) && cf(Ij, Ii, spin))
           algo->calc(step, diagi, diagj, op1.at(II), op2.at(II), ff(Ii, Ij), Ii, Ij, rho_here, stats); // stats.Zft needed
       }
     algo->end(step);
   }
};
template <typename S>
using speclist = std::list<BaseSpectrum<S>>;

inline auto spec_fn(const std::string &name, const std::string &prefix, const std::string &algoname, const bool save = true) {
  if (save) fmt::print("Spectrum: {} {} {}\n", name, prefix, algoname); // Don't show if it's not going to be saved
  return prefix + "_" + algoname + "_dens_" + name; // no suffix (.dat vs. .bin)
}

#include "algo_FT.h"
#include "algo_DMNRG.h"
#include "algo_FDM.h"
#include "algo_CFS.h"

#include "dmnrg.h"
#include "splitting.h"

// Determine the ranges of index r
template<typename S>
Rmaxvals::Rmaxvals(const Invar &I, const InvarVec &InVec, const DiagInfo<S> &diagprev, std::shared_ptr<Symmetry<S>> Sym) {
  for (const auto &[i, In] : InVec | ranges::views::enumerate)
    values.push_back(Sym->triangle_inequality(I, In, Sym->QN_subspace(i)) ? diagprev.size_subspace(In) : 0);
}

// Formatted output of the computed expectation values
template<typename S>
class ExpvOutput {
 private:
   using t_expv = typename traits<S>::t_expv;
   std::ofstream F;                     // output stream
   std::map<std::string, t_expv> &m;    // reference to the name->value mapping
   const std::list<std::string> fields; // list of fields to be output (may be a subset of the fields actually present in m)
   const Params &P;
   void field_numbers() {     // Consecutive numbers for the columns
     F << '#' << formatted_output(1, P) << ' ';
     for (const auto ctr : range1(fields.size())) F << formatted_output(1 + ctr, P) << ' ';
     F << std::endl;
   }
   // Label and field names. Label is the first column (typically the temperature).
   void field_names(const std::string labelname = "T") {
     F << '#' << formatted_output(labelname, P) << ' ';
     std::transform(fields.cbegin(), fields.cend(), std::ostream_iterator<std::string>(F, " "), [this](const auto op) { return formatted_output(op, P); });
     F << std::endl;
   }
 public:
   // Output the current values for the label and for all the fields
   void field_values(const double labelvalue, const bool cout_dump = true) {
     F << ' ' << formatted_output(labelvalue, P) << ' ';
     std::transform(fields.cbegin(), fields.cend(), std::ostream_iterator<std::string>(F, " "), [this](const auto op) { return formatted_output(m[op], P); });
     F << std::endl;
     if (cout_dump)
       for (const auto &op: fields)
         fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "<{}>={}\n", op, to_string(m[op]));
   }
   ExpvOutput(const std::string &fn, std::map<std::string, t_expv> &m_, 
                   const std::list<std::string> &fields_, const Params &P_) : m(m_), fields(fields_), P(P_) {
     F.open(fn);
     field_numbers();
     field_names();
   }
};

template<typename S>
class Oprecalc {
 private:
   RUNTYPE runtype;
   std::shared_ptr<Symmetry<S>> Sym;
   MemTime mt;
   const Params &P;
 public:
   // Operators required to calculate expectation values and spectral densities
   struct Ops : public std::set<std::pair<std::string,std::string>> {
     void report(std::ostream &F = std::cout) {
       F << std::endl << "Computing the following operators:" << std::endl;
       for (const auto &[type, name]: *this) fmt::print("{} {}\n", name, type);
     }
     // Singlet operators are always recomputed in the first NRG run, so that we can calculate the expectation values.
     bool do_s(const std::string &name, const Params &P, const Step &step) {
       if (step.nrg()) return true;                                          // for computing <O> 
       if (step.dmnrg() && P.fdmexpv && step.N() <= P.fdmexpvn) return true; // for computing <O> using FDM algorithm
       return this->count({"s", name});
     }
     bool do_g(const std::string &name, const Params &P, const Step &step) {
       if (step.nrg()) return true;                                          // for computing <O>
       if (step.dmnrg() && P.fdmexpv && step.N() <= P.fdmexpvn) return true; // for computing <O> using FDM algorithm
       return this->count({"g", name});
     }
   };
   Ops ops;

   // Spectral densities
   struct SL : public speclist<S> {
     void calc(const Step &step, const DiagInfo<S> &diag, DensMatElements<S> &rho, DensMatElements<S> &rhoFDM, 
               const Stats<S> &stats, std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) {
       mt.time_it("spec");
       for (auto &i : *this) i.calc(step, diag, rho, rhoFDM, stats);
     }
   };
   SL sl;
   
   // Wrapper routine for recalculations
   template <typename RecalcFnc>
     MatrixElements<S> recalc(const std::string &name, const MatrixElements<S> &mold, RecalcFnc recalc_fnc, const std::string &tip, 
                              const Step &step, const DiagInfo<S> &diag, const QSrmax &qsrmax) {
       nrglog('0', "Recalculate " << tip << " " << name);
       auto mnew = recalc_fnc(diag, qsrmax, mold);
       if (tip == "g") Sym->recalc_global(step, diag, qsrmax, name, mnew);
       return mnew;
     }
   
   template <typename ... Args>
     MatrixElements<S> recalc_or_clear(const bool selected, Args&& ... args) {
       return selected ? recalc(std::forward<Args>(args)...) : MatrixElements<S>();
     }

   // Recalculate operator matrix representations
   ATTRIBUTE_NO_SANITIZE_DIV_BY_ZERO // avoid false positives  
   void recalculate_operators(IterInfo<S> &a, const Step &step, const DiagInfo<S> &diag, const QSrmax &qsrmax) {
       mt.time_it("recalc");
       for (auto &[name, m] : a.ops)
         m = recalc_or_clear(ops.do_s(name, P, step), name, m, [this](const auto &... pr) { return Sym->recalc_singlet(pr..., 1);  }, "s", step, diag, qsrmax);
       for (auto &[name, m] : a.opsp)
         m = recalc_or_clear(ops.count({"p", name}),  name, m, [this](const auto &... pr) { return Sym->recalc_singlet(pr..., -1); }, "p", step, diag, qsrmax);
       for (auto &[name, m] : a.opsg) 
         m = recalc_or_clear(ops.do_g(name, P, step), name, m, [this](const auto &... pr) { return Sym->recalc_singlet(pr...,  1); }, "g", step, diag, qsrmax);
       for (auto &[name, m] : a.opd)
         m = recalc_or_clear(ops.count({"d", name}),  name, m, [this](const auto &... pr) { return Sym->recalc_doublet(pr...);     }, "d", step, diag, qsrmax);
       for (auto &[name, m] : a.opt)
         m = recalc_or_clear(ops.count({"t", name}),  name, m, [this](const auto &... pr) { return Sym->recalc_triplet(pr...);     }, "t", step, diag, qsrmax);
       for (auto &[name, m] : a.opot)
         m = recalc_or_clear(ops.count({"ot", name}), name, m, [this](const auto &... pr) { return Sym->recalc_orb_triplet(pr...); }, "ot", step, diag, qsrmax);
       for (auto &[name, m] : a.opq)
         m = recalc_or_clear(ops.count({"q", name}),  name, m, [this](const auto &... pr) { return Sym->recalc_quadruplet(pr...);  }, "q", step, diag, qsrmax);
     }

   // Establish the data structures for storing spectral information [and prepare output files].
   template<typename A, typename M>
     void prepare_spec_algo(std::string prefix, const Params &P, FactorFnc ff, CheckFnc cf, M && op1, M && op2, int spin,
                            std::string name, const gf_type gt) {
       BaseSpectrum<S> spec(std::forward<M>(op1), std::forward<M>(op2), spin, std::make_shared<A>(name, prefix, gt, P), ff, cf);
       sl.push_back(spec);
     }

   template<typename ... Args>
     void prepare_spec(std::string prefix, Args && ... args) {
       if (prefix == "gt") {
         if (runtype == RUNTYPE::NRG) prepare_spec_algo<Algo_GT<S,0>>(prefix, P, std::forward<Args>(args)...);
         return;
       }
       if (prefix == "i1t") {
         if (runtype == RUNTYPE::NRG) prepare_spec_algo<Algo_GT<S,1>>(prefix, P, std::forward<Args>(args)...);
         return;
       }
       if (prefix == "i2t") {
         if (runtype == RUNTYPE::NRG) prepare_spec_algo<Algo_GT<S,2>>(prefix, P, std::forward<Args>(args)...);
         return;
       }
       if (prefix == "chit") {
         if (runtype == RUNTYPE::NRG) prepare_spec_algo<Algo_CHIT<S>>(prefix, P, std::forward<Args>(args)...);
         return;
       }
       // If we did not return from this funciton by this point, what we are computing is the spectral function. There are
       // several possibilities in this case, all of which may be enabled at the same time.
       if (runtype == RUNTYPE::NRG) {
         if (P.finite)     prepare_spec_algo<Algo_FT<S>>    (prefix, P, std::forward<Args>(args)...);
         if (P.finitemats) prepare_spec_algo<Algo_FTmats<S>>(prefix, P, std::forward<Args>(args)...);
       }
       if (runtype == RUNTYPE::DMNRG) {
         if (P.dmnrg)     prepare_spec_algo<Algo_DMNRG<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.dmnrgmats) prepare_spec_algo<Algo_DMNRGmats<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.cfs)       prepare_spec_algo<Algo_CFS<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.cfsgt)     prepare_spec_algo<Algo_CFSgt<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.cfsls)     prepare_spec_algo<Algo_CFSls<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.fdm)       prepare_spec_algo<Algo_FDM<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.fdmgt)     prepare_spec_algo<Algo_FDMgt<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.fdmls)     prepare_spec_algo<Algo_FDMls<S>>(prefix, P, std::forward<Args>(args)...);
         if (P.fdmmats)   prepare_spec_algo<Algo_FDMmats<S>>(prefix, P, std::forward<Args>(args)...);
       }
     }

   // Construct the suffix of the filename for spectral density files: 'A_?-A_?'.
   // If SPIN == 1 or SPIN == -1, '-u' or '-d' is appended to the string.
   auto sdname(const std::string &a, const std::string &b, const int spin) {
     return a + "-" + b + (spin == 0 ? "" : (spin == 1 ? "-u" : "-d"));
   }

   void loopover(const CustomOp<S> &set1, const CustomOp<S> &set2,
                 const string_token &stringtoken, FactorFnc ff, CheckFnc cf,
                 const std::string &prefix,
                 const std::string &type1, const std::string &type2, const gf_type gt, const int spin) {
    for (const auto &[name1, op1] : set1) {
      for (const auto &[name2, op2] : set2) {
        if (const auto name = sdname(name1, name2, spin); stringtoken.find(name)) {
          prepare_spec(prefix, ff, cf, op1, op2, spin, name, gt);
          ops.insert({type1, name1});
          ops.insert({type2, name2});
        }
      }
    }
  }

  // Reset lists of operators which need to be iterated
  Oprecalc(const RUNTYPE &runtype, const IterInfo<S> &a, std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) : runtype(runtype), Sym(Sym), mt(mt), P(P) {
    std::cout << std::endl << "Computing the following spectra:" << std::endl;
    // Correlators (singlet operators of all kinds)
    string_token sts(P.specs);
    loopover(a.ops,  a.ops,  sts, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "corr", "s", "s", gf_type::bosonic, 0);
    loopover(a.opsp, a.opsp, sts, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "corr", "p", "p", gf_type::bosonic, 0);
    loopover(a.opsg, a.opsg, sts, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "corr", "g", "g", gf_type::bosonic, 0);
    loopover(a.ops,  a.opsg, sts, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "corr", "s", "g", gf_type::bosonic, 0);
    loopover(a.opsg, a.ops,  sts, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "corr", "g", "s", gf_type::bosonic, 0);
    // Global susceptibilities (global singlet operators)
    string_token stchit(P.specchit);
    loopover(a.ops,  a.ops,  stchit, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "chit", "s", "s", gf_type::bosonic, 0);
    loopover(a.ops,  a.opsg, stchit, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "chit", "s", "g", gf_type::bosonic, 0);
    loopover(a.opsg, a.ops,  stchit, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "chit", "g", "s", gf_type::bosonic, 0);
    loopover(a.opsg, a.opsg, stchit, Sym->CorrelatorFactorFnc(), Sym->TrivialCheckSpinFnc(), "chit", "g", "g", gf_type::bosonic, 0);
    // Dynamic spin susceptibilities (triplet operators)
    string_token stt(P.spect);
    loopover(a.opt, a.opt, stt, Sym->SpinSuscFactorFnc(), Sym->TrivialCheckSpinFnc(),  "spin", "t", "t", gf_type::bosonic, 0);
    string_token stot(P.specot);
    loopover(a.opot, a.opot, stot, Sym->OrbSuscFactorFnc(), Sym->TrivialCheckSpinFnc(), "orbspin", "ot", "ot", gf_type::bosonic, 0);
    const auto varmin = Sym->isfield() ? -1 : 0;
    const auto varmax = Sym->isfield() ? +1 : 0;
    // Spectral functions (doublet operators)
    string_token std(P.specd);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(a.opd, a.opd, std, Sym->SpecdensFactorFnc(), Sym->SpecdensCheckSpinFnc(), "spec", "d", "d", gf_type::fermionic, SPIN);
    string_token stgt(P.specgt);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(a.opd, a.opd, stgt, Sym->SpecdensFactorFnc(), Sym->SpecdensCheckSpinFnc(), "gt", "d", "d", gf_type::fermionic, SPIN);
    string_token sti1t(P.speci1t);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(a.opd, a.opd, sti1t, Sym->SpecdensFactorFnc(), Sym->SpecdensCheckSpinFnc(),"i1t", "d", "d", gf_type::fermionic, SPIN);
    string_token sti2t(P.speci2t);
    for (int SPIN = varmin; SPIN <= varmax; SPIN += 2)
      loopover(a.opd, a.opd, sti2t, Sym->SpecdensFactorFnc(), Sym->SpecdensCheckSpinFnc(), "i2t", "d", "d", gf_type::fermionic, SPIN);
    // Spectral functions (quadruplet operators)
    string_token stq(P.specq);
    loopover(a.opq, a.opq, stq, Sym->SpecdensquadFactorFnc(), Sym->TrivialCheckSpinFnc(),  "specq", "q", "q", gf_type::fermionic, 0);
    ops.report();
  }
};

// Store eigenvalue & quantum numbers information (RG flow diagrams)
class Annotated {
 private:
   std::ofstream F;
   // scaled = true -> output scaled energies (i.e. do not multiply by the rescale factor)
   template<typename S>
     inline auto scaled_energy(typename traits<S>::t_eigen e, const Step &step, const Stats<S> &stats,
                               bool scaled = true, bool absolute = false) {
     return e * (scaled ? 1.0 : step.scale()) + (absolute ? stats.total_energy : 0.0);
   }
   const Params &P;
 public:
   explicit Annotated(const Params &P) : P(P) {}
   template<typename S> void dump(const Step &step, const DiagInfo<S> &diag, const Stats<S> &stats, 
                                  std::shared_ptr<Symmetry<S>> Sym, const std::string filename = "annotated.dat") {
     if (!P.dumpannotated) return;
     if (!F.is_open()) { // open output file
       F.open(filename);
       F << std::setprecision(P.dumpprecision);
     }
     std::vector<std::pair<double, Invar>> seznam;
     for (const auto &[I, eig] : diag)
       for (const auto e : eig.value_zero)
         seznam.emplace_back(e, I);
     ranges::sort(seznam);
     size_t len = std::min<size_t>(seznam.size(), P.dumpannotated); // non-const
     // If states are clustered, we dump the full cluster
     while (len < seznam.size()-1 && my_fcmp(seznam[len].first, seznam[len-1].first, P.grouptol) == 0) len++;
     auto scale = [&step, &stats, this](auto x) { return scaled_energy(x, step, stats, P.dumpscaled, P.dumpabs); };
     if (P.dumpgroups) {
       // Group by degeneracies
       for (size_t i = 0; i < len;) { // i increased in the while loop below
         const auto [e0, I0] = seznam[i];
         F << scale(e0);
         std::vector<std::string> QNstrings;
         size_t total_degeneracy = 0; // Total number of levels (incl multiplicity)
         while (i < len && my_fcmp(seznam[i].first, e0, P.grouptol) == 0) {
           const auto [e, I] = seznam[i];
           QNstrings.push_back(to_string(I));
           total_degeneracy += Sym->mult(I);
           i++;
         }
         ranges::sort(QNstrings);
         for (const auto &j : QNstrings) F << " (" << j << ")";
         F << " [" << total_degeneracy << "]" << std::endl;
       }
     } else {
       seznam.resize(len); // truncate!
       for (const auto &[e, I] : seznam) 
         F << scale(e) << " " << I << std::endl;
     }
     F << std::endl; // Consecutive iterations are separated by an empty line
   }
};

// Handle all output
template<typename S>
struct Output {
  const RUNTYPE runtype;
  const Params &P;
  Annotated annotated;
  std::ofstream Fenergies;  // all energies (different file for NRG and for DMNRG)
  std::unique_ptr<ExpvOutput<S>> custom;
  std::unique_ptr<ExpvOutput<S>> customfdm;
  Output(const RUNTYPE &runtype, const IterInfo<S> &iterinfo, Stats<S> &stats, const Params &P,
              const std::string filename_energies= "energies.nrg"s,
              const std::string filename_custom = "custom", 
              const std::string filename_customfdm = "customfdm")
    : runtype(runtype), P(P), annotated(P) {
      // We dump all energies to separate files for NRG and DM-NRG runs. This is a very convenient way to check if both
      // runs produce the same results.
      if (P.dumpenergies && runtype == RUNTYPE::NRG) Fenergies.open(filename_energies);
      std::list<std::string> ops;
      for (const auto &name : iterinfo.ops  | boost::adaptors::map_keys) ops.push_back(name);
      for (const auto &name : iterinfo.opsg | boost::adaptors::map_keys) ops.push_back(name);
      if (runtype == RUNTYPE::NRG)
        custom = std::make_unique<ExpvOutput<S>>(filename_custom, stats.expv, ops, P);
      else if (runtype == RUNTYPE::DMNRG && P.fdmexpv) 
        customfdm = std::make_unique<ExpvOutput<S>>(filename_customfdm, stats.fdmexpv, ops, P);
    }
  // Dump all energies in diag to a file
  void dump_all_energies(const DiagInfo<S> &diag, const int N) {
    if (!Fenergies) return;
    Fenergies << std::endl << "===== Iteration number: " << N << std::endl;
    diag.dump_value_zero(Fenergies);
  }
};

template<typename S>
CONSTFNC auto calc_trace_singlet(const Step &step, const DiagInfo<S> &diag, 
                                 const MatrixElements<S> &n, std::shared_ptr<Symmetry<S>> Sym) {
  typename traits<S>::t_matel tr{};
  for (const auto &[I, eig] : diag) {
    const auto & nI = n.at({I,I});
    const auto dim = eig.getnrstored();
    my_assert(dim == nI.size2());
    typename traits<S>::t_matel sum{};
    for (const auto r : range0(dim)) sum += exp(-step.TD_factor() * eig.value_zero(r)) * nI(r, r);
    tr += Sym->mult(I) * sum;
  }
  return tr; // note: t_expv = t_matel
}

// Measure thermodynamic expectation values of singlet operators
template<typename S>
void measure_singlet(const Step &step, Stats<S> &stats, const DiagInfo<S> &diag, const IterInfo<S> &a, 
                            Output<S> &output, std::shared_ptr<Symmetry<S>> Sym, const Params &P) {
  const auto Z = ranges::accumulate(diag, 0.0, [&Sym, &step](auto total, const auto &d) { const auto &[I, eig] = d;
    return total + Sym->mult(I) * ranges::accumulate(eig.value_zero, 0.0,
                                                     [f=step.TD_factor()](auto sum, const auto &x) { return sum + exp(-f*x); }); });
  for (const auto &[name, m] : a.ops)  stats.expv[name] = calc_trace_singlet(step, diag, m, Sym) / Z;
  for (const auto &[name, m] : a.opsg) stats.expv[name] = calc_trace_singlet(step, diag, m, Sym) / Z;
  output.custom->field_values(step.Teff());
}

template<typename T>
T trace_contract(const ublas::matrix<T> &A, const ublas::matrix<T> &B, const size_t range)
{
  T sum{};
  for (const auto i : range0(range))
       for (const auto j : range0(range)) 
      sum += A(i, j) * B(j, i);
  return sum;
}

template<typename S>
CONSTFNC auto calc_trace_fdm_kept(const size_t ndx, const MatrixElements<S> &n, const DensMatElements<S> &rhoFDM, 
                                  const AllSteps<S> &dm, std::shared_ptr<Symmetry<S>> Sym) {
  typename traits<S>::t_matel tr{};
  for (const auto &[I, rhoI] : rhoFDM)
    tr += Sym->mult(I) * trace_contract(rhoI, n.at({I,I}), dm[ndx].at(I).kept); // over kept states ONLY
  return tr;
}

template<typename S>
void measure_singlet_fdm(const Step &step, Stats<S> &stats, const DiagInfo<S> &diag, const IterInfo<S> &a, 
                         Output<S> &output,  const DensMatElements<S> &rhoFDM, 
                         const AllSteps<S> &dm, std::shared_ptr<Symmetry<S>> Sym, const Params &P) {
  for (const auto &[name, m] : a.ops)  stats.fdmexpv[name] = calc_trace_fdm_kept(step.N(), m, rhoFDM, dm, Sym);
  for (const auto &[name, m] : a.opsg) stats.fdmexpv[name] = calc_trace_fdm_kept(step.N(), m, rhoFDM, dm, Sym);
  output.customfdm->field_values(P.T);
}

// Calculate grand canonical partition function at current NRG energy shell. This is not the same as the true
// partition function of the full problem! Instead this is the Z_N that is used to initialize the density matrix,
// i.e. rho = 1/Z_N \sum_{l} exp{-beta E_l} |l;N> <l;N|.  grand_canonical_Z() is also used to calculate stats.Zft,
// that is used to compute the spectral function with the conventional approach, as well as stats.Zgt for G(T)
// calculations, stats.Zchit for chi(T) calculations.
template<typename S>
auto grand_canonical_Z(const Step &step, const DiagInfo<S> &diag, std::shared_ptr<Symmetry<S>> Sym, const double factor = 1.0) {
  double ZN{};
  for (const auto &[I, eig]: diag) 
    for (const auto &i : eig.kept()) // sum over all kept states
      ZN += Sym->mult(I) * exp(-eig.value_zero(i) * step.scT() * factor);
  my_assert(ZN >= 1.0);
  return ZN;
}

// Calculate rho_N, the density matrix at the last NRG iteration. It is
// normalized to 1. Note: in CFS approach, we consider all states in the
// last iteration to be "discarded".
// For the details on the full Fock space approach see:
// F. B. Anders, A. Schiller, Phys. Rev. Lett. 95, 196801 (2005).
// F. B. Anders, A. Schiller, Phys. Rev. B 74, 245113 (2006).
// R. Peters, Th. Pruschke, F. B. Anders, Phys. Rev. B 74, 245114 (2006).
template<typename S>
auto init_rho(const Step &step, const DiagInfo<S> &diag, std::shared_ptr<Symmetry<S>> Sym) {
  DensMatElements<S> rho;
  for (const auto &[I, eig]: diag)
    rho[I] = eig.diagonal_exp(step.scT()) / grand_canonical_Z(step, diag, Sym);
  check_trace_rho(rho, Sym);
  return rho;
}

// Determine the number of states to be retained. Returns Emax - the highest energy to still be retained.
template<typename S>
auto highest_retained_energy(const Step &step, const DiagInfo<S> &diag, const Params &P) {
  auto energies = diag.sorted_energies();
  my_assert(energies.front() == 0.0); // check for the subtraction of Egs
  const auto totalnumber = energies.size();
  size_t nrkeep;
  if (P.keepenergy <= 0.0) {
    nrkeep = P.keep;
  } else {
    const auto keepenergy = P.keepenergy * step.unscale();
    // We add 1 for historical reasons. We thus keep states with E<=Emax, and one additional state which has E>Emax.
    nrkeep = 1 + ranges::count_if(energies, [=](double e) { return e <= keepenergy; });
    nrkeep = std::clamp<size_t>(nrkeep, P.keepmin, P.keep);
  }
  // Check for near degeneracy and ensure that the truncation occurs in a "gap" between nearly-degenerate clusters of
  // eigenvalues.
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

struct truncate_stats {
  size_t nrall, nrallmult, nrkept, nrkeptmult;
  template<typename S> truncate_stats(const DiagInfo<S> &diag, std::shared_ptr<Symmetry<S>> Sym) {
    nrall = ranges::accumulate(diag, 0, [](int n, const auto &d) { const auto &[I, eig] = d; return n+eig.getdim(); });
    nrallmult = ranges::accumulate(diag, 0, [Sym](int n, const auto &d) { const auto &[I, eig] = d; return n+Sym->mult(I)*eig.getdim(); });
    nrkept = ranges::accumulate(diag, 0, [](int n, const auto &d) { const auto &[I, eig] = d; return n+eig.getnrkept(); });
    nrkeptmult = ranges::accumulate(diag, 0, [Sym](int n, const auto &d) { const auto &[I, eig] = d; return n+Sym->mult(I)*eig.getnrkept(); });
  }
  void report() {
    std::cout << nrgdump4(nrkept, nrkeptmult, nrall, nrallmult) << std::endl;
  }
};

struct NotEnough : public std::exception {};

// Compute the number of states to keep in each subspace. Returns true if an insufficient number of states has been
// obtained in the diagonalization and we need to compute more states.
template<typename S>
void truncate_prepare(const Step &step, DiagInfo<S> &diag, std::shared_ptr<Symmetry<S>> Sym, const Params &P) {
  const auto Emax = highest_retained_energy(step, diag, P);
  for (auto &[I, eig] : diag)
    diag[I].truncate_prepare_subspace(step.last() && P.keep_all_states_in_last_step() ? eig.getnrcomputed() :
                                      ranges::count_if(eig.value_zero, [Emax](const double e) { return e <= Emax; }));
  std::cout << "Emax=" << Emax/step.unscale() << " ";
  truncate_stats ts(diag, Sym);
  ts.report();
  if (ranges::any_of(diag, [Emax](const auto &d) { const auto &[I, eig] = d; 
    return eig.getnrkept() == eig.getnrcomputed() && eig.value_zero(eig.getnrcomputed()-1) != Emax && eig.getnrcomputed() < eig.getdim(); }))
      throw NotEnough();
  const double ratio = double(ts.nrkept) / ts.nrall;
  fmt::print(FMT_STRING("Kept: {} out of {}, ratio={:.3}\n"), ts.nrkept, ts.nrall, ratio);
}

// Calculate partial statistical sums, ZnD*, and the grand canonical Z (stats.ZZG), computed with respect to absolute
// energies. calc_ZnD() must be called before the second NRG run.
template<typename S>
void calc_ZnD(const AllSteps<S> &dm, Stats<S> &stats, std::shared_ptr<Symmetry<S>> Sym, const double T) {
  mpf_set_default_prec(400); // this is the number of bits, not decimal digits!
  for (const auto N : dm.Nall()) {
    my_mpf ZnDG, ZnDN; // arbitrary-precision accumulators to avoid precision loss
    mpf_set_d(ZnDG, 0.0);
    mpf_set_d(ZnDN, 0.0);
    for (const auto &[I, ds] : dm[N])
      for (const auto i : ds.all()) {
        my_mpf g, n;
        mpf_set_d(g, Sym->mult(I) * exp(-ds.eig.absenergyG[i]/T)); // absenergyG >= 0.0
        mpf_set_d(n, Sym->mult(I) * exp(-ds.eig.absenergyN[i]/T)); // absenergyN >= 0.0
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
  for (const auto N : dm.Nall()) {
    my_mpf a;
    mpf_set(a, stats.ZnDG[N]);
    my_mpf b;
    mpf_set_d(b, Sym->nr_combs());
    mpf_pow_ui(b, b, dm.Nend - N - 1);
    my_mpf c;
    mpf_mul(c, a, b);
    mpf_add(ZZG, ZZG, c);
  }
  stats.ZZG = mpf_get_d(ZZG);
  std::cout << "ZZG=" << HIGHPREC(stats.ZZG) << std::endl;
  for (const auto N : dm.Nall()) {
    const double w  = pow(Sym->nr_combs(), int(dm.Nend - N - 1)) / stats.ZZG;
    stats.wnfactor[N] = w; // These ratios enter the terms for the spectral function.
    stats.wn[N] = w * mpf_get_d(stats.ZnDG[N]); // This is w_n defined after Eq. (8) in the WvD paper.
  }
  const auto sumwn = ranges::accumulate(stats.wn, 0.0);
  std::cout << "sumwn=" << sumwn << " sumwn-1=" << sumwn - 1.0 << std::endl;
  my_assert(num_equal(sumwn, 1.0));  // Check the sum-rule.
}

template<typename S>
void report_ZnD(Stats<S> &stats, const Params &P) {
  for (const auto N : P.Nall())
    std::cout << "ZG[" << N << "]=" << HIGHPREC(mpf_get_d(stats.ZnDG[N])) << std::endl;
  for (const auto N : P.Nall())
    std::cout << "ZN[" << N << "]=" << HIGHPREC(mpf_get_d(stats.ZnDN[N])) << std::endl;
  for (const auto N : P.Nall())
    std::cout << "w[" << N << "]=" << HIGHPREC(stats.wn[N]) << std::endl;
  for (const auto N : P.Nall())
    std::cout << "wfactor[" << N << "]=" << HIGHPREC(stats.wnfactor[N]) << std::endl;
}

// TO DO: use Boost.Multiprecision instead of low-level GMP calls
// https://www.boost.org/doc/libs/1_72_0/libs/multiprecision/doc/html/index.html
template<typename S>
void fdm_thermodynamics(const AllSteps<S> &dm, Stats<S> &stats, std::shared_ptr<Symmetry<S>> Sym, const double T)
{
  stats.td_fdm.T = T;
  stats.Z_fdm = stats.ZZG*exp(-stats.GS_energy/T); // this is the true partition function
  stats.td_fdm.F = stats.F_fdm = -log(stats.ZZG)*T+stats.GS_energy; // F = -k_B*T*log(Z)
  // We use multiple precision arithmetics to ensure sufficient accuracy in the calculation of
  // the variance of energy and thus the heat capacity.
  my_mpf E, E2;
  mpf_set_d(E, 0.0);
  mpf_set_d(E2, 0.0);
  for (const auto N : dm.Nall())
    if (stats.wn[N] > 1e-16) 
      for (const auto &[I, ds] : dm[N]) 
        for (const auto i : ds.all()) {
          my_mpf weight;
          mpf_set_d(weight, stats.wn[N] * Sym->mult(I) * exp(-ds.eig.absenergyN[i]/T));
          mpf_div(weight, weight, stats.ZnDN[N]);
          my_mpf e;
          mpf_set_d(e, ds.eig.absenergy[i]);
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
  stats.td_fdm.C = stats.C_fdm = mpf_get_d(varE)/pow(T,2);
  stats.td_fdm.S = stats.S_fdm = (stats.E_fdm-stats.F_fdm)/T;
  std::cout << std::endl;
  std::cout << "Z_fdm=" << HIGHPREC(stats.Z_fdm) << std::endl;
  std::cout << "F_fdm=" << HIGHPREC(stats.F_fdm) << std::endl;
  std::cout << "E_fdm=" << HIGHPREC(stats.E_fdm) << std::endl;
  std::cout << "C_fdm=" << HIGHPREC(stats.C_fdm) << std::endl;
  std::cout << "S_fdm=" << HIGHPREC(stats.S_fdm) << std::endl;
  std::cout << std::endl;
  stats.td_fdm.save_values();
}

// We calculate thermodynamic quantities before truncation to make better use of the available states. Here we
// compute quantities which are defined for all symmetry types. Other calculations are performed by calculate_TD
// member functions defined in symmetry.h
template<typename S>
void calculate_TD(const Step &step, const DiagInfo<S> &diag, Stats<S> &stats, Output<S> &output, 
                  std::shared_ptr<Symmetry<S>> Sym, const double additional_factor = 1.0) {
  // Rescale factor for energies. The energies are expressed in units of omega_N, thus we need to appropriately
  // rescale them to calculate the Boltzmann weights at the temperature scale Teff (Teff=scale/betabar).
  const auto rescale_factor = step.TD_factor() * additional_factor;
  auto mult = [Sym](const auto &I) { return Sym->mult(I); };
  const auto Z  = diag.trace([](double x) { return 1; },        rescale_factor, mult); // partition function
  const auto E  = diag.trace([](double x) { return x; },        rescale_factor, mult); // Tr[beta H]
  const auto E2 = diag.trace([](double x) { return pow(x,2); }, rescale_factor, mult); // Tr[(beta H)^2]
  stats.Z = Z;
  stats.td.T  = step.Teff();
  stats.td.E  = E/Z;               // beta <H>
  stats.td.E2 = E2/Z;              // beta^2 <H^2>
  stats.td.C  = E2/Z - pow(E/Z,2); // C/k_B=beta^2(<H^2>-<H>^2)
  stats.td.F  = -log(Z);           // F/(k_B T)=-ln(Z)
  stats.td.S  = E/Z+log(Z);        // S/k_B=beta<H>+ln(Z)
  Sym->calculate_TD(step, diag, stats, rescale_factor);  // symmetry-specific calculation routine
  stats.td.save_values();
}

template<typename S>
void calculate_spectral_and_expv(const Step &step, Stats<S> &stats, Output<S> &output, Oprecalc<S> &oprecalc, 
                                 const DiagInfo<S> &diag, const IterInfo<S> &iterinfo, const AllSteps<S> &dm, 
                                 std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) {
  // Zft is used in the spectral function calculations using the conventional approach. We calculate it here, in
  // order to avoid recalculations later on.
  stats.Zft = grand_canonical_Z(step, diag, Sym);
  if (std::string(P.specgt) != "" || std::string(P.speci1t) != "" || std::string(P.speci2t) != "")
    stats.Zgt = grand_canonical_Z(step, diag, Sym, 1.0/(P.gtp*step.scT()) ); // exp(-x*gtp)
  if (std::string(P.specchit) != "") 
    stats.Zchit = grand_canonical_Z(step, diag, Sym, 1.0/(P.chitp*step.scT()) ); // exp(-x*chitp)
  DensMatElements<S> rho, rhoFDM;
  if (step.dmnrg()) {
    if (P.need_rho()) {
      rho.load(step.ndx(), P, fn_rho, P.removefiles);
      check_trace_rho(rho, Sym); // Check if Tr[rho]=1, i.e. the normalization
    }
    if (P.need_rhoFDM()) 
      rhoFDM.load(step.ndx(), P, fn_rhoFDM, P.removefiles);
  }
  oprecalc.sl.calc(step, diag, rho, rhoFDM, stats, Sym, mt, P);
  if (step.nrg()) {
    measure_singlet(step, stats, diag, iterinfo, output, Sym, P);
    iterinfo.dump_diagonal(P.dumpdiagonal);
  }
  if (step.dmnrg() && P.fdmexpv && step.N() == P.fdmexpvn) measure_singlet_fdm(step, stats, diag, iterinfo, output, rhoFDM, dm, Sym, P);
}

// Perform calculations of physical quantities. Called prior to NRG iteration (if calc0=true) and after each NRG
// step.
template<typename S>
void perform_basic_measurements(const Step &step, const DiagInfo<S> &diag, std::shared_ptr<Symmetry<S>> Sym,
                                Stats<S> &stats, Output<S> &output) {
  output.dump_all_energies(diag, step.ndx());
  calculate_TD(step, diag, stats, output, Sym);
  output.annotated.dump(step, diag, stats, Sym);
}

// Subspaces for the new iteration
template<typename S>
auto new_subspaces(const DiagInfo<S> &diagprev, std::shared_ptr<Symmetry<S>> Sym) {
  std::set<Invar> subspaces;
  for (const auto &I : diagprev.subspaces()) {
    const auto all = Sym->new_subspaces(I);
    const auto non_empty = all | ranges::views::filter([&Sym](const auto &In) { return Sym->Invar_allowed(In); }) | ranges::to<std::vector>();
    std::copy(non_empty.begin(), non_empty.end(), std::inserter(subspaces, subspaces.end()));
  }
  return subspaces;
}

template<typename S>
typename traits<S>::Matrix prepare_task_for_diag(const Step &step, const Invar &I, const Opch<S> &opch, const Coef<S> &coef, 
                                                 const DiagInfo<S> &diagprev, std::shared_ptr<Symmetry<S>> Sym, const Params &P) {
  const auto anc = Sym->ancestors(I);
  const Rmaxvals rm{I, anc, diagprev, Sym};
  typename traits<S>::Matrix h(rm.total(), rm.total(), 0);   // H_{N+1}=\lambda^{1/2} H_N+\xi_N (hopping terms)
  for (const auto i : Sym->combs())
    for (const auto r : range0(rm.rmax(i)))
      h(rm.offset(i) + r, rm.offset(i) + r) = P.nrg_step_scale_factor() * diagprev.at(anc[i]).value_zero(r);
  Sym->make_matrix(h, step, rm, I, anc, opch, coef);  // Symmetry-type-specific matrix initialization steps
  if (P.logletter('m')) dump_matrix(h);
  return h;
}

template<typename S>
auto diagonalisations_OpenMP(const Step &step, const Opch<S> &opch, const Coef<S> &coef, const DiagInfo<S> &diagprev,
                             const std::vector<Invar> &tasks, const DiagParams &DP, std::shared_ptr<Symmetry<S>> Sym, const Params &P) {
  DiagInfo<S> diagnew;
  const auto nr = tasks.size();
  size_t itask = 0;
  // cppcheck-suppress unreadVariable symbolName=nth
  const int nth = P.diagth; // NOLINT
#pragma omp parallel for schedule(dynamic) num_threads(nth)
  for (itask = 0; itask < nr; itask++) {
    const Invar I  = tasks[itask];
    auto h = prepare_task_for_diag(step, I, opch, coef, diagprev, Sym, P); // non-const, consumed by diagonalise()
    const int thid = omp_get_thread_num();
#pragma omp critical
    { nrglog('(', "Diagonalizing " << I << " size=" << h.size1() << " (task " << itask + 1 << "/" << nr << ", thread " << thid << ")"); }
    auto e = diagonalise<S>(h, DP, -1); // -1 = not using MPI
#pragma omp critical
    { diagnew[I] = e; }
  }
  return diagnew;
}

enum TAG : int { TAG_EXIT = 1, TAG_DIAG_DBL, TAG_DIAG_CMPL, TAG_SYNC, TAG_MATRIX, TAG_INVAR, 
                 TAG_MATRIX_SIZE, TAG_MATRIX_LINE, TAG_EIGEN_INT, TAG_EIGEN_VEC };

class MPI {
 private:
   boost::mpi::environment &mpienv;
   boost::mpi::communicator &mpiw;
   
 public:
   MPI(boost::mpi::environment &mpienv, boost::mpi::communicator &mpiw) : mpienv(mpienv), mpiw(mpiw) {}
   auto myrank() { return mpiw.rank(); } // used in diag.h, time_mem.h
   void send_params(const DiagParams &DP) {
     mpilog("Sending diag parameters " << DP.diag << " " << DP.diagratio);
     for (auto i = 1; i < mpiw.size(); i++) mpiw.send(i, TAG_SYNC, 0);
     auto DPcopy = DP;
     boost::mpi::broadcast(mpiw, DPcopy, 0);
   }
   DiagParams receive_params() {
     DiagParams DP;
     boost::mpi::broadcast(mpiw, DP, 0);
     mpilog("Received diag parameters " << DP.diag << " " << DP.diagratio);
     return DP;
   }
   void check_status(const boost::mpi::status &status) {
     if (status.error()) {
       std::cout << "MPI communication error. rank=" << mpiw.rank() << std::endl;
       mpienv.abort(1);
     }
   }
   // NOTE: MPI is limited to message size of 2GB (or 4GB). For big problems we thus need to send objects line by line.
   template<typename S> void send_matrix(const int dest, const ublas::matrix<S> &m) {
     const auto size1 = m.size1();
     mpiw.send(dest, TAG_MATRIX_SIZE, size1);
     const auto size2 = m.size2();
     mpiw.send(dest, TAG_MATRIX_SIZE, size2);
     mpilog("Sending matrix of size " << size1 << " x " << size2 << " line by line to " << dest);
     for (const auto i: range0(size1)) {
       ublas::vector<typename traits<S>::t_matel> vec = ublas::matrix_row<const ublas::matrix<S>>(m, i); // YYY
       mpiw.send(dest, TAG_MATRIX_LINE, vec);
     }
   }
   template<typename S> auto receive_matrix(const int source) {
     size_t size1;
     check_status(mpiw.recv(source, TAG_MATRIX_SIZE, size1));
     size_t size2;
     check_status(mpiw.recv(source, TAG_MATRIX_SIZE, size2));
     typename traits<S>::Matrix m(size1, size2);
     mpilog("Receiving matrix of size " << size1 << " x " << size2 << " line by line from " << source);
     for (const auto i: range0(size1)) {
       ublas::vector<typename traits<S>::t_matel> vec;
       check_status(mpiw.recv(source, TAG_MATRIX_LINE, vec));
       my_assert(vec.size() == size2);
       ublas::matrix_row<typename traits<S>::Matrix>(m, i) = vec;
     }
     return m;
   }
   template<typename S> void send_eigen(const int dest, const Eigen<S> &eig) {
     mpilog("Sending eigen from " << mpiw.rank() << " to " << dest);
     mpiw.send(dest, TAG_EIGEN_VEC, eig.value_orig);
     send_matrix<S>(dest, eig.matrix);
   }
   template<typename S> auto receive_eigen(const int source) {
     mpilog("Receiving eigen from " << source << " on " << mpiw.rank());
     Eigen<S> eig;
     check_status(mpiw.recv(source, TAG_EIGEN_VEC, eig.value_orig));
     eig.matrix = receive_matrix<S>(source);
     return eig;
   } 
   // Read results from a slave process.
   template<typename S> std::pair<Invar, Eigen<S>> read_from(const int source) {
     mpilog("Reading results from " << source);
     const auto eig = receive_eigen<S>(source);
     Invar Irecv;
     check_status(mpiw.recv(source, TAG_INVAR, Irecv));
     mpilog("Received results for subspace " << Irecv << " [nr=" << eig.getnrstored() << ", dim=" << eig.getdim() << "]");
     my_assert(eig.value_orig.size() == eig.matrix.size1());
     my_assert(eig.matrix.size1() <= eig.matrix.size2());
     return {Irecv, eig};
   } 
   // Handle a diagonalisation request
   template<typename S> void slave_diag(const int master, const DiagParams &DP) {
     // 1. receive the matrix and the subspace identification
     auto m = receive_matrix<S>(master);
     Invar I;
     check_status(mpiw.recv(master, TAG_INVAR, I));
     // 2. preform the diagonalisation
     const auto eig = diagonalise<S>(m, DP, myrank());
     // 3. send back the results
     send_eigen<S>(master, eig);
     mpiw.send(master, TAG_INVAR, I);
   }
   template<typename S>
   DiagInfo<S> diagonalisations_MPI(const Step &step, const Opch<S> &opch, const Coef<S> &coef, const DiagInfo<S> &diagprev,
                                    const std::vector<Invar> &tasks, const DiagParams &DP, std::shared_ptr<Symmetry<S>> Sym, const Params &P) {
       DiagInfo<S> diagnew;
       send_params(DP);                                   // Synchronise parameters
       std::list<Invar> todo(tasks.begin(), tasks.end()); // List of all the tasks to handle
       std::list<Invar> done;                             // List of finished tasks.
       std::deque<int> nodes(mpiw.size());                // Available computation nodes (including the master, which is always at the head of the deque).
       std::iota(nodes.begin(), nodes.end(), 0);
       nrglog('M', "nrtasks=" << tasks.size() << " nrnodes=" << nodes.size());
       while (!todo.empty()) {
         my_assert(!nodes.empty());
         // i is the node to which the next job will be scheduled. (If a single task is left undone, do it on the master
         // node to avoid the unnecessary network copying.)
         const auto i = todo.size() != 1 ? get_back(nodes) : 0;
         // On master, we take short jobs from the end. On slaves, we take long jobs from the beginning.
         const Invar I = i == 0 ? get_back(todo) : get_front(todo);
         auto h = prepare_task_for_diag(step, I, opch, coef, diagprev, Sym, P); // non-const
         nrglog('M', "Scheduler: job " << I << " (dim=" << h.size1() << ")" << " on node " << i);
         if (i == 0) {
           // On master, diagonalize immediately.
           diagnew[I] = diagonalise<S>(h, DP, myrank());
           nodes.push_back(0);
           done.push_back(I);
         } else {
           mpiw.send(i, std::is_same_v<S, double> ? TAG_DIAG_DBL : TAG_DIAG_CMPL, 0);
           send_matrix<S>(i, h);
           mpiw.send(i, TAG_INVAR, I);
         }
         // Check for terminated jobs
         while (auto status = mpiw.iprobe(boost::mpi::any_source, TAG_EIGEN_VEC)) {
           nrglog('M', "Receiveing results from " << status->source());
           const auto [Irecv, eig] = read_from<S>(status->source());
           diagnew[Irecv] = eig;
           done.push_back(Irecv);
           // The node is now available for new tasks!
           nodes.push_back(status->source());
         }
       }
       // Keep reading results sent from the slave processes until all tasks have been completed.
       while (done.size() != tasks.size()) {
         const auto status = mpiw.probe(boost::mpi::any_source, TAG_EIGEN_VEC);
         const auto [Irecv, eig]  = read_from<S>(status.source());
         diagnew[Irecv] = eig;
         done.push_back(Irecv);
       }
       return diagnew;
     }
   void done() {
     for (auto i = 1; i < mpiw.size(); i++) mpiw.send(i, TAG_EXIT, 0); // notify slaves we are done
   }
};

// Build matrix H(ri;r'i') in each subspace and diagonalize it
template<typename S>
auto diagonalisations(const Step &step, const Opch<S> &opch, const Coef<S> &coef, const DiagInfo<S> &diagprev, 
                      const std::vector<Invar> &tasks, const double diagratio, std::shared_ptr<Symmetry<S>> Sym, MPI &mpi, MemTime &mt, const Params &P) {
  mt.time_it("diag");
  return P.diag_mode == "MPI" ? mpi.diagonalisations_MPI<S>(step, opch, coef, diagprev, tasks, DiagParams(P, diagratio), Sym, P) 
                              : diagonalisations_OpenMP(step, opch, coef, diagprev, tasks, DiagParams(P, diagratio), Sym, P);
}

// Determine the structure of matrices in the new NRG shell
template<typename S>
QSrmax::QSrmax(const DiagInfo<S> &diagprev, std::shared_ptr<Symmetry<S>> Sym) {
  for (const auto &I : new_subspaces(diagprev, Sym))
    (*this)[I] = Rmaxvals{I, Sym->ancestors(I), diagprev, Sym};
}

// Recalculate irreducible matrix elements for Wilson chains.
template<typename S>
void recalc_irreducible(const Step &step, const DiagInfo<S> &diag, const QSrmax &qsrmax, Opch<S> &opch, 
                        std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) {
  mt.time_it("recalc f");
  if (!P.substeps) {
    opch = Sym->recalc_irreduc(step, diag, qsrmax);
  } else {
    const auto [N, M] = step.NM();
    for (const auto i: range0(size_t(P.channels)))
      if (i == M) {
        opch[i] = Sym->recalc_irreduc_substeps(step, diag, qsrmax, i);
      } else {
        for (const auto j: range0(size_t(P.perchannel)))
          opch[i][j] = Sym->recalc_doublet(diag, qsrmax, opch[i][j]);
      }
  }
}

template<typename S>
auto do_diag(const Step &step, IterInfo<S> &iterinfo, const Coef<S> &coef, Stats<S> &stats, const DiagInfo<S> &diagprev,
             QSrmax &qsrmax, std::shared_ptr<Symmetry<S>> Sym, MPI &mpi, MemTime &mt, const Params &P) {
  step.infostring();
  Sym->show_coefficients(step, coef);
  auto tasks = qsrmax.task_list();
  double diagratio = P.diagratio; // non-const
  DiagInfo<S> diag;
  while (true) {
    try {
      if (step.nrg()) {
        if (!(P.resume && int(step.ndx()) <= P.laststored))
          diag = diagonalisations(step, iterinfo.opch, coef, diagprev, tasks, diagratio, Sym, mpi, mt, P); // compute in first run
        else
          diag = DiagInfo<S>(step.ndx(), P, false); // or read from disk
      }
      if (step.dmnrg()) {
        diag = DiagInfo<S>(step.ndx(), P, P.removefiles); // read from disk in second run
        diag.subtract_GS_energy(stats.GS_energy);
      }
      stats.Egs = diag.find_groundstate();
      if (step.nrg()) // should be done only once!
        diag.subtract_Egs(stats.Egs);
      Clusters<S> clusters(diag, P.fixeps);
      truncate_prepare(step, diag, Sym, P);
      break;
    }
    catch (NotEnough &e) {
      fmt::print(fmt::emphasis::bold | fg(fmt::color::yellow), "Insufficient number of states computed.\n");
      if (!(step.nrg() && P.restart)) break;
      diagratio = std::min(diagratio * P.restartfactor, 1.0);
      fmt::print(fmt::emphasis::bold | fg(fmt::color::yellow), "\nRestarting this iteration step. diagratio={}\n\n", diagratio);
    }
  }
  return diag;
}

// Absolute energies. Must be called in the first NRG run after stats.total_energy has been updated, but before
// store_transformations(). absenergyG is updated to its correct values (referrenced to absolute 0) in
// shift_abs_energies().
template<typename S>
void calc_abs_energies(const Step &step, DiagInfo<S> &diag, const Stats<S> &stats) {
  for (auto &eig : diag.eigs()) {
    eig.absenergyN = eig.value_zero * step.scale();        // referenced to the lowest energy in current NRG step (not modified later on)
    eig.absenergy = eig.absenergyN;
    for (auto &x : eig.absenergy) x += stats.total_energy; // absolute energies (not modified later on)
    eig.absenergyG = eig.absenergy;                        // referenced to the absolute 0 (updated by shft_abs_energies())
  }
}

// Perform processing after a successful NRG step. Also called from doZBW() as a final step.
template<typename S>
void after_diag(const Step &step, IterInfo<S> &iterinfo, Stats<S> &stats, DiagInfo<S> &diag, Output<S> &output,
                QSrmax &qsrmax, AllSteps<S> &dm, Oprecalc<S> &oprecalc, std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) {
  stats.total_energy += stats.Egs * step.scale(); // stats.Egs has already been initialized
  std::cout << "Total energy=" << HIGHPREC(stats.total_energy) << "  Egs=" << HIGHPREC(stats.Egs) << std::endl;
  stats.rel_Egs[step.ndx()] = stats.Egs;
  stats.abs_Egs[step.ndx()] = stats.Egs * step.scale();
  stats.energy_offsets[step.ndx()] = stats.total_energy;
  if (step.nrg()) {
    calc_abs_energies(step, diag, stats);  // only in the first run, in the second one the data is loaded from file!
    if (P.dm && !(P.resume && int(step.ndx()) <= P.laststored))
      diag.save(step.ndx(), P);
    perform_basic_measurements(step, diag, Sym, stats, output); // Measurements are performed before the truncation!
  }
  if (!P.ZBW)
    split_in_blocks(diag, qsrmax);
  if (P.do_recalc_all(step.runtype)) { // Either ...
    oprecalc.recalculate_operators(iterinfo, step, diag, qsrmax);
    calculate_spectral_and_expv(step, stats, output, oprecalc, diag, iterinfo, dm, Sym, mt, P);
  }
  if (!P.ZBW)
    diag.truncate_perform();                        // Actual truncation occurs at this point
  dm.store(step.ndx(), diag, qsrmax, step.last());  // Store information about subspaces and states for DM algorithms
  if (!step.last()) {
    recalc_irreducible(step, diag, qsrmax, iterinfo.opch, Sym, mt, P);
    if (P.dump_f) iterinfo.opch.dump();
  }
  if (P.do_recalc_kept(step.runtype)) { // ... or ...
    oprecalc.recalculate_operators(iterinfo, step, diag, qsrmax);
    calculate_spectral_and_expv(step, stats, output, oprecalc, diag, iterinfo, dm, Sym, mt, P);
  }
  if (P.do_recalc_none())  // ... or this
    calculate_spectral_and_expv(step, stats, output, oprecalc, diag, iterinfo, dm, Sym, mt, P);
  if (P.checksumrules) operator_sumrules(iterinfo, Sym);
}

// Perform one iteration step
template<typename S>
auto iterate(const Step &step, IterInfo<S> &iterinfo, const Coef<S> &coef, Stats<S> &stats, const DiagInfo<S> &diagprev,
             Output<S> &output, AllSteps<S> &dm, Oprecalc<S> &oprecalc, std::shared_ptr<Symmetry<S>> Sym, MPI &mpi, MemTime &mt, const Params &P) {
  QSrmax qsrmax{diagprev, Sym};
  auto diag = do_diag(step, iterinfo, coef, stats, diagprev, qsrmax, Sym, mpi, mt, P);
  after_diag(step, iterinfo, stats, diag, output, qsrmax, dm, oprecalc, Sym, mt, P);
  iterinfo.trim_matrices(diag);
  diag.clear_eigenvectors();
  mt.brief_report();
  return diag;
}

// Perform calculations with quantities from 'data' file
template<typename S>
void docalc0(Step &step, const IterInfo<S> &iterinfo, const DiagInfo<S> &diag0, Stats<S> &stats, Output<S> &output, 
             Oprecalc<S> &oprecalc, std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) {
  step.set(P.Ninit - 1); // in the usual case with Ninit=0, this will result in N=-1
  std::cout << std::endl << "Before NRG iteration";
  std::cout << " (N=" << step.N() << ")" << std::endl;
  perform_basic_measurements(step, diag0, Sym, stats, output);
  AllSteps<S> empty_dm(0, 0);
  calculate_spectral_and_expv(step, stats, output, oprecalc, diag0, iterinfo, empty_dm, Sym, mt, P);
  if (P.checksumrules) operator_sumrules(iterinfo, Sym);
}

// doZBW() takes the place of iterate() called from main_loop() in the case of zero-bandwidth calculation.
// It replaces do_diag() and calls after_diag() as the last step.
template<typename S>
auto nrg_ZBW(Step &step, IterInfo<S> &iterinfo, Stats<S> &stats, const DiagInfo<S> &diag0, Output<S> &output, 
             AllSteps<S> &dm, Oprecalc<S> &oprecalc, std::shared_ptr<Symmetry<S>> Sym, MemTime &mt, const Params &P) {
  std::cout << std::endl << "Zero bandwidth calculation" << std::endl;
  step.set_ZBW();
  // --- begin do_diag() equivalent
  DiagInfo<S> diag;
  if (step.nrg())
    diag = diag0;
  if (step.dmnrg()) {
    diag = DiagInfo<S>(step.ndx(), P, P.removefiles);
    diag.subtract_GS_energy(stats.GS_energy);
  }
  stats.Egs = diag.find_groundstate();
  if (step.nrg())      
    diag.subtract_Egs(stats.Egs);
  truncate_prepare(step, diag, Sym, P); // determine # of kept and discarded states
  // --- end do_diag() equivalent
  QSrmax qsrmax{};
  after_diag(step, iterinfo, stats, diag, output, qsrmax, dm, oprecalc, Sym, mt, P);
  return diag;
}

template<typename S>
auto nrg_loop(Step &step, IterInfo<S> &iterinfo, const Coef<S> &coef, Stats<S> &stats, const DiagInfo<S> &diag0,
              Output<S> &output, AllSteps<S> &dm, Oprecalc<S> &oprecalc, std::shared_ptr<Symmetry<S>> Sym, MPI &mpi, MemTime &mt, const Params &P) {
  auto diag = diag0;
  for (step.init(); !step.end(); step.next())
    diag = iterate(step, iterinfo, coef, stats, diag, output, dm, oprecalc, Sym, mpi, mt, P);
  step.set(step.lastndx());
  return diag;
}

#include "mk_sym.h"

// Called immediately after parsing the information about the number of channels from the data file. This ensures
// that Invar can be parsed correctly.
template <typename S>
std::shared_ptr<Symmetry<S>> set_symmetry(const Params &P, Stats<S> &stats) {
  my_assert(P.channels > 0 && P.combs > 0); // must be set at this point
  std::cout << "SYMMETRY TYPE: " << P.symtype.value() << std::endl;
  auto Sym = get<S>(P.symtype.value(), P, stats.td.allfields);
  Sym->load();
  Sym->erase_first();
  return Sym;
}

template <typename S> class NRG_calculation {
private:
  MPI mpi;
  Params P;
  Stats<S> stats;
  MemTime mt; // memory and timing statistics
public:
  auto run_nrg(Step &step, IterInfo<S> &iterinfo, const Coef<S> &coef, Stats<S> &stats, const DiagInfo<S> &diag0,
               AllSteps<S> &dm, std::shared_ptr<Symmetry<S>> Sym) {
    diag0.states_report(Sym->multfnc());
    auto oprecalc = Oprecalc<S>(step.runtype, iterinfo, Sym, mt, P);
    auto output = Output<S>(step.runtype, iterinfo, stats, P);
    // If calc0=true, a calculation of TD quantities is performed before starting the NRG iteration.
    if (step.nrg() && P.calc0 && !P.ZBW)
      docalc0(step, iterinfo, diag0, stats, output, oprecalc, Sym, mt, P);
    auto diag = P.ZBW ? nrg_ZBW(step, iterinfo, stats, diag0, output, dm, oprecalc, Sym, mt, P)
      : nrg_loop(step, iterinfo, coef, stats, diag0, output, dm, oprecalc, Sym, mpi, mt, P);
    fmt::print(fmt::emphasis::bold | fg(fmt::color::red), FMT_STRING("\nTotal energy: {:.18}\n"), stats.total_energy);
    stats.GS_energy = stats.total_energy;
    if (step.nrg() && P.dumpsubspaces) dm.dump_subspaces();
    fmt::print("\n** Iteration completed.\n\n");
    return diag;
  }
  NRG_calculation(MPI &mpi, const Workdir &workdir, const bool embedded) : mpi(mpi), P("param", "param", workdir, embedded), stats(P) {
    auto [diag0, iterinfo, coef, Sym] = read_data<S>(P, stats);
    Step step{P, RUNTYPE::NRG};
    AllSteps<S> dm(P.Ninit, P.Nlen);
    auto diag = run_nrg(step, iterinfo, coef, stats, diag0, dm, Sym);
    if (std::string(P.stopafter) == "nrg") exit1("*** Stopped after the first sweep.");
    dm.shift_abs_energies(stats.GS_energy); // we call this here, to enable a file dump
    if (P.dumpabsenergies)
      dm.dump_all_absolute_energies();
    if (P.dm) {
      if (P.need_rho()) {
        auto rho = init_rho(step, diag, Sym);
        rho.save(step.lastndx(), P, fn_rho);
        if (!P.ZBW) calc_densitymatrix(rho, dm, Sym, mt, P);
      }
      if (P.need_rhoFDM()) {
        calc_ZnD(dm, stats, Sym, P.T);
        if (P.logletter('w')) 
          report_ZnD(stats, P);
        fdm_thermodynamics(dm, stats, Sym, P.T);
        auto rhoFDM = init_rho_FDM(step.lastndx(), dm, stats, Sym, P.T);
        rhoFDM.save(step.lastndx(), P, fn_rhoFDM);
        if (!P.ZBW) calc_fulldensitymatrix(step, rhoFDM, dm, stats, Sym, mt, P);
      }
      if (std::string(P.stopafter) == "rho") exit1("*** Stopped after the DM calculation.");
      auto [diag0_dm, iterinfo_dm, coef_dm, Sym_dm] = read_data<S>(P, stats);
      Step step_dmnrg{P, RUNTYPE::DMNRG};
      run_nrg(step_dmnrg, iterinfo_dm, coef_dm, stats, diag0_dm, dm, Sym_dm);
      my_assert(num_equal(stats.GS_energy, stats.total_energy));
    }
  }
  ~NRG_calculation() {
    if (!P.embedded) mt.report(); // only when running as a stand-alone application
    if (P.done) { std::ofstream D("DONE"); } // Indicate completion by creating a flag file
  }
};

// Returns true if the data file contains complex values
inline bool complex_data(const std::string filename = "data") {
  std::ifstream F(filename);
  if (!F) throw std::runtime_error("Can't load initial data.");
  std::string l;
  std::getline(F, l);
  std::getline(F, l);
  std::getline(F, l); // third line
  const auto pos = l.find("COMPLEX"); 
  return pos != std::string::npos;
} 

#endif
