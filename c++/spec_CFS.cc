// Choose one of the following two! OLD is the non-optimized code by Rok Zitko, OPTIMIZED is the hand-tuned code
// contributed by Markus Greger. The optimized code is faster by an order of magnitude!

class Algo_CFSls : virtual public Algo {
 public:
   Algo_CFSls(const Params &P) : Algo(P) {}
   spCS_t make_cs(const BaseSpectrum &) override { return make_shared<ChainSpectrumBinning>(P); }
   void calc(const Step &step, const Eigen &, const Eigen &, const Matrix &, const Matrix &, const BaseSpectrum &, t_factor, spCS_t, const Invar &,
             const Invar &, const DensMatElements &, const Stats &stats) const override;
   string name() override { return "CFSls"; }
   string merge() override { return "CFS"; }
   string rho_type() override { return "rho"; }
};

class Algo_CFSgt : virtual public Algo {
 public:
   Algo_CFSgt(const Params &P) : Algo(P) {}
   spCS_t make_cs(const BaseSpectrum &) override { return make_shared<ChainSpectrumBinning>(P); }
   void calc(const Step &step, const Eigen &, const Eigen &, const Matrix &, const Matrix &, const BaseSpectrum &, t_factor, spCS_t, const Invar &,
             const Invar &, const DensMatElements &, const Stats &stats) const override;
   string name() override { return "CFSgt"; }
   string merge() override { return "CFS"; }
   string rho_type() override { return "rho"; }
};

class Algo_CFS : public Algo_CFSls, public Algo_CFSgt {
 public:
   Algo_CFS(const Params &P) : Algo(P), Algo_CFSls(P), Algo_CFSgt(P) {}
   spCS_t make_cs(const BaseSpectrum &) override { return make_shared<ChainSpectrumBinning>(P); }
   void calc(const Step &step, const Eigen &diagIp, const Eigen &diagI1, const Matrix &op1II, const Matrix &op2II, const BaseSpectrum &bs, t_factor spinfactor,
             spCS_t cs, const Invar &Ip, const Invar &I1, const DensMatElements &rho, const Stats &stats) const override {
               Algo_CFSgt::calc(step, diagIp, diagI1, op1II, op2II, bs, spinfactor, cs, Ip, I1, rho, stats);
               Algo_CFSls::calc(step, diagIp, diagI1, op1II, op2II, bs, spinfactor, cs, Ip, I1, rho, stats);
             }
   string name() override { return "CFS"; }
   string merge() override { return "CFS"; }
   string rho_type() override { return "rho"; }
};

//#define Algo_CFS_OLD
#define Algo_CFS_OPTIMIZED

// Cf. Peters, Pruschke, Anders, Phys. Rev. B 74, 245113 (2006).

#if defined(NRG_COMPLEX) || defined(Algo_CFS_OLD)
void Algo_CFSls::calc(const Step &step, const Eigen &diagIp, const Eigen &diagI1, const Matrix &op1II, const Matrix &op2II, const BaseSpectrum &bs, t_factor spinfactor,
                      spCS_t cs, const Invar &Ip, const Invar &I1, const DensMatElements &rho, const Stats &stats) const {
  double sign = (bs.mt == matstype::bosonic ? S_BOSONIC : S_FERMIONIC);
  const Matrix &rhoNIp = rho.at(Ip);
  const Matrix &rhoNI1 = rho.at(I1);
  auto dimp            = rhoNIp.size1();
  auto dim1            = rhoNI1.size1();
  // Convention: k-loops over retained states, l-loop over discarded states.
  // i-term, Eq. (11). This part is analogous to that for Algo_FT, i.e., it has the form of the usual Lehmann
  // representation.
  if (step.last()) {
    my_assert(dim1 == diagI1.getnrstored());
    my_assert(dimp == diagIp.getnrstored());
    dim1 = diagI1.getnrstored(); // override  
    dimp = diagIp.getnrstored();
    for (size_t r1 = 0; r1 < dim1; r1++) {
      const t_eigen E1 = diagI1.value_zero(r1);
      for (size_t rp = 0; rp < dimp; rp++) {
        const t_eigen Ep = diagIp.value_zero(rp);
        DELTA d;
        d.energy = E1 - Ep;
        d.weight = (spinfactor / stats.Zft) * CONJ_ME(op1II(r1, rp)) * op2II(r1, rp) * exp(-E1 * step.scT()) * (-sign);
        cs->add(step.scale() * d.energy, d.weight);
      }
    }
  } else {
    // iii-term, Eq. (16), positive frequency excitations
    const size_t dimA = diagI1.getnrstored();
    for (size_t rl = dim1; rl < dimA; rl++) {
      const t_eigen El = diagI1.value_zero(rl);
      for (size_t rk = 0; rk < dimp; rk++) {
        const t_eigen Ek = diagIp.value_zero(rk);
        DELTA d;
        d.energy = El - Ek;
        my_assert(d.energy >= 0.0); // always positive!
        weight_bucket sum;
        for (size_t rkp = 0; rkp < dimp; rkp++) sum += op2II(rl, rkp) * rhoNIp(rkp, rk);
        d.weight = spinfactor * CONJ_ME(op1II(rl, rk)) * t_weight(sum) * (-sign);
        cs->add(step.scale() * d.energy, d.weight);
      }
    }
  } // if (last)
}

