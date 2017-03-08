/*
 * DivergenceAndContinuityPenaltyOperators.h
 *
 *  Created on: 2017 M03 1
 *      Author: fehn
 */

#ifndef INCLUDE_DIVERGENCEANDCONTINUITYPENALTYOPERATORS_H_
#define INCLUDE_DIVERGENCEANDCONTINUITYPENALTYOPERATORS_H_

/*
 *  Operator data
 */
struct DivergencePenaltyOperatorData
{
  DivergencePenaltyOperatorData()
    :
    penalty_parameter(1.0)
  {}

  double penalty_parameter;
};

/*
 *  Divergence penalty operator: ( div(v_h) , tau_div * div(u_h) )_Omega^e where
 *   v_h : test function
 *   u_h : solution
 *   tau_div: divergence penalty factor tau_div = K * || U_mean || * h
 *   Omega^e : element e
 */
template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class DivergencePenaltyOperator : public BaseOperator<dim>
{
public:
  static const bool is_xwall = (xwall_quad_rule>1) ? true : false;
  static const unsigned int n_actual_q_points_vel_linear = (is_xwall) ? xwall_quad_rule : fe_degree+1;
  typedef FEEvaluationWrapper<dim,fe_degree,fe_degree_xwall,n_actual_q_points_vel_linear,dim,value_type,is_xwall> FEEval_Velocity_Velocity_linear;

  typedef DivergencePenaltyOperator<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule, value_type> This;

  DivergencePenaltyOperator(MatrixFree<dim,value_type> const    &data_in,
                            unsigned int const                  dof_index_in,
                            unsigned int const                  quad_index_in,
                            DivergencePenaltyOperatorData const operator_data_in)
    :
    data(data_in),
    dof_index(dof_index_in),
    quad_index(quad_index_in),
    array_penalty_parameter(0),
    operator_data(operator_data_in)
  {
    array_penalty_parameter.resize(data.n_macro_cells()+data.n_macro_ghost_cells());
  }

  void calculate_array_penalty_parameter(parallel::distributed::Vector<value_type> const &velocity)
  {
    velocity.update_ghost_values();

    FEEval_Velocity_Velocity_linear fe_eval(data,this->fe_param,dof_index);

    AlignedVector<VectorizedArray<value_type> > JxW_values(fe_eval.n_q_points);

    for (unsigned int cell=0; cell<data.n_macro_cells()+data.n_macro_ghost_cells(); ++cell)
    {
      fe_eval.reinit(cell);
      fe_eval.read_dof_values(velocity);
      fe_eval.evaluate (true,false);
      VectorizedArray<value_type> volume = make_vectorized_array<value_type>(0.0);
      Tensor<1,dim,VectorizedArray<value_type> > U_mean;
      VectorizedArray<value_type> norm_U_mean;
      JxW_values.resize(fe_eval.n_q_points);
      fe_eval.fill_JxW_values(JxW_values);
      for (unsigned int q=0; q<fe_eval.n_q_points; ++q)
      {
        volume += JxW_values[q];
        U_mean += JxW_values[q]*fe_eval.get_value(q);
      }
      U_mean /= volume;
      norm_U_mean = U_mean.norm();

      array_penalty_parameter[cell] = operator_data.penalty_parameter * norm_U_mean * std::exp(std::log(volume)/(double)dim);
    }
  }

  MatrixFree<dim,value_type> const & get_data() const
  {
    return data;
  }

  AlignedVector<VectorizedArray<value_type> > const & get_array_penalty_parameter() const
  {
    return array_penalty_parameter;
  }

  unsigned int get_dof_index() const
  {
    return dof_index;
  }

  unsigned int get_quad_index() const
  {
    return quad_index;
  }

  FEParameters<dim> const * get_fe_param() const
  {
    return this->fe_param;
  }

  void vmult (parallel::distributed::Vector<value_type>       &dst,
              parallel::distributed::Vector<value_type> const &src) const
  {
    apply(dst,src);
  }

  void apply (parallel::distributed::Vector<value_type>       &dst,
              parallel::distributed::Vector<value_type> const &src) const
  {
    dst = 0;

    apply_add(dst,src);
  }

  void apply_add (parallel::distributed::Vector<value_type>       &dst,
                  parallel::distributed::Vector<value_type> const &src) const
  {
    this->get_data().cell_loop(&This::cell_loop, this, dst, src);
  }

  void calculate_diagonal(parallel::distributed::Vector<value_type> &diagonal) const
  {
    diagonal = 0;

    add_diagonal(diagonal);
  }

  void add_diagonal(parallel::distributed::Vector<value_type> &diagonal) const
  {
    parallel::distributed::Vector<value_type>  src_dummy(diagonal);
    this->get_data().cell_loop(&This::cell_loop_diagonal, this, diagonal, src_dummy);
  }

private:
  template<typename FEEvaluation>
  inline void do_cell_integral(FEEvaluation       &fe_eval,
                               unsigned int const cell) const
  {
    fe_eval.evaluate (false,true,false);

    VectorizedArray<value_type> tau = this->get_array_penalty_parameter()[cell];

    for (unsigned int q=0; q<fe_eval.n_q_points; ++q)
    {
      VectorizedArray<value_type > divergence = fe_eval.get_divergence(q);
      Tensor<2,dim,VectorizedArray<value_type> > unit_times_divU;
      for (unsigned int d=0; d<dim; ++d)
      {
        unit_times_divU[d][d] = divergence;
      }
      fe_eval.submit_gradient(tau*unit_times_divU, q);
    }

    fe_eval.integrate (false,true);
  }

  void cell_loop (const MatrixFree<dim,value_type>                &data,
                  parallel::distributed::Vector<value_type>       &dst,
                  const parallel::distributed::Vector<value_type> &src,
                  const std::pair<unsigned int,unsigned int>      &cell_range) const
  {
    FEEval_Velocity_Velocity_linear fe_eval(data,this->get_fe_param(),this->get_dof_index());

    for (unsigned int cell=cell_range.first; cell<cell_range.second; ++cell)
    {
      fe_eval.reinit(cell);
      fe_eval.read_dof_values(src);

      do_cell_integral(fe_eval,cell);

      fe_eval.distribute_local_to_global (dst);
    }
  }

  void cell_loop_diagonal (const MatrixFree<dim,value_type>                 &data,
                           parallel::distributed::Vector<value_type>        &dst,
                           const parallel::distributed::Vector<value_type>  &,
                           const std::pair<unsigned int,unsigned int>       &cell_range) const
  {
    FEEval_Velocity_Velocity_linear fe_eval(data,this->get_fe_param(),this->get_dof_index());

    for (unsigned int cell=cell_range.first; cell<cell_range.second; ++cell)
    {
      fe_eval.reinit (cell);

      VectorizedArray<value_type> local_diagonal_vector[fe_eval.tensor_dofs_per_cell*dim];
      for (unsigned int j=0; j<fe_eval.dofs_per_cell*dim; ++j)
      {
        for (unsigned int i=0; i<fe_eval.dofs_per_cell*dim; ++i)
          fe_eval.write_cellwise_dof_value(i,make_vectorized_array<value_type>(0.));
        fe_eval.write_cellwise_dof_value(j,make_vectorized_array<value_type>(1.));

        do_cell_integral(fe_eval,cell);

        local_diagonal_vector[j] = fe_eval.read_cellwise_dof_value(j);
      }
      for (unsigned int j=0; j<fe_eval.dofs_per_cell*dim; ++j)
        fe_eval.write_cellwise_dof_value(j,local_diagonal_vector[j]);

      fe_eval.distribute_local_to_global (dst);
    }
  }

  MatrixFree<dim,value_type> const & data;
  unsigned int const dof_index;
  unsigned int const quad_index;
  AlignedVector<VectorizedArray<value_type> > array_penalty_parameter;
  DivergencePenaltyOperatorData operator_data;
};


