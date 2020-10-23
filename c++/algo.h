#ifndef _algo_h_
#define _algo_h_

#include <string>
#include "traits.h"
#include "params.h"
#include "eigen.h"
#include "operators.h"
#include "invar.h"
#include "step.h"
#include "stats.h"

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

#endif