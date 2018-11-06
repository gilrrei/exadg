/*
 * unsteady_incompressible_flow_with_transport.cc
 *
 *  Created on: Nov 6, 2018
 *      Author: fehn
 */

// deal.II
#include <deal.II/base/revision.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_tools.h>

// CONVECTION-DIFFUSION

// postprocessor
#include "convection_diffusion/postprocessor/postprocessor.h"

// spatial discretization
#include "convection_diffusion/spatial_discretization/dg_convection_diffusion_operation.h"

// time integration
#include "convection_diffusion/time_integration/time_int_bdf.h"
#include "convection_diffusion/time_integration/time_int_explicit_runge_kutta.h"

// user interface, etc.
#include "convection_diffusion/user_interface/analytical_solution.h"
#include "convection_diffusion/user_interface/boundary_descriptor.h"
#include "convection_diffusion/user_interface/field_functions.h"
#include "convection_diffusion/user_interface/input_parameters.h"
#include "functionalities/print_functions.h"
#include "functionalities/print_general_infos.h"

// NAVIER-STOKES

// postprocessor
#include "../include/incompressible_navier_stokes/postprocessor/postprocessor.h"

// spatial discretization
#include "../include/incompressible_navier_stokes/spatial_discretization/dg_navier_stokes_coupled_solver.h"
#include "../include/incompressible_navier_stokes/spatial_discretization/dg_navier_stokes_dual_splitting.h"
#include "../include/incompressible_navier_stokes/spatial_discretization/dg_navier_stokes_pressure_correction.h"

// temporal discretization
#include "../include/incompressible_navier_stokes/time_integration/time_int_bdf_coupled_solver.h"
#include "../include/incompressible_navier_stokes/time_integration/time_int_bdf_dual_splitting.h"
#include "../include/incompressible_navier_stokes/time_integration/time_int_bdf_pressure_correction.h"

// Parameters, BCs, etc.
#include "../include/incompressible_navier_stokes/user_interface/analytical_solution.h"
#include "../include/incompressible_navier_stokes/user_interface/boundary_descriptor.h"
#include "../include/incompressible_navier_stokes/user_interface/field_functions.h"
#include "../include/incompressible_navier_stokes/user_interface/input_parameters.h"

#include "../include/functionalities/print_general_infos.h"

using namespace dealii;

// Navier-Stokes test case
#include "incompressible_navier_stokes_test_cases/cavity.h"

// convection-diffusion test case
#include "convection_diffusion_test_cases/cavity.h"

template<int dim, int degree_u, int degree_p, int degree_s, typename Number = double>
class Problem
{
public:
  Problem(unsigned int const refine_steps_space, unsigned int const refine_steps_time = 0);

  void
  setup(bool const do_restart);

  void
  solve();

private:
  // GENERAL (FLUID + TRANSPORT)
  void
  print_header();

  void
  run_timeloop();

  void
  set_start_time();

  void
  synchronize_time_step_size();

  ConditionalOStream pcout;

  parallel::distributed::Triangulation<dim> triangulation;
  std::vector<GridTools::PeriodicFacePair<typename Triangulation<dim>::cell_iterator>>
    periodic_faces;

  const unsigned int n_refine_space;

  bool use_adaptive_time_stepping;

  // INCOMPRESSIBLE NAVIER-STOKES
  void
  setup_navier_stokes(bool const do_restart);

  void
  setup_navier_stokes_operation();

  void
  setup_navier_stokes_time_integrator(bool const do_restart);

  void
  setup_navier_stokes_solvers();

  std::shared_ptr<IncNS::FieldFunctions<dim>>      fluid_field_functions;
  std::shared_ptr<IncNS::BoundaryDescriptorU<dim>> fluid_boundary_descriptor_velocity;
  std::shared_ptr<IncNS::BoundaryDescriptorP<dim>> fluid_boundary_descriptor_pressure;
  std::shared_ptr<IncNS::AnalyticalSolution<dim>>  fluid_analytical_solution;

  IncNS::InputParameters<dim> fluid_param;

  typedef IncNS::DGNavierStokesBase<dim, degree_u, degree_p, Number> DGBase;

