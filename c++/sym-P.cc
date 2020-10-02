class SymmetryP : public Symmetry {
  public:
  SymmetryP() : Symmetry() { all_syms["P"] = this; }

  void init() override {
    InvarStructure InvStruc[] = {
       {"P", multiplicative} // fermion parity
    };
    initInvar(InvStruc, ARRAYLENGTH(InvStruc));
    InvarSinglet = Invar(1);
  }

  void load() override {
    switch (channels) {
      case 1:
#include "p/p-1ch-In2.dat"
#include "p/p-1ch-QN.dat"
        break;

      case 2:
#include "p/p-2ch-In2.dat"
#include "p/p-2ch-QN.dat"
        break;

      default: my_assert_not_reached();
    }
  }

  void makematrix_polarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch);
  void makematrix_nonpolarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch);

  void calculate_TD(const Step &step, const DiagInfo &diag, double factor) override{};

  bool triangle_inequality(const Invar &I1, const Invar &I2, const Invar &I3) override { return z2_equality(I1.get("P"), I2.get("P"), I3.get("P")); }

  DECL;
  HAS_DOUBLET;
  HAS_GLOBAL;
};

Symmetry *SymP = new SymmetryP;

#undef OFFDIAG_CR_DO
#undef OFFDIAG_CR_UP
#undef OFFDIAG_AN_DO
#undef OFFDIAG_AN_UP

#define OFFDIAG_CR_DO(i, j, ch, factor) offdiag_function(step, i, j, ch, 0, t_matel(factor) * xi(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_CR_UP(i, j, ch, factor) offdiag_function(step, i, j, ch, 1, t_matel(factor) * xi(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_AN_DO(i, j, ch, factor) offdiag_function(step, i, j, ch, 2, t_matel(factor) * xi(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_AN_UP(i, j, ch, factor) offdiag_function(step, i, j, ch, 3, t_matel(factor) * xi(step.N(), ch), h, qq, In, opch)

#undef ISOSPINX
#define ISOSPINX(i, j, ch, factor) diag_offdiag_function(step, i, j, ch, t_matel(factor) * 2.0 * delta(step.N() + 1, ch), h, qq)

#undef DIAG
#define DIAG(i, ch, number) diag_function(step, i, ch, number, zeta(step.N() + 1, ch), h, qq)

void SymmetryP::makematrix_nonpolarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch) {
  switch (channels) {
    case 1:
#include "p/p-1ch-offdiag-CR-UP.dat"
#include "p/p-1ch-offdiag-CR-DO.dat"
#include "p/p-1ch-offdiag-AN-UP.dat"
#include "p/p-1ch-offdiag-AN-DO.dat"
#include "p/p-1ch-diag.dat"
#include "p/p-1ch-Ixtot.dat"
      break;

    case 2:
#include "p/p-2ch-offdiag-CR-UP.dat"
#include "p/p-2ch-offdiag-CR-DO.dat"
#include "p/p-2ch-offdiag-AN-UP.dat"
#include "p/p-2ch-offdiag-AN-DO.dat"
#include "p/p-2ch-diag.dat"
#include "p/p-2ch-Ixtot.dat"
      break;

    default: my_assert_not_reached();
  }
}

#undef OFFDIAG_CR_DO
#undef OFFDIAG_CR_UP
#undef OFFDIAG_AN_DO
#undef OFFDIAG_AN_UP

#define OFFDIAG_CR_DO(i, j, ch, factor) offdiag_function(step, i, j, ch, 0, t_matel(factor) * xiDOWN(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_CR_UP(i, j, ch, factor) offdiag_function(step, i, j, ch, 1, t_matel(factor) * xiUP(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_AN_DO(i, j, ch, factor) offdiag_function(step, i, j, ch, 2, t_matel(factor) * xiDOWN(step.N(), ch), h, qq, In, opch)
#define OFFDIAG_AN_UP(i, j, ch, factor) offdiag_function(step, i, j, ch, 3, t_matel(factor) * xiUP(step.N(), ch), h, qq, In, opch)

#undef ISOSPINX
#define ISOSPINX(i, j, ch, factor) diag_offdiag_function(step, i, j, ch, t_matel(factor) * 2.0 * delta(step.N() + 1, ch), h, qq)

#undef DIAG_UP
#define DIAG_UP(i, j, ch, number) diag_function(step, i, ch, number, zetaUP(step.N() + 1, ch), h, qq)

#undef DIAG_DOWN
#define DIAG_DOWN(i, j, ch, number) diag_function(step, i, ch, number, zetaDOWN(step.N() + 1, ch), h, qq)

void SymmetryP::makematrix_polarized(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch) {
  switch (channels) {
    case 1:
#include "p/p-1ch-offdiag-CR-UP.dat"
#include "p/p-1ch-offdiag-CR-DO.dat"
#include "p/p-1ch-offdiag-AN-UP.dat"
#include "p/p-1ch-offdiag-AN-DO.dat"
#include "p/p-1ch-diag-UP.dat"
#include "p/p-1ch-diag-DOWN.dat"
#include "p/p-1ch-Ixtot.dat"
      break;

    case 2:
#include "p/p-2ch-offdiag-CR-UP.dat"
#include "p/p-2ch-offdiag-CR-DO.dat"
#include "p/p-2ch-offdiag-AN-UP.dat"
#include "p/p-2ch-offdiag-AN-DO.dat"
#include "p/p-2ch-diag-UP.dat"
#include "p/p-2ch-diag-DOWN.dat"
#include "p/p-2ch-Ixtot.dat"
      break;

    default: my_assert_not_reached();
  }
}

void SymmetryP::makematrix(Matrix &h, const Step &step, const Rmaxvals &qq, const Invar &I, const InvarVec &In, const Opch &opch) {
  if (P.polarized) {
    makematrix_polarized(h, step, qq, I, In, opch);
  } else {
    makematrix_nonpolarized(h, step, qq, I, In, opch);
  }
}

#include "nrg-recalc-P.cc"
