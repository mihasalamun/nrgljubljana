// *** WARNING!!! Modify nrg-recalc-SL3.cc.m4, not nrg-recalc-SL3.cc !!!

// Quantum number dependant recalculation routines
// Rok Zitko, rok.zitko@ijs.si, June 2006, Nov 2007, Oct 2010
// This file pertains to the spinless-fermions code.

// m4 macros for nrg-recalc-*.cc files
// Rok Zitko, rok.zitko@ijs.si, 2007-2020










  





template<typename SC>
MatrixElements_tmpl<SC> SymmetrySL3_tmpl<SC>::recalc_doublet(const DiagInfo_tmpl<SC> &diag, const QSrmax &qsrmax, const MatrixElements_tmpl<SC> &cold) {
  MatrixElements_tmpl<SC> cnew;
  for(const auto &[I1, eig]: diag) {
    Number q11 = I1.get("Q1");
    Number q21 = I1.get("Q2");
    Number q31 = I1.get("Q3");
    Invar Ip   = Invar(q11 - 1, q21, q31); // This is a channel 1 operator
    {
  nrglog('f', "RECALC(fn=" << "sl3/sl3-3ch-doublet.dat" << ", Iop=" << Invar(1, 0, 0) << ")");
  auto II = Twoinvar(I1, Ip);
  if (diag.count(I1) && diag.count(Ip)) {
    if (diag.at(I1).getnrstored() && diag.at(Ip).getnrstored()) {
      [&]() ATTRIBUTE_NO_SANITIZE_DIV_BY_ZERO { 
        Recalc recalc_table[] = {
#include "sl3/sl3-3ch-doublet.dat"
        };
        cnew[II] = this->recalc_general(diag, qsrmax, cold, I1, Ip, recalc_table, ARRAYLENGTH(recalc_table), Invar(1, 0, 0));
      }(); // immediately executed lambda
    }
  }
};
  }
  return cnew;
}

template<typename SC>
Opch_tmpl<SC> SymmetrySL3_tmpl<SC>::recalc_irreduc(const Step &step, const DiagInfo_tmpl<SC> &diag, const QSrmax &qsrmax) {
  Opch_tmpl<SC> opch = newopch<SC>(P);
  for(const auto &[Ip, eig]: diag) {
    Number q1p = Ip.get("Q1");
    Number q2p = Ip.get("Q2");
    Number q3p = Ip.get("Q3");

    Invar I1;

    I1 = Invar(q1p + 1, q2p, q3p);
    {
  nrglog('f', "RECALC_F(fn=" << "sl3/sl3-3ch-a.dat" << ", ch=" << 0 << ")");
  auto II = Twoinvar(I1, Ip);
  if (diag.count(I1) && diag.count(Ip) && this->recalc_f_coupled(I1, Ip, this->Invar_f)) {
    if (diag.at(I1).getnrstored() && diag.at(Ip).getnrstored()) {
      [&]() ATTRIBUTE_NO_SANITIZE_DIV_BY_ZERO { 
        Recalc_f recalc_table[] = {
#include "sl3/sl3-3ch-a.dat"
        };
        opch[0][0][II] = this->recalc_f(diag, qsrmax, I1, Ip, recalc_table, ARRAYLENGTH(recalc_table));
      }();
    }
  }
};

    I1 = Invar(q1p, q2p + 1, q3p);
    {
  nrglog('f', "RECALC_F(fn=" << "sl3/sl3-3ch-b.dat" << ", ch=" << 1 << ")");
  auto II = Twoinvar(I1, Ip);
  if (diag.count(I1) && diag.count(Ip) && this->recalc_f_coupled(I1, Ip, this->Invar_f)) {
    if (diag.at(I1).getnrstored() && diag.at(Ip).getnrstored()) {
      [&]() ATTRIBUTE_NO_SANITIZE_DIV_BY_ZERO { 
        Recalc_f recalc_table[] = {
#include "sl3/sl3-3ch-b.dat"
        };
        opch[1][0][II] = this->recalc_f(diag, qsrmax, I1, Ip, recalc_table, ARRAYLENGTH(recalc_table));
      }();
    }
  }
};

    I1 = Invar(q1p, q2p, q3p + 1);
    {
  nrglog('f', "RECALC_F(fn=" << "sl3/sl3-3ch-c.dat" << ", ch=" << 2 << ")");
  auto II = Twoinvar(I1, Ip);
  if (diag.count(I1) && diag.count(Ip) && this->recalc_f_coupled(I1, Ip, this->Invar_f)) {
    if (diag.at(I1).getnrstored() && diag.at(Ip).getnrstored()) {
      [&]() ATTRIBUTE_NO_SANITIZE_DIV_BY_ZERO { 
        Recalc_f recalc_table[] = {
#include "sl3/sl3-3ch-c.dat"
        };
        opch[2][0][II] = this->recalc_f(diag, qsrmax, I1, Ip, recalc_table, ARRAYLENGTH(recalc_table));
      }();
    }
  }
};
  }
  return opch;
}

#undef QTOT
#define QTOT(i1, ip, ch, value) this->recalc1_global(diag, qsrmax, I1, cn, i1, ip, value)

#undef N1
#define N1(i1, ip, ch, value) this->recalc1_global(diag, qsrmax, I1, cn, i1, ip, value)

#undef N2
#define N2(i1, ip, ch, value) this->recalc1_global(diag, qsrmax, I1, cn, i1, ip, value)

#undef N3
#define N3(i1, ip, ch, value) this->recalc1_global(diag, qsrmax, I1, cn, i1, ip, value)

template<typename SC>
void SymmetrySL3_tmpl<SC>::recalc_global(const Step &step, const DiagInfo_tmpl<SC> &diag, const QSrmax &qsrmax, const std::string name, MatrixElements_tmpl<SC> &cnew) {
  if (name == "Qtot") {
    for(const auto &[I1, eig]: diag) {
      const Twoinvar II = {I1, I1};
      Matrix &cn        = cnew[II];
#include "sl3/sl3-3ch-qtot.dat"
    }
    return;
  }

  if (name == "N1") {
    for(const auto &[I1, eig]: diag) {
      const Twoinvar II = {I1, I1};
      Matrix &cn        = cnew[II];
#include "sl3/sl3-3ch-N1.dat"
    }
    return;
  }

  if (name == "N2") {
    for(const auto &[I1, eig]: diag) {
      const Twoinvar II = {I1, I1};
      Matrix &cn        = cnew[II];
#include "sl3/sl3-3ch-N2.dat"
    }
    return;
  }

  if (name == "N3") {
    for(const auto &[I1, eig]: diag) {
      const Twoinvar II = {I1, I1};
      Matrix &cn        = cnew[II];
#include "sl3/sl3-3ch-N3.dat"
    }
    return;
  }

  my_assert_not_reached();
}
