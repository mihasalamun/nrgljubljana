class SymmetryNONE : public Symmetry {
  public:
   template<typename ... Args> SymmetryNONE(Args&& ... args) : Symmetry(std::forward<Args>(args)...) {
     InvarStructure InvStruc[] = {
       {"x", additive} // dummy quantum number
     };
     initInvar(InvStruc, ARRAYLENGTH(InvStruc));
     InvarSinglet = Invar(0);
   }

  void load() override {
    switch (P.channels) {
      case 1:
#include "none/none-1ch-In2.dat"
#include "none/none-1ch-QN.dat"
        break;
      case 2:
#include "none/none-2ch-In2.dat"
#include "none/none-2ch-QN.dat"
        break;
      default: my_assert_not_reached();
    }
  }

  void make_matrix_polarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch, const Coef &coef);
  void make_matrix_nonpolarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch, const Coef &coef);
  void calculate_TD(const Step &step, const DiagInfo &diag, const Stats &stats, double factor) override {};

  DECL;
  HAS_DOUBLET;
  HAS_GLOBAL;
};

#undef OFFDIAG_CR_DO
#undef OFFDIAG_CR_UP

#define OFFDIAG_CR_DO(i, j, ch, factor) offdiag_function(step, i, j, ch, 0, t_matel(factor) * coef.xi(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_CR_UP(i, j, ch, factor) offdiag_function(step, i, j, ch, 1, t_matel(factor) * coef.xi(step.N(), ch), h, qq, In, opch)

#undef ISOSPINX
#define ISOSPINX(i, j, ch, factor) diag_offdiag_function(step, i, j, ch, t_matel(factor) * 2.0 * coef.delta(step.N() + 1, ch), h, qq)

#undef DIAG
#define DIAG(i, ch, number) diag_function(step, i, ch, number, coef.zeta(step.N() + 1, ch), h, qq)

void SymmetryNONE::make_matrix_nonpolarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch, const Coef &coef) {
  switch (P.channels) {
    case 1:
#include "none/none-1ch-offdiag-CR-UP.dat"
#include "none/none-1ch-offdiag-CR-DO.dat"
#include "none/none-1ch-diag.dat"
#include "none/none-1ch-Ixtot.dat"
      break;
    case 2:
#include "none/none-2ch-offdiag-CR-UP.dat"
#include "none/none-2ch-offdiag-CR-DO.dat"
#include "none/none-2ch-diag.dat"
#include "none/none-2ch-Ixtot.dat"
      break;
    default: my_assert_not_reached();
  }
}

#undef OFFDIAG_CR_DO
#undef OFFDIAG_CR_UP

#define OFFDIAG_CR_DO(i, j, ch, factor) offdiag_function(step, i, j, ch, 0, t_matel(factor) * coef.xiDOWN(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_CR_UP(i, j, ch, factor) offdiag_function(step, i, j, ch, 1, t_matel(factor) * coef.xiUP(step.N(), ch), h, qq, In, opch)

#undef ISOSPINX
#define ISOSPINX(i, j, ch, factor) diag_offdiag_function(step, i, j, ch, t_matel(factor) * 2.0 * coef.delta(step.N() + 1, ch), h, qq)

#undef DIAG_UP
#define DIAG_UP(i, j, ch, number) diag_function(step, i, ch, number, coef.zetaUP(step.N() + 1, ch), h, qq)

#undef DIAG_DOWN
#define DIAG_DOWN(i, j, ch, number) diag_function(step, i, ch, number, coef.zetaDOWN(step.N() + 1, ch), h, qq)

void SymmetryNONE::make_matrix_polarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch, const Coef &coef) {
  switch (P.channels) {
    case 1:
#include "none/none-1ch-offdiag-CR-UP.dat"
#include "none/none-1ch-offdiag-CR-DO.dat"
#include "none/none-1ch-diag-UP.dat"
#include "none/none-1ch-diag-DOWN.dat"
#include "none/none-1ch-Ixtot.dat"
      break;
    case 2:
#include "none/none-2ch-offdiag-CR-UP.dat"
#include "none/none-2ch-offdiag-CR-DO.dat"
#include "none/none-2ch-diag-UP.dat"
#include "none/none-2ch-diag-DOWN.dat"
#include "none/none-2ch-Ixtot.dat"
      break;
    default: my_assert_not_reached();
  }
}

void SymmetryNONE::make_matrix(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch, const Coef &coef) {
  if (P.polarized)
    make_matrix_polarized(h, step, qq, I, In, opch, coef);
  else
    make_matrix_nonpolarized(h, step, qq, I, In, opch, coef);
}

#include "nrg-recalc-NONE.cc"
