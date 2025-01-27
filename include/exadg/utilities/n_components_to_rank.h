/*  ______________________________________________________________________
 *
 *  ExaDG - High-Order Discontinuous Galerkin for the Exa-Scale
 *
 *  Copyright (C) 2021 by the ExaDG authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *  ______________________________________________________________________
 */

#ifndef INCLUDE_EXADG_UTILITIES_N_COMPONENTS_TO_RANK_H_
#define INCLUDE_EXADG_UTILITIES_N_COMPONENTS_TO_RANK_H_

namespace ExaDG
{
template<int rank, int dim>
constexpr unsigned int
rank_to_n_components()
{
  return (rank == 0) ? 1 : ((rank == 1) ? dim : dealii::numbers::invalid_unsigned_int);
}

template<int n_components, int dim>
constexpr unsigned int
n_components_to_rank()
{
  return (n_components == 1) ? 0 :
                               ((n_components == dim) ? 1 : dealii::numbers::invalid_unsigned_int);
}
} // namespace ExaDG

#endif /* INCLUDE_EXADG_UTILITIES_N_COMPONENTS_TO_RANK_H_ */
