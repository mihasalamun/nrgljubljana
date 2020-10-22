#include "nrg-general.h"
#include "sym-SPU1-impl.h"
#include "sym-SPU1.h" // include for consistency

template <>
std::unique_ptr<Symmetry<double>> mk_SPU1(const Params &P, Allfields &allfields)
{
  return std::make_unique<SymmetrySPU1<double>>(P, allfields);
}

template <>
std::unique_ptr<Symmetry<cmpl>> mk_SPU1(const Params &P, Allfields &allfields)
{
  return std::make_unique<SymmetrySPU1<cmpl>>(P, allfields);
}