/*
 *  Operator data.
 */
struct ContinuityPenaltyOperatorData
{
  ContinuityPenaltyOperatorData()
    :
    penalty_parameter(1.0)
  {}

  double penalty_parameter;
};


/*
 *  Continuity penalty operator: ( v_h , tau_conti * jump(u_h) )_dOmega^e where
 *   v_h : test function
 *   u_h : solution
 *   jump(u_h) = u_h^{-} - u_h^{+} where "-" denotes interior information and "+" exterior information
 *   tau_conti: continuity penalty factor tau_conti = K * || U_mean ||
 *   dOmega^e : boundary of element e
 */
template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class ContinuityPenaltyOperator : public BaseOperator<dim>
{
public:
  static const bool is_xwall = (xwall_quad_rule>1) ? true : false;
  static const unsigned int n_actual_q_points_vel_linear = (is_xwall) ? xwall_quad_rule : fe_degree+1;
  typedef FEEvaluationWrapper<dim,fe_degree,fe_degree_xwall,n_actual_q_points_vel_linear,dim,value_type,is_xwall> FEEval_Velocity_Velocity_linear;
  typedef FEFaceEvaluationWrapper<dim,fe_degree,fe_degree_xwall,n_actual_q_points_vel_linear,dim,value_type,is_xwall> FEFaceEval_Velocity_Velocity_linear;

  typedef ContinuityPenaltyOperator<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule, value_type> This;

  ContinuityPenaltyOperator(MatrixFree<dim,value_type> const    &data_in,
                            unsigned int const                  dof_index_in,
                            unsigned int const                  quad_index_in,
                            ContinuityPenaltyOperatorData const operator_data_in)
    :
    data(data_in),
    dof_index(dof_index_in),
    quad_index(quad_index_in),
    array_penalty_parameter(0),
    operator_data(operator_data_in)
  {
    array_penalty_parameter.resize(data.n_macro_cells()+data.n_macro_ghost_cells());
  }

  void calculate_array_penalty_parameter(parallel::distributed::Vector<value_type> const &velocity)
  {
    velocity.update_ghost_values();

    FEEval_Velocity_Velocity_linear fe_eval(data,this->fe_param,dof_index);

    AlignedVector<VectorizedArray<value_type> > JxW_values(fe_eval.n_q_points);

    for (unsigned int cell=0; cell<data.n_macro_cells()+data.n_macro_ghost_cells(); ++cell)
    {
      fe_eval.reinit(cell);
      fe_eval.read_dof_values(velocity);
      fe_eval.evaluate (true,false);
      VectorizedArray<value_type> volume = make_vectorized_array<value_type>(0.0);
      Tensor<1,dim,VectorizedArray<value_type> > U_mean;
      VectorizedArray<value_type> norm_U_mean;
      JxW_values.resize(fe_eval.n_q_points);
      fe_eval.fill_JxW_values(JxW_values);
      for (unsigned int q=0; q<fe_eval.n_q_points; ++q)
      {
        volume += JxW_values[q];
        U_mean += JxW_values[q]*fe_eval.get_value(q);
      }
      U_mean /= volume;
      norm_U_mean = U_mean.norm();

      array_penalty_parameter[cell] = operator_data.penalty_parameter * norm_U_mean;
    }
  }

  MatrixFree<dim,value_type> const & get_data() const
  {
    return data;
  }
  AlignedVector<VectorizedArray<value_type> > const & get_array_penalty_parameter() const
  {
    return array_penalty_parameter;
  }
  unsigned int get_dof_index() const
  {
    return dof_index;
  }
  unsigned int get_quad_index() const
  {
    return quad_index;
  }
  FEParameters<dim> const * get_fe_param() const
  {
    return this->fe_param;
  }

  void vmult (parallel::distributed::Vector<value_type>       &dst,
              parallel::distributed::Vector<value_type> const &src) const
  {
    apply(dst,src);
  }

  void apply (parallel::distributed::Vector<value_type>       &dst,
              parallel::distributed::Vector<value_type> const &src) const
  {
    dst = 0;

    apply_add(dst,src);
  }

  void apply_add (parallel::distributed::Vector<value_type>       &dst,
                  parallel::distributed::Vector<value_type> const &src) const
  {
    this->get_data().loop(&This::cell_loop,&This::face_loop,
                          &This::boundary_face_loop,this, dst, src);
  }

  void calculate_diagonal(parallel::distributed::Vector<value_type> &diagonal) const
  {
    diagonal = 0;

    add_diagonal(diagonal);
  }

  void add_diagonal(parallel::distributed::Vector<value_type> &diagonal) const
  {
    parallel::distributed::Vector<value_type>  src_dummy(diagonal);
    this->get_data().loop(&This::cell_loop_diagonal,&This::face_loop_diagonal,
                          &This::boundary_face_loop_diagonal, this, diagonal, src_dummy);
  }

private:

  void cell_loop (const MatrixFree<dim,value_type>                &data,
                  parallel::distributed::Vector<value_type>       &dst,
                  const parallel::distributed::Vector<value_type> &src,
                  const std::pair<unsigned int,unsigned int>      &cell_range) const
  {
    // do nothing, i.e. no volume integrals
  }

  void face_loop (const MatrixFree<dim,value_type>                 &data,
                  parallel::distributed::Vector<value_type>        &dst,
                  const parallel::distributed::Vector<value_type>  &src,
                  const std::pair<unsigned int,unsigned int>       &face_range) const
  {
    FEFaceEval_Velocity_Velocity_linear fe_eval(data,this->get_fe_param(),true,this->get_dof_index());
    FEFaceEval_Velocity_Velocity_linear fe_eval_neighbor(data,this->get_fe_param(),false,this->get_dof_index());

    for(unsigned int face=face_range.first; face<face_range.second; face++)
    {
      fe_eval.reinit (face);
      fe_eval_neighbor.reinit (face);

      fe_eval.read_dof_values(src);
      fe_eval_neighbor.read_dof_values(src);

      fe_eval.evaluate(true,false);
      fe_eval_neighbor.evaluate(true,false);

      VectorizedArray<value_type> tau = 0.5*(fe_eval.read_cell_data(this->get_array_penalty_parameter())
                                             + fe_eval_neighbor.read_cell_data(this->get_array_penalty_parameter()));

      for(unsigned int q=0;q<fe_eval.n_q_points;++q)
      {
        Tensor<1,dim,VectorizedArray<value_type> > uM = fe_eval.get_value(q);
        Tensor<1,dim,VectorizedArray<value_type> > uP = fe_eval_neighbor.get_value(q);
        Tensor<1,dim,VectorizedArray<value_type> > jump_value = uM - uP;

        fe_eval.submit_value(tau*jump_value,q);
        fe_eval_neighbor.submit_value(-tau*jump_value,q);
      }
      fe_eval.integrate(true,false);
      fe_eval_neighbor.integrate(true,false);

      fe_eval.distribute_local_to_global(dst);
      fe_eval_neighbor.distribute_local_to_global(dst);
    }
  }

  void boundary_face_loop (const MatrixFree<dim,value_type>                &,
                           parallel::distributed::Vector<value_type>       &,
                           const parallel::distributed::Vector<value_type> &,
                           const std::pair<unsigned int,unsigned int>      &) const
  {
    // do nothing, i.e. no continuity penalty on boundary faces
  }

  void cell_loop_diagonal (const MatrixFree<dim,value_type>                &data,
                           parallel::distributed::Vector<value_type>       &dst,
                           const parallel::distributed::Vector<value_type> &,
                           const std::pair<unsigned int,unsigned int>      &cell_range) const
  {
    // do nothing, i.e. no volume integrals
  }

  void face_loop_diagonal (const MatrixFree<dim,value_type>                 &data,
                           parallel::distributed::Vector<value_type>        &dst,
                           const parallel::distributed::Vector<value_type>  &,
                           const std::pair<unsigned int,unsigned int>       &face_range) const
  {
    FEFaceEval_Velocity_Velocity_linear fe_eval(data,this->get_fe_param(),true,this->get_dof_index());
    FEFaceEval_Velocity_Velocity_linear fe_eval_neighbor(data,this->get_fe_param(),false,this->get_dof_index());

    for(unsigned int face=face_range.first; face<face_range.second; face++)
    {
      fe_eval.reinit (face);
      fe_eval_neighbor.reinit (face);

      // element-
      VectorizedArray<value_type> local_diagonal_vector[fe_eval.tensor_dofs_per_cell*dim];
      for (unsigned int j=0; j<fe_eval.dofs_per_cell*dim; ++j)
      {
        // set dof value j of element- to 1 and all other dof values of element- to zero
        for (unsigned int i=0; i<fe_eval.dofs_per_cell*dim; ++i)
          fe_eval.write_cellwise_dof_value(i,make_vectorized_array(0.));
        fe_eval.write_cellwise_dof_value(j,make_vectorized_array(1.));

        fe_eval.evaluate(true,false);

        VectorizedArray<value_type> tau = 0.5*(fe_eval.read_cell_data(this->get_array_penalty_parameter())
                                               + fe_eval_neighbor.read_cell_data(this->get_array_penalty_parameter()));

        for(unsigned int q=0;q<fe_eval.n_q_points;++q)
        {
          Tensor<1,dim,VectorizedArray<value_type> > uM = fe_eval.get_value(q);
          // set uP to zero
          Tensor<1,dim,VectorizedArray<value_type> > uP;
          Tensor<1,dim,VectorizedArray<value_type> > jump_value = uM - uP;

          fe_eval.submit_value(tau*jump_value,q);
        }

        fe_eval.integrate(true,false);

        local_diagonal_vector[j] = fe_eval.read_cellwise_dof_value(j);
      }
      for (unsigned int j=0; j<fe_eval.dofs_per_cell*dim; ++j)
        fe_eval.write_cellwise_dof_value(j, local_diagonal_vector[j]);

      fe_eval.distribute_local_to_global(dst);

      // neighbor (element+)
      VectorizedArray<value_type> local_diagonal_vector_neighbor[fe_eval_neighbor.tensor_dofs_per_cell*dim];
      for (unsigned int j=0; j<fe_eval_neighbor.dofs_per_cell*dim; ++j)
      {
        // set dof value j of element+ to 1 and all other dof values of element+ to zero
        for (unsigned int i=0; i<fe_eval_neighbor.dofs_per_cell*dim; ++i)
          fe_eval_neighbor.write_cellwise_dof_value(i, make_vectorized_array(0.));
        fe_eval_neighbor.write_cellwise_dof_value(j,make_vectorized_array(1.));

        fe_eval_neighbor.evaluate(true,false);

        VectorizedArray<value_type> tau = 0.5*(fe_eval.read_cell_data(this->get_array_penalty_parameter())
                                               + fe_eval_neighbor.read_cell_data(this->get_array_penalty_parameter()));

        for(unsigned int q=0;q<fe_eval.n_q_points;++q)
        {
          // set uM to zero
          Tensor<1,dim,VectorizedArray<value_type> > uM;
          Tensor<1,dim,VectorizedArray<value_type> > uP = fe_eval_neighbor.get_value(q);
          Tensor<1,dim,VectorizedArray<value_type> > jump_value = uP - uM; // interior - exterior = uP - uM (neighbor!)

          fe_eval_neighbor.submit_value(tau*jump_value,q);
        }
        fe_eval_neighbor.integrate(true,false);

        local_diagonal_vector_neighbor[j] = fe_eval_neighbor.read_cellwise_dof_value(j);
      }
      for (unsigned int j=0; j<fe_eval_neighbor.dofs_per_cell*dim; ++j)
        fe_eval_neighbor.write_cellwise_dof_value(j, local_diagonal_vector_neighbor[j]);

      fe_eval_neighbor.distribute_local_to_global(dst);
    }
  }

  void boundary_face_loop_diagonal (const MatrixFree<dim,value_type>                &,
                                    parallel::distributed::Vector<value_type>       &,
                                    const parallel::distributed::Vector<value_type> &,
                                    const std::pair<unsigned int,unsigned int>      &) const
  {
    // do nothing, i.e. no continuity penalty on boundary faces
  }

  MatrixFree<dim,value_type> const & data;
  unsigned int const dof_index;
  unsigned int const quad_index;
  AlignedVector<VectorizedArray<value_type> > array_penalty_parameter;
  ContinuityPenaltyOperatorData operator_data;
};


#endif /* INCLUDE_DIVERGENCEANDCONTINUITYPENALTYOPERATORS_H_ */