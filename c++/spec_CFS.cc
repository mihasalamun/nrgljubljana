// Cf. Peters, Pruschke, Anders, Phys. Rev. B 74, 245113 (2006).
// Based on the implementation by Markus Greger.

template<typename S>
class Algo_CFSls : virtual public Algo<S> {
 protected:
   inline static const std::string algoname = "CFSls";
   SpectrumRealFreq<S> spec;
   const int sign; // 1 for bosons, -1 for fermions
   using CB = ChainBinning<S>;
   std::unique_ptr<CB> cb;
   const bool save;
 public:
   using Matrix = typename traits<S>::Matrix;
   using t_coef = typename traits<S>::t_coef;
   using t_eigen = typename traits<S>::t_eigen;
   using Algo<S>::P;
   Algo_CFSls(const std::string &name, const std::string &prefix, const gf_type gt, const Params &P, const bool save = true)
     : Algo<S>(P), spec(name, algoname, spec_fn(name, prefix, algoname, save), P), sign(gf_sign(gt)), save(save) {}
   void begin(const Step &) override { cb = std::make_unique<CB>(P); }
   void calc(const Step &step, const Eigen<S> &diagIp, const Eigen<S> &diagI1, const Matrix &op1, const Matrix &op2,
             t_coef factor, const Invar &Ip, const Invar &I1, const DensMatElements<S> &rho, const Stats<S> &stats) override
   {
     const auto &rhoNIp = rho.at(Ip);
     const auto &rhoNI1 = rho.at(I1);
     // Convention: k-loops over retained states, l-loop over discarded states. i-term, Eq. (11).
     if (step.last()) {
       for (const auto r1: diagI1.kept()) {
         for (const auto rp: diagIp.kept()) {
           const auto E1 = diagI1.value_zero(r1);
           const auto Ep = diagIp.value_zero(rp);
           const auto weight = (factor / stats.Zft) * conj_me(op1(r1, rp)) * op2(r1, rp) * exp(-E1 * step.scT()) * (-sign);
           cb->add(step.scale() * (E1-Ep), weight);
         }
       }
     } else {
       // iii-term, Eq. (16), positive frequency excitations
       if (op2.size1() && rhoNIp.size1()) {
         Matrix op2_m_rho;
         const ublas::matrix_range<const Matrix> op2_TK(op2, ublas::range(0, op2.size1()), ublas::range(0, rhoNIp.size1()));
         op2_m_rho = Matrix(op2_TK.size1(), rhoNIp.size2());
         atlas::gemm(CblasNoTrans, CblasNoTrans, 1.0, op2_TK, rhoNIp, 0.0, op2_m_rho); // rhoNEW <- rhoNEW + factor T U
         for (const auto rl: diagI1.discarded()) {
           for (const auto rk: diagIp.kept()) {
             const auto El       = diagI1.value_zero(rl);
             const auto Ek       = diagIp.value_zero(rk);
             const auto weight = factor * conj_me(op1(rl, rk)) * op2_m_rho(rl, rk) * (-sign);
             cb->add(step.scale() * (El-Ek), weight);
           }
         }
       }
     }
   }
   void end(const Step &step) override {
     spec.mergeCFS(*cb.get());
     cb.reset();
   }
   ~Algo_CFSls() { if (save) spec.save(); }
   std::string rho_type() override { return "rho"; }
};