void Algo_CFSgt::calc(const Step &step, const Eigen &diagIp, const Eigen &diagI1, const Matrix &op1II, const Matrix &op2II, const BaseSpectrum &bs, t_factor spinfactor,
                      spCS_t cs, const Invar &Ip, const Invar &I1, const DensMatElements &rho, const Stats &stats) const {
  const Matrix &rhoNIp = rho.at(Ip);
  const Matrix &rhoNI1 = rho.at(I1);
  auto dimp            = rhoNIp.size1();
  auto dim1            = rhoNI1.size1();
  // Convention: k-loops over retained states, l-loop over discarded states.
  // i-term, Eq. (11).
  if (step.last()) {
    my_assert(dim1 == diagI1.getnrstored());
    my_assert(dimp == diagIp.getnrstored());
    dim1 = diagI1.getnrstored();
    dimp = diagIp.getnrstored();
    for (size_t r1 = 0; r1 < dim1; r1++) {
      const t_eigen E1 = diagI1.value_zero(r1);
      for (size_t rp = 0; rp < dimp; rp++) {
        const t_eigen Ep = diagIp.value_zero(rp);
        DELTA d;
        d.energy = E1 - Ep;
        d.weight = (spinfactor / stats.Zft) * CONJ_ME(op1II(r1, rp)) * op2II(r1, rp) * exp(-Ep * step.scT());
        cs->add(step.scale() * d.energy, d.weight);
      }
    }
  } else {
    // ii-term, Eq. (15), negative frequency excitations
    for (size_t rk = 0; rk < dim1; rk++) {
      const t_eigen Ek  = diagI1.value_zero(rk);
      const size_t dimB = diagIp.getnrstored();
      for (size_t rl = dimp; rl < dimB; rl++) {
        const t_eigen El = diagIp.value_zero(rl);
        DELTA d;
        d.energy = Ek - El;
        my_assert(d.energy <= 0.0); // always negative!
        weight_bucket sum;
        for (size_t rkp = 0; rkp < dim1; rkp++) sum += CONJ_ME(op1II(rkp, rl)) * rhoNI1(rkp, rk);
        d.weight = spinfactor * t_weight(sum) * op2II(rk, rl);
        cs->add(step.scale() * d.energy, d.weight);
      }
    }
  } // if (last)
}
#endif

