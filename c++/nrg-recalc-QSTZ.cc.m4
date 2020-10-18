// *** WARNING!!! Modify nrg-recalc-QSTZ.cc.m4, not nrg-recalc-QSTZ.cc !!!

// Quantum number dependant recalculation routines
// Rok Zitko, rok.zitko@ijs.si, Feb 2016
// This file pertains to (Q,S,Tz) subspaces

include(recalc-macros.m4)

template<typename SC>
MatrixElements_tmpl<SC> SymmetryQSTZ_tmpl<SC>::recalc_doublet(const DiagInfo_tmpl<SC> &diag, const QSrmax &qsrmax, const MatrixElements_tmpl<SC> &cold) {
  MatrixElements_tmpl<SC> cnew;
  for(const auto &[I1, eig]: diag) {
    Number q1   = I1.get("Q");
    Sspin ss1   = I1.get("SS");
    Tangmom tz1 = I1.get("TZ");
    Invar Ip;

    // Two different lengths: D_3CH_a and D_3CH_b

    // Invar(1,2,+-1,0) is correct. 1 = add charge, 2 = doublet,
    // 1 = triplet (because working with abs orbital momentum QNs)

    Ip = Invar(q1 - 1, ss1 + 1, tz1 - 1);
    RECALC_TAB("qstz/qstz-doubletp-1.dat", Invar(1, 2, +1));

    Ip = Invar(q1 - 1, ss1 - 1, tz1 - 1);
    RECALC_TAB("qstz/qstz-doubletm-1.dat", Invar(1, 2, +1));

    Ip = Invar(q1 - 1, ss1 + 1, tz1);
    RECALC_TAB("qstz/qstz-doubletp0.dat", Invar(1, 2, 0));

    Ip = Invar(q1 - 1, ss1 - 1, tz1);
    RECALC_TAB("qstz/qstz-doubletm0.dat", Invar(1, 2, 0));

    Ip = Invar(q1 - 1, ss1 + 1, tz1 + 1);
    RECALC_TAB("qstz/qstz-doubletp+1.dat", Invar(1, 2, -1));

    Ip = Invar(q1 - 1, ss1 - 1, tz1 + 1);
    RECALC_TAB("qstz/qstz-doubletm+1.dat", Invar(1, 2, -1));
  }
  return cnew;
}

// ch=1 <-> Tz=+1
// ch=2 <-> Tz=0
// ch=3 <-> Tz=-1

template<typename SC>
Opch_tmpl<SC> SymmetryQSTZ_tmpl<SC>::recalc_irreduc(const Step &step, const DiagInfo_tmpl<SC> &diag, const QSrmax &qsrmax) {
  Opch_tmpl<SC> opch = newopch<SC>(P);
  for(const auto &[Ip, eig]: diag) {
    Number qp   = Ip.get("Q");
    Sspin ssp   = Ip.get("SS");
    Tangmom tzp = Ip.get("TZ");
    Invar I1;

    // The different files just correspond to contributions computed
    // for various d[CR,sz,tz] operators.
    // Check: there should not be any lines with equal subspaces
    // indexes in different files!! That's indeed the case for the
    // generated files for symtype=QST.
    I1 = Invar(qp + 1, ssp + 1, tzp + 1);
    RECALC_F_TAB("qstz/qstz-spinup+1.dat", 0);

    I1 = Invar(qp + 1, ssp + 1, tzp);
    RECALC_F_TAB("qstz/qstz-spinup0.dat", 0);

    I1 = Invar(qp + 1, ssp + 1, tzp - 1);
    RECALC_F_TAB("qstz/qstz-spinup-1.dat", 0);

    I1 = Invar(qp + 1, ssp - 1, tzp + 1);
    RECALC_F_TAB("qstz/qstz-spindo+1.dat", 0);

    I1 = Invar(qp + 1, ssp - 1, tzp);
    RECALC_F_TAB("qstz/qstz-spindo0.dat", 0);

    I1 = Invar(qp + 1, ssp - 1, tzp - 1);
    RECALC_F_TAB("qstz/qstz-spindo-1.dat", 0);
  }
  return opch;
}

template<typename SC>
MatrixElements_tmpl<SC> SymmetryQSTZ_tmpl<SC>::recalc_triplet(const DiagInfo_tmpl<SC> &diag, const QSrmax &qsrmax, const MatrixElements_tmpl<SC> &cold) {
  MatrixElements_tmpl<SC> cnew;
  for(const auto &[I1, eig]: diag) {
    Number q1   = I1.get("Q");
    Sspin ss1   = I1.get("SS");
    Tangmom tz1 = I1.get("TZ");
    Invar Ip;

    Ip = Invar(q1, ss1, tz1);
    RECALC_TAB("qstz/qstz-triplets.dat", Invar(0, 3, 0));

    Ip = Invar(q1, ss1 + 2, tz1);
    RECALC_TAB("qstz/qstz-tripletp.dat", Invar(0, 3, 0));

    Ip = Invar(q1, ss1 - 2, tz1);
    RECALC_TAB("qstz/qstz-tripletm.dat", Invar(0, 3, 0));
  }
  return cnew;
}