  typedef IncNS::DGNavierStokesCoupled<dim, degree_u, degree_p, Number> DGCoupled;

  typedef IncNS::DGNavierStokesDualSplitting<dim, degree_u, degree_p, Number> DGDualSplitting;

  typedef IncNS::DGNavierStokesPressureCorrection<dim, degree_u, degree_p, Number>
    DGPressureCorrection;

  std::shared_ptr<DGBase> navier_stokes_operation;

  std::shared_ptr<DGCoupled> navier_stokes_operation_coupled;

  std::shared_ptr<DGDualSplitting> navier_stokes_operation_dual_splitting;

  std::shared_ptr<DGPressureCorrection> navier_stokes_operation_pressure_correction;

  typedef IncNS::PostProcessorBase<dim, degree_u, degree_p, Number> Postprocessor;

  std::shared_ptr<Postprocessor> fluid_postprocessor;

  typedef IncNS::TimeIntBDFCoupled<dim, degree_u, Number, DGCoupled> TimeIntCoupled;

  typedef IncNS::TimeIntBDFDualSplitting<dim, degree_u, Number, DGDualSplitting>
    TimeIntDualSplitting;

  typedef IncNS::TimeIntBDFPressureCorrection<dim, degree_u, Number, DGPressureCorrection>
    TimeIntPressureCorrection;

  std::shared_ptr<TimeIntCoupled> time_integrator_coupled;

  std::shared_ptr<TimeIntDualSplitting> time_integrator_dual_splitting;

  std::shared_ptr<TimeIntPressureCorrection> time_integrator_pressure_correction;

  // SCALAR TRANSPORT
  void
  setup_convection_diffusion();

  ConvDiff::InputParameters scalar_param;

  std::shared_ptr<ConvDiff::FieldFunctions<dim>>     scalar_field_functions;
  std::shared_ptr<ConvDiff::BoundaryDescriptor<dim>> scalar_boundary_descriptor;

  std::shared_ptr<ConvDiff::AnalyticalSolution<dim>> scalar_analytical_solution;

  std::shared_ptr<ConvDiff::DGOperation<dim, degree_s, Number>> conv_diff_operation;
  std::shared_ptr<ConvDiff::PostProcessor<dim, degree_s>>       scalar_postprocessor;