// Based on the implementation by Markus Greger.
#if defined(NRG_REAL) && defined(Algo_CFS_OPTIMIZED)
void Algo_CFSls::calc(const Step &step, const Eigen &diagIp, const Eigen &diagI1, const Matrix &op1II, const Matrix &op2II, const BaseSpectrum &bs, double spinfactor,
                      spCS_t cs, const Invar &Ip, const Invar &I1, const DensMatElements &rho, const Stats &stats) const {
  double sign = (bs.mt == matstype::bosonic ? S_BOSONIC : S_FERMIONIC);
  const Matrix &rhoNIp = rho.at(Ip);
  const Matrix &rhoNI1 = rho.at(I1);
  auto dimp            = rhoNIp.size1();
  auto dim1            = rhoNI1.size1();
  // Convention: k-loops over retained states, l-loop over discarded
  // states.
  // i-term, Eq. (11).
  if (step.last()) {
    my_assert(dim1 == diagI1.getnrkept());
    my_assert(dimp == diagIp.getnrkept());
    dim1 = diagI1.getnrstored();
    dimp = diagIp.getnrstored();
    for (size_t r1 = 0; r1 < dim1; r1++) {
      const double E1 = diagI1.value_zero(r1);
      for (size_t rp = 0; rp < dimp; rp++) {
        const double Ep = diagIp.value_zero(rp);
        double d_energy = E1 - Ep;
        double d_weight = (spinfactor / stats.Zft) * op1II(r1, rp) * op2II(r1, rp) * exp(-E1 * step.scT()) * (-sign);
        cs->add(step.scale() * d_energy, d_weight);
      }
    }
  } else {
    // iii-term, Eq. (16), positive frequency excitations
    const size_t dimA     = diagI1.getnrstored();
    auto energies_beginIp = begin(diagIp.value_zero);
    auto energies_beginI1 = begin(diagI1.value_zero);
    if (dimA && dimp) {
      Matrix op2II_m_rho;
      const ublas::matrix_range<const Matrix> op2II_TK(op2II, ublas::range(0, op2II.size1()), ublas::range(0, rhoNIp.size1()));
      op2II_m_rho = Matrix(op2II_TK.size1(), rhoNIp.size2());
      atlas::gemm(CblasNoTrans, CblasNoTrans, 1.0, op2II_TK, rhoNIp, 0.0, op2II_m_rho); // rhoNEW <- rhoNEW + factor T U
      for (size_t rl = dim1; rl < dimA; rl++) {
        const double El = *(energies_beginI1 + rl);
        for (size_t rk = 0; rk < dimp; rk++) {
          const double Ek       = *(energies_beginIp + rk);
          const double d_energy = El - Ek;
          const double sum      = op2II_m_rho(rl, rk);
          const double d_weight = spinfactor * op1II(rl, rk) * sum * (-sign);
          cs->add(step.scale() * d_energy, d_weight);
        }
      }
    }
  } // if (last)
}

void Algo_CFSgt::calc(const Step &step, const Eigen &diagIp, const Eigen &diagI1, const Matrix &op1II, const Matrix &op2II, const BaseSpectrum &bs, double spinfactor,
                      spCS_t cs, const Invar &Ip, const Invar &I1, const DensMatElements &rho, const Stats &stats) const {
  const Matrix &rhoNIp = rho.at(Ip);
  const Matrix &rhoNI1 = rho.at(I1);
  auto dimp            = rhoNIp.size1();
  auto dim1            = rhoNI1.size1();
  // Convention: k-loops over retained states, l-loop over discarded states.
  // i-term, Eq. (11).
  if (step.last()) {
    cout << "dim1=" << dim1 << endl;
    cout << "nr1=" << diagI1.getnrstored() << endl;
    cout << "nrc1=" << diagI1.getnrcomputed() << endl;
    cout << "nrkept1=" << diagI1.getnrkept() << endl;
    my_assert(dim1 == diagI1.getnrkept());
    my_assert(dimp == diagIp.getnrkept());
    dim1 = diagI1.getnrstored();
    dimp = diagIp.getnrstored();
    for (size_t r1 = 0; r1 < dim1; r1++) {
      const double E1 = diagI1.value_zero(r1);
      for (size_t rp = 0; rp < dimp; rp++) {
        const double Ep = diagIp.value_zero(rp);
        double d_energy = E1 - Ep;
        double d_weight = (spinfactor / stats.Zft) * op1II(r1, rp) * op2II(r1, rp) * exp(-Ep * step.scT());
        cs->add(step.scale() * d_energy, d_weight);
      }
    }
  } else {
    const size_t dimB     = diagIp.getnrstored();
    auto energies_beginIp = begin(diagIp.value_zero);
    auto energies_beginI1 = begin(diagI1.value_zero);
    if (dim1 && dimB) {
      const ublas::matrix_range<const Matrix> op1II_KT(op1II, ublas::range(0, rhoNI1.size1()), ublas::range(0, op1II.size2()));
      Matrix op1II_m_rho(rhoNI1.size2(), op1II_KT.size2());
      atlas::gemm(CblasTrans, CblasNoTrans, 1.0, rhoNI1, op1II_KT, 0.0, op1II_m_rho); // rhoNEW <- rhoNEW + factor T U
      for (size_t rk = 0; rk < dim1; rk++) {                                          // ii-term, Eq. (15), negative frequency excitations
        const double Ek = *(energies_beginI1 + rk);
        for (size_t rl = dimp; rl < dimB; rl++) {
          const double El       = *(energies_beginIp + rl);
          const double d_energy = Ek - El;
          const double sum      = op1II_m_rho(rk, rl);
          double d_weight       = spinfactor * sum;
          d_weight *= op2II(rk, rl);
          cs->add(step.scale() * d_energy, d_weight);
        }
      }
    }
  } // if (last)
}
#endif
