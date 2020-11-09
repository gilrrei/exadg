/*
 * postprocessor.h
 *
 *  Created on: Oct 12, 2016
 *      Author: fehn
 */

#ifndef INCLUDE_CONVECTION_DIFFUSION_POSTPROCESSOR_H_
#define INCLUDE_CONVECTION_DIFFUSION_POSTPROCESSOR_H_

// deal.II
#include <deal.II/lac/la_parallel_vector.h>

// ExaDG
#include <exadg/convection_diffusion/postprocessor/output_generator.h>
#include <exadg/convection_diffusion/postprocessor/postprocessor_base.h>
#include <exadg/convection_diffusion/user_interface/analytical_solution.h>
#include <exadg/postprocessor/error_calculation.h>
#include <exadg/postprocessor/output_data_base.h>

namespace ExaDG
{
namespace ConvDiff
{
using namespace dealii;

template<int dim>
struct PostProcessorData
{
  PostProcessorData()
  {
  }

  OutputDataBase            output_data;
  ErrorCalculationData<dim> error_data;
};

template<int dim, typename Number>
class PostProcessor : public PostProcessorBase<dim, Number>
{
private:
  typedef LinearAlgebra::distributed::Vector<Number> VectorType;

public:
  PostProcessor(PostProcessorData<dim> const & pp_data, MPI_Comm const & mpi_comm);

  void
  setup(DoFHandler<dim> const & dof_handler, Mapping<dim> const & mapping);

  void
  do_postprocessing(VectorType const & solution,
                    double const       time             = 0.0,
                    int const          time_step_number = -1);

private:
  PostProcessorData<dim> pp_data;

  MPI_Comm const & mpi_comm;

  OutputGenerator<dim, Number> output_generator;
  ErrorCalculator<dim, Number> error_calculator;
};

} // namespace ConvDiff
} // namespace ExaDG


#endif /* INCLUDE_CONVECTION_DIFFUSION_POSTPROCESSOR_H_ */