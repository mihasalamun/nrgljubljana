#ifndef _matsubara_h_
#define _matsubara_h_

// Note: range limited to short numbers.
inline auto ww(const short n, const gf_type mt, const double T)
{
  switch (mt) {
    case gf_type::bosonic: return T * M_PI * (2 * n);
    case gf_type::fermionic: return T * M_PI * (2 * n + 1);
    default: my_assert_not_reached();
  }
}
inline auto wb(const short n, const double T) { return T * M_PI * (2 * n); }
inline auto wf(const short n, const double T) { return T * M_PI * (2 * n + 1); }

template<typename S>
class Matsubara {
 private:
   using t_temp = typename traits<S>::t_temp;
   using t_weight = typename traits<S>::t_weight;
   using matsgf = std::vector<std::pair<t_temp, t_weight>>;
   matsgf v;
   gf_type mt;
   t_temp T;
 public:
   Matsubara() = delete;
   Matsubara(const size_t mats, const gf_type mt, const t_temp T) : mt(mt), T(T) {
     my_assert(mt == gf_type::bosonic || mt == gf_type::fermionic);
     for (const auto n: range0(mats)) v.emplace_back(ww(n, mt, T), 0);
   }
   void add(const size_t n, const t_weight &w) { v[n].second += w; }
   void merge(const Matsubara &m2) {
     my_assert(v.size() == m2.v.size());
     for (const auto n: range0(v.size())) {
       my_assert(v[n].first == m2.v[n].first);
       v[n].second += m2.v[n].second;
     }
   }
   template <typename T> void save(T && F, const int prec) const {
     F << std::setprecision(prec); // prec_xy
     for (const auto &[e, w] : v) outputxy(F, e, w, true);
   }
};

#endif // _matsubara_h_