  std::shared_ptr<ConvDiff::TimeIntExplRK<dim, degree_s, Number>> scalar_time_integrator_explRK;
  std::shared_ptr<ConvDiff::TimeIntBDF<dim, degree_s, Number>>    scalar_time_integrator_BDF;
};

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
Problem<dim, degree_u, degree_p, degree_s, Number>::Problem(unsigned int const refine_steps_space,
                                                            unsigned int const refine_steps_time)
  : pcout(std::cout, Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0),
    triangulation(MPI_COMM_WORLD,
                  dealii::Triangulation<dim>::none,
                  parallel::distributed::Triangulation<dim>::construct_multigrid_hierarchy),
    n_refine_space(refine_steps_space),
    use_adaptive_time_stepping(false)
{
  print_header();
  print_MPI_info(pcout);

  fluid_param.set_input_parameters();
  fluid_param.check_input_parameters();

  if(fluid_param.print_input_parameters == true)
    fluid_param.print(pcout);

  fluid_field_functions.reset(new IncNS::FieldFunctions<dim>());
  // this function has to be defined in the header file
  // that implements all problem specific things like
  // parameters, geometry, boundary conditions, etc.
  IncNS::set_field_functions(fluid_field_functions);

  fluid_analytical_solution.reset(new IncNS::AnalyticalSolution<dim>());
  // this function has to be defined in the header file
  // that implements all problem specific things like
  // parameters, geometry, boundary conditions, etc.
  IncNS::set_analytical_solution(fluid_analytical_solution);

  fluid_boundary_descriptor_velocity.reset(new IncNS::BoundaryDescriptorU<dim>());
  fluid_boundary_descriptor_pressure.reset(new IncNS::BoundaryDescriptorP<dim>());

  if(fluid_param.calculation_of_time_step_size == IncNS::TimeStepCalculation::AdaptiveTimeStepCFL)
  {
    AssertThrow(false, ExcMessage("Not implemented"));

    // TODO: use this later once adaptive time stepping has been implemented for scaling
    // convection-diffusion equation
    //    AssertThrow(scalar_param.calculation_of_time_step_size ==
    //    ConvDiff::TimeStepCalculation::AdaptiveTimeStepCFL,
    //        ExcMessage("Adaptive time stepping has to be used for both fluid and scalar transport
    //        solvers."));

    use_adaptive_time_stepping = true;
  }

  AssertThrow(fluid_param.solver_type == IncNS::SolverType::Unsteady,
              ExcMessage("This is an unsteady solver. Check input parameters."));

  // initialize postprocessor
  // this function has to be defined in the header file
  // that implements all problem specific things like
  // parameters, geometry, boundary conditions, etc.
  fluid_postprocessor =
    IncNS::construct_postprocessor<dim, degree_u, degree_p, Number>(fluid_param);

  // initialize navier_stokes_operation
  if(this->fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    navier_stokes_operation_coupled.reset(
      new DGCoupled(triangulation, fluid_param, fluid_postprocessor));

    navier_stokes_operation = navier_stokes_operation_coupled;
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    navier_stokes_operation_dual_splitting.reset(
      new DGDualSplitting(triangulation, fluid_param, fluid_postprocessor));

    navier_stokes_operation = navier_stokes_operation_dual_splitting;
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    navier_stokes_operation_pressure_correction.reset(
      new DGPressureCorrection(triangulation, fluid_param, fluid_postprocessor));

    navier_stokes_operation = navier_stokes_operation_pressure_correction;
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }


  // initialize time integrator that depends on navier_stokes_operation
  if(this->fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    time_integrator_coupled.reset(new TimeIntCoupled(
      navier_stokes_operation_coupled, fluid_param, refine_steps_time, use_adaptive_time_stepping));
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    time_integrator_dual_splitting.reset(
      new TimeIntDualSplitting(navier_stokes_operation_dual_splitting,
                               fluid_param,
                               refine_steps_time,
                               use_adaptive_time_stepping));
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    time_integrator_pressure_correction.reset(
      new TimeIntPressureCorrection(navier_stokes_operation_pressure_correction,
                                    fluid_param,
                                    refine_steps_time,
                                    use_adaptive_time_stepping));
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }



  scalar_param.set_input_parameters();
  scalar_param.check_input_parameters();
  AssertThrow(scalar_param.problem_type == ConvDiff::ProblemType::Unsteady,
              ExcMessage("ProblemType must be unsteady!"));

  if(scalar_param.print_input_parameters)
    scalar_param.print(pcout);

  scalar_field_functions.reset(new ConvDiff::FieldFunctions<dim>());
  // this function has to be defined in the header file that implements
  // all problem specific things like parameters, geometry, boundary conditions, etc.
  ConvDiff::set_field_functions(scalar_field_functions);

  scalar_analytical_solution.reset(new ConvDiff::AnalyticalSolution<dim>());
  ConvDiff::set_analytical_solution(scalar_analytical_solution);

  scalar_boundary_descriptor.reset(new ConvDiff::BoundaryDescriptor<dim>());

  // initialize postprocessor
  scalar_postprocessor.reset(new ConvDiff::PostProcessor<dim, degree_s>());

  // initialize convection diffusion operation
  conv_diff_operation.reset(new ConvDiff::DGOperation<dim, degree_s, Number>(triangulation,
                                                                             scalar_param,
                                                                             scalar_postprocessor));

  // initialize time integrator
  if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK)
  {
    scalar_time_integrator_explRK.reset(new ConvDiff::TimeIntExplRK<dim, degree_s, Number>(
      conv_diff_operation, scalar_param, scalar_field_functions->velocity, refine_steps_time));
  }
  else if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF)
  {
    scalar_time_integrator_BDF.reset(new ConvDiff::TimeIntBDF<dim, degree_s, Number>(
      conv_diff_operation, scalar_param, scalar_field_functions->velocity, refine_steps_time));
  }
  else
  {
    AssertThrow(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK ||
                  scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF,
                ExcMessage("Specified time integration scheme is not implemented!"));
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::print_header()
{
  // clang-format off
  pcout << std::endl << std::endl << std::endl
  << "_________________________________________________________________________________" << std::endl
  << "                                                                                 " << std::endl
  << "                High-order discontinuous Galerkin solver for the                 " << std::endl
  << "                unsteady, incompressible Navier-Stokes equations                 " << std::endl
  << "                             with scalar transport.                              " << std::endl
  << "_________________________________________________________________________________" << std::endl
  << std::endl;
  // clang-format on
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::setup_navier_stokes(bool const do_restart)
{
  // this function has to be defined in the header file that implements all
  // problem specific things like parameters, geometry, boundary conditions, etc.
  IncNS::create_grid_and_set_boundary_conditions(triangulation,
                                                 n_refine_space,
                                                 fluid_boundary_descriptor_velocity,
                                                 fluid_boundary_descriptor_pressure,
                                                 periodic_faces);

  print_grid_data(pcout, n_refine_space, triangulation);

  setup_navier_stokes_operation();

  // setup time integrator before calling setup_solvers
  // (this is necessary since the setup of the solvers
  // depends on quantities such as the time_step_size or gamma0!!!)
  setup_navier_stokes_time_integrator(do_restart);

  setup_navier_stokes_solvers();
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::setup_navier_stokes_operation()
{
  AssertThrow(navier_stokes_operation.get() != 0, ExcMessage("Not initialized."));

  navier_stokes_operation->setup(periodic_faces,
                                 fluid_boundary_descriptor_velocity,
                                 fluid_boundary_descriptor_pressure,
                                 fluid_field_functions,
                                 fluid_analytical_solution);
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::setup_navier_stokes_time_integrator(
  bool const do_restart)
{
  if(this->fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    time_integrator_coupled->setup(do_restart);
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    time_integrator_dual_splitting->setup(do_restart);
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    time_integrator_pressure_correction->setup(do_restart);
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::setup_navier_stokes_solvers()
{
  if(this->fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    navier_stokes_operation_coupled->setup_solvers(
      time_integrator_coupled->get_scaling_factor_time_derivative_term());
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    navier_stokes_operation_dual_splitting->setup_solvers(
      time_integrator_dual_splitting->get_time_step_size(),
      time_integrator_dual_splitting->get_scaling_factor_time_derivative_term());
  }
  else if(this->fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    navier_stokes_operation_pressure_correction->setup_solvers(
      time_integrator_pressure_correction->get_time_step_size(),
      time_integrator_pressure_correction->get_scaling_factor_time_derivative_term());
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::setup_convection_diffusion()
{
  // this function has to be defined in the header file that implements
  // all problem specific things like parameters, geometry, boundary conditions, etc.
  parallel::distributed::Triangulation<dim> triangulation_dummy(
    MPI_COMM_WORLD,
    dealii::Triangulation<dim>::none,
    parallel::distributed::Triangulation<dim>::construct_multigrid_hierarchy);

  ConvDiff::create_grid_and_set_boundary_conditions(triangulation_dummy,
                                                    n_refine_space,
                                                    scalar_boundary_descriptor);

  conv_diff_operation->setup(periodic_faces,
                             scalar_boundary_descriptor,
                             scalar_field_functions,
                             scalar_analytical_solution);

  if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK)
  {
    scalar_time_integrator_explRK->setup();
  }
  else if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF)
  {
    // call setup() of time_integrator before setup_solvers() of conv_diff_operation
    // because setup_solver() needs quantities such as the time step size for a
    // correct initialization of preconditioners
    scalar_time_integrator_BDF->setup();

    conv_diff_operation->setup_solver(
      scalar_time_integrator_BDF->get_scaling_factor_time_derivative_term());
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::set_start_time()
{
  // Setup time integrator and get time step size
  double fluid_time = fluid_param.start_time, scalar_time = scalar_param.start_time;

  double time = std::min(fluid_time, scalar_time);

  // Set the same start time for both solvers

  // fluid
  if(fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    time_integrator_coupled->set_time(time);
  }
  else if(fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    time_integrator_dual_splitting->set_time(time);
  }
  else if(fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    time_integrator_pressure_correction->set_time(time);
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }

  // scalar transport
  if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK)
  {
    scalar_time_integrator_explRK->set_time(time);
  }
  else if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF)
  {
    scalar_time_integrator_BDF->set_time(time);
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::synchronize_time_step_size()
{
  // Setup time integrator and get time step size
  double time_step_size_fluid = 1.0, time_step_size_scalar = 1.0;

  // fluid
  if(fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    time_step_size_fluid = time_integrator_coupled->get_time_step_size();
  }
  else if(fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    time_step_size_fluid = time_integrator_dual_splitting->get_time_step_size();
  }
  else if(fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    time_step_size_fluid = time_integrator_pressure_correction->get_time_step_size();
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }

  // scalar transport
  if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK)
  {
    time_step_size_scalar = scalar_time_integrator_explRK->get_time_step_size();
  }
  else if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF)
  {
    time_step_size_scalar = scalar_time_integrator_BDF->get_time_step_size();
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }

  double time_step_size = std::min(time_step_size_fluid, time_step_size_scalar);

  if(use_adaptive_time_stepping == false)
  {
    // decrease time_step in order to exactly hit end_time
    time_step_size = (fluid_param.end_time - fluid_param.start_time) /
                     (1 + int((fluid_param.end_time - fluid_param.start_time) / time_step_size));

    pcout << std::endl
          << "Combined time step size for both domains: " << time_step_size << std::endl;
  }

  // Set the same time step size for both solvers

  // fluid
  if(fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
  {
    time_integrator_coupled->set_time_step_size(time_step_size);
  }
  else if(fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFDualSplittingScheme)
  {
    time_integrator_dual_splitting->set_time_step_size(time_step_size);
  }
  else if(fluid_param.temporal_discretization ==
          IncNS::TemporalDiscretization::BDFPressureCorrection)
  {
    time_integrator_pressure_correction->set_time_step_size(time_step_size);
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }


  // scalar transport
  if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK)
  {
    scalar_time_integrator_explRK->set_time_step_size(time_step_size);
  }
  else if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF)
  {
    scalar_time_integrator_BDF->set_time_step_size(time_step_size);
  }
  else
  {
    AssertThrow(false, ExcMessage("Not implemented."));
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::run_timeloop()
{
  bool finished_fluid = false, finished_scalar = false;

  set_start_time();

  synchronize_time_step_size();

  while(!finished_fluid || !finished_scalar)
  {
    // fluid: advance one time step
    if(fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
    {
      finished_fluid = time_integrator_coupled->advance_one_timestep(!finished_fluid);
    }
    else if(fluid_param.temporal_discretization ==
            IncNS::TemporalDiscretization::BDFDualSplittingScheme)
    {
      finished_fluid = time_integrator_dual_splitting->advance_one_timestep(!finished_fluid);
    }
    else if(fluid_param.temporal_discretization ==
            IncNS::TemporalDiscretization::BDFPressureCorrection)
    {
      finished_fluid = time_integrator_pressure_correction->advance_one_timestep(!finished_fluid);
    }
    else
    {
      AssertThrow(false, ExcMessage("Not implemented."));
    }

    // Communicate between fluid solver and scalar transport solver, i.e., ask the fluid solver
    // for the velocity field and hand it over to the scalar transport solver

    // TODO
//    LinearAlgebra::distributed::Vector<Number> velocity;
//    if(fluid_param.temporal_discretization == IncNS::TemporalDiscretization::BDFCoupledSolution)
//    {
//      velocity = time_integrator_coupled->get_velocity();
//    }
//    else if(fluid_param.temporal_discretization ==
//            IncNS::TemporalDiscretization::BDFDualSplittingScheme)
//    {
//      velocity = time_integrator_dual_splitting->get_velocity();
//    }
//    else if(fluid_param.temporal_discretization ==
//            IncNS::TemporalDiscretization::BDFPressureCorrection)
//    {
//      velocity = time_integrator_pressure_correction->get_velocity();
//    }
//    else
//    {
//      AssertThrow(false, ExcMessage("Not implemented."));
//    }


    // scalar transport: advance one time step
    if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::ExplRK)
    {
      finished_scalar = scalar_time_integrator_explRK->advance_one_timestep(!finished_scalar);
    }
    else if(scalar_param.temporal_discretization == ConvDiff::TemporalDiscretization::BDF)
    {
      finished_scalar = scalar_time_integrator_BDF->advance_one_timestep(!finished_scalar);
    }
    else
    {
      AssertThrow(false, ExcMessage("Not implemented."));
    }

    if(use_adaptive_time_stepping == true)
    {
      AssertThrow(false, ExcMessage("Adaptive time stepping is not implemented."));

      // Both solvers have already calculated the new, adaptive time step size individually in
      // function advance_one_timestep(). Here, we only have to synchronize the time step size.
      synchronize_time_step_size();
    }
  }
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::setup(bool const do_restart)
{
  setup_navier_stokes(do_restart);

  setup_convection_diffusion();
}

template<int dim, int degree_u, int degree_p, int degree_s, typename Number>
void
Problem<dim, degree_u, degree_p, degree_s, Number>::solve()
{
  run_timeloop();
}

int
main(int argc, char ** argv)
{
  try
  {
    Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);

    if(Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0)
    {
      std::cout << "deal.II git version " << DEAL_II_GIT_SHORTREV << " on branch "
                << DEAL_II_GIT_BRANCH << std::endl
                << std::endl;
    }

    deallog.depth_console(0);

    bool do_restart = false;
    if(argc > 1)
    {
      do_restart = std::atoi(argv[1]);
      if(do_restart)
      {
        AssertThrow(false, ExcMessage("Not implemented"));
      }
    }

    AssertThrow(IncNS::REFINE_STEPS_SPACE_MIN == IncNS::REFINE_STEPS_SPACE_MAX,
                ExcMessage("Invalid parameters!"));
    AssertThrow(IncNS::REFINE_STEPS_TIME_MIN == IncNS::REFINE_STEPS_TIME_MAX,
                ExcMessage("Invalid parameters!"));

    AssertThrow(ConvDiff::REFINE_STEPS_SPACE_MIN == ConvDiff::REFINE_STEPS_SPACE_MAX,
                ExcMessage("Invalid parameters!"));
    AssertThrow(ConvDiff::REFINE_STEPS_TIME_MIN == ConvDiff::REFINE_STEPS_TIME_MAX,
                ExcMessage("Invalid parameters!"));

    AssertThrow(ConvDiff::REFINE_STEPS_SPACE_MIN == IncNS::REFINE_STEPS_SPACE_MIN,
                ExcMessage("Invalid parameters!"));
    AssertThrow(ConvDiff::REFINE_STEPS_TIME_MIN == IncNS::REFINE_STEPS_TIME_MIN,
                ExcMessage("Invalid parameters!"));

    // currently, no refinement tests in space or time possible
    unsigned int refine_steps_space = IncNS::REFINE_STEPS_SPACE_MIN;
    unsigned int refine_steps_time  = IncNS::REFINE_STEPS_TIME_MIN;

    Problem<IncNS::DIMENSION,
            IncNS::FE_DEGREE_VELOCITY,
            IncNS::FE_DEGREE_PRESSURE,
            ConvDiff::FE_DEGREE,
            IncNS::VALUE_TYPE>
      problem(refine_steps_space, refine_steps_time);

    problem.setup(do_restart);

    problem.solve();
  }
  catch(std::exception & exc)
  {
    std::cerr << std::endl
              << std::endl
              << "----------------------------------------------------" << std::endl;
    std::cerr << "Exception on processing: " << std::endl
              << exc.what() << std::endl
              << "Aborting!" << std::endl
              << "----------------------------------------------------" << std::endl;
    return 1;
  }
  catch(...)
  {
    std::cerr << std::endl
              << std::endl
              << "----------------------------------------------------" << std::endl;
    std::cerr << "Unknown exception!" << std::endl
              << "Aborting!" << std::endl
              << "----------------------------------------------------" << std::endl;
    return 1;
  }
  return 0;
}