template<typename S>
class Algo_CFSgt : virtual public Algo<S> {
 protected:
   inline static const std::string algoname = "CFSgt";
   SpectrumRealFreq<S> spec;
   const int sign; // 1 for bosons, -1 for fermions
   using CB = ChainBinning<S>;
   std::unique_ptr<CB> cb;
   const bool save;
 public:
   using Matrix = typename traits<S>::Matrix;
   using t_coef = typename traits<S>::t_coef;
   using t_eigen = typename traits<S>::t_eigen;
   using Algo<S>::P;
   Algo_CFSgt(const std::string &name, const std::string &prefix, const gf_type gt, const Params &P, const bool save = true)
     : Algo<S>(P), spec(name, algoname, spec_fn(name, prefix, algoname, save), P), sign(gf_sign(gt)), save(save) {}
   void begin(const Step &) override { cb = std::make_unique<CB>(P); }
   void calc(const Step &step, const Eigen<S> &diagIp, const Eigen<S> &diagI1, const Matrix &op1, const Matrix &op2,
             t_coef factor, const Invar &Ip, const Invar &I1, const DensMatElements<S> &rho, const Stats<S> &stats) override
   {
     const auto &rhoNIp = rho.at(Ip);
     const auto &rhoNI1 = rho.at(I1);
     // Convention: k-loops over retained states, l-loop over discarded states. i-term, Eq. (11).
     if (step.last()) {
       for (const auto r1: diagI1.kept()) {
         for (const auto rp: diagIp.kept()) {
           const auto E1 = diagI1.value_zero(r1);
           const auto Ep = diagIp.value_zero(rp);
           const auto weight = (factor / stats.Zft) * conj_me(op1(r1, rp)) * op2(r1, rp) * exp(-Ep * step.scT());
           cb->add(step.scale() * (E1-Ep), weight);
         }
       }
     } else {
       if (rhoNI1.size1() && op1.size2()) {
         const ublas::matrix_range<const Matrix> op1_KT(op1, ublas::range(0, rhoNI1.size1()), ublas::range(0, op1.size2()));
         Matrix op1_m_rho(rhoNI1.size2(), op1_KT.size2());
         if constexpr (std::is_same_v<S, double>) {
           atlas::gemm(CblasTrans, CblasNoTrans, 1.0, rhoNI1, op1_KT, 0.0, op1_m_rho); // rhoNEW <- rhoNEW + factor T U
         } else {
           const ublas::matrix<std::complex<double>> conj_op1_KT = conj(op1_KT);
           atlas::gemm(CblasTrans, CblasNoTrans, 1.0, rhoNI1, conj_op1_KT, 0.0, op1_m_rho); // rhoNEW <- rhoNEW + factor T U
         }
         for (const auto rk: diagI1.kept()) {                                          // ii-term, Eq. (15), negative frequency excitations
           for (const auto rl: diagIp.discarded()) {
             const auto Ek       = diagI1.value_zero(rk);
             const auto El       = diagIp.value_zero(rl);
             const auto weight = factor * op1_m_rho(rk, rl) * op2(rk, rl);
             cb->add(step.scale() * (Ek-El), weight);
           }
         }
       }
     }
   }
   void end(const Step &step) override {
     spec.mergeCFS(*cb.get());
     cb.reset();
   }
   ~Algo_CFSgt() { if (save) spec.save(); }
   std::string rho_type() override { return "rho"; }
};

template<typename S>
class Algo_CFS : public Algo_CFSls<S>, public Algo_CFSgt<S> {
 private:
   inline static const std::string algoname = "CFS";
   SpectrumRealFreq<S> spec_tot;
 public:
   using Matrix = typename traits<S>::Matrix;
   using t_coef = typename traits<S>::t_coef;
   using t_eigen = typename traits<S>::t_eigen;
   using Algo<S>::P;
   Algo_CFS(const std::string &name, const std::string &prefix, const gf_type gt, const Params &P) :
     Algo<S>(P), Algo_CFSls<S>(name, prefix, gt, P, false), Algo_CFSgt<S>(name, prefix, gt, P, false), spec_tot(name, algoname, spec_fn(name, prefix, algoname), P) {}
   void begin(const Step &step) override {
     Algo_CFSgt<S>::begin(step);
     Algo_CFSls<S>::begin(step);
   }
   void calc(const Step &step, const Eigen<S> &diagIp, const Eigen<S> &diagI1, const Matrix &op1, const Matrix &op2,
             t_coef factor, const Invar &Ip, const Invar &I1, const DensMatElements<S> &rho, const Stats<S> &stats) override
   {
     Algo_CFSgt<S>::calc(step, diagIp, diagI1, op1, op2, factor, Ip, I1, rho, stats);
     Algo_CFSls<S>::calc(step, diagIp, diagI1, op1, op2, factor, Ip, I1, rho, stats);
   }
   void end(const Step &step) override {
     spec_tot.mergeCFS(*Algo_CFSgt<S>::cb.get());
     spec_tot.mergeCFS(*Algo_CFSls<S>::cb.get());
     Algo_CFSgt<S>::cb.reset();
     Algo_CFSls<S>::cb.reset();
   }
   ~Algo_CFS() { spec_tot.save(); }
   std::string rho_type() override { return "rho"; }
};
