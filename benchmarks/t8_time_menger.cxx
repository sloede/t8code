/*
  This file is part of t8code.
  t8code is a C library to manage a collection (a forest) of multiple
  connected adaptive space-trees of general element classes in parallel.

  Copyright (C) 2015 the developers

  t8code is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  t8code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with t8code; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <t8.h>
#include <t8_eclass.h>
#include <t8_cmesh.h>
#include <t8_cmesh/t8_cmesh_examples.h>
#include <t8_forest.h>
#include <t8_vec.h>
#include <t8_schemes/t8_default/t8_default_cxx.hxx>
#include <t8_forest/t8_forest_private.h>
#include <t8_forest/t8_forest_partition.h>
#include <t8_forest/t8_forest_types.h>

int
t8_adapt_callback_remove (t8_forest_t forest,
                          t8_forest_t forest_from,
                          t8_locidx_t which_tree,
                          t8_locidx_t lelement_id,
                          t8_eclass_scheme_c * ts,
                          const int is_family,
                          const int num_elements, 
                          t8_element_t * elements[])
{
  /*
  int child_id;
  int ancestor_id;
  int level;

  level = ts->t8_element_level (elements[0]);
  child_id = ts->t8_element_child_id (elements[0]);
  ancestor_id = ts->t8_element_ancestor_id (elements[0], level-1);

  if (3 == child_id + ancestor_id) {
    return -2;
  }
  return 0;
  */


  int child_id;
  int ancestor_id;
  int level;

  level = ts->t8_element_level (elements[0]);
  T8_ASSERT (level > 1);
  child_id = ts->t8_element_child_id (elements[0]);
  ancestor_id = ts->t8_element_ancestor_id (elements[0], level-1);

  if (ancestor_id < 4) {
    if (child_id > 3) {
      if (4 != child_id - ancestor_id) {
        return -2;
      }
    }
    else {
      if (3 == child_id + ancestor_id) {
        return -2;
      }
    }
  }
  else {
    if (child_id > 3) {
      if (11 == child_id + ancestor_id) {
        return -2;
      }
    }
    else {
      if (4 != ancestor_id - child_id) {
        return -2;
      }
    }
  }
  return 0;
  
}

int
t8_adapt_callback_refine (t8_forest_t forest,
                          t8_forest_t forest_from,
                          t8_locidx_t which_tree,
                          t8_locidx_t lelement_id,
                          t8_eclass_scheme_c * ts,
                          const int is_family,
                          const int num_elements, 
                          t8_element_t * elements[])
{
  int* level = (int*) t8_forest_get_user_data (forest);
  int level_element = ts->t8_element_level (elements[0]);
  if (level_element < *level*2 + 2) {
    return 1;
  }
  return 0;
}

int
t8_adapt_callback_rr (t8_forest_t forest,
                          t8_forest_t forest_from,
                          t8_locidx_t which_tree,
                          t8_locidx_t lelement_id,
                          t8_eclass_scheme_c * ts,
                          const int is_family,
                          const int num_elements, 
                          t8_element_t * elements[])
{
  int child_id;
  int ancestor_id;
  int level_element;

  level_element = ts->t8_element_level (elements[0]);

  if (0 == level_element%2) {
    child_id = ts->t8_element_child_id (elements[0]);
    ancestor_id = ts->t8_element_ancestor_id (elements[0], level_element-1);
    if (ancestor_id < 4) {
      if (child_id > 3) {
        if (4 != child_id - ancestor_id) {
          return -2;
        }
      }
      else {
        if (3 == child_id + ancestor_id) {
          return -2;
        }
      }
    }
    else {
      if (child_id > 3) {
        if (11 == child_id + ancestor_id) {
          return -2;
        }
      }
      else {
        if (4 != ancestor_id - child_id) {
          return -2;
        }
      }
    }
  }
  if (level_element < 10) {
    return 1;
  }

  return 0;
  
}

static t8_forest_t
t8_adapt_forest (t8_forest_t forest_from, t8_forest_adapt_t adapt_fn,
                 int do_partition, int recursive, int do_face_ghost, void *user_data)
{
  t8_forest_t         forest_new;

  t8_forest_init (&forest_new);
  t8_forest_set_adapt (forest_new, forest_from, adapt_fn, recursive);
  t8_forest_set_ghost (forest_new, do_face_ghost, T8_GHOST_FACES);
  if (do_partition) {
    t8_forest_set_partition (forest_new, NULL, 0);
  }
  if (user_data != NULL) {
    t8_forest_set_user_data (forest_new, user_data);
  }
  t8_forest_commit (forest_new);

  return forest_new;
}

void
t8_construct_menger (int max_level)
{
  int                 level, min_level;
  t8_cmesh_t          cmesh;
  t8_forest_t         forest;
  t8_scheme_cxx_t    *scheme;

  scheme = t8_scheme_new_default_cxx ();

  /* Construct a cmesh */
  cmesh = t8_cmesh_new_hypercube (T8_ECLASS_HEX, sc_MPI_COMM_WORLD, 0, 0, 0);

  /* Compute the first level, such that no process is empty */
  min_level = t8_forest_min_nonempty_level (cmesh, scheme);
  min_level = SC_MAX (min_level, 2);
  max_level += min_level;
  
  forest = t8_forest_new_uniform (cmesh, scheme, min_level, 0, sc_MPI_COMM_WORLD);
  forest = t8_adapt_forest (forest, t8_adapt_callback_rr, 0, 1, 0, NULL);

  for (level = min_level; level < max_level; level++) {
    //forest = t8_adapt_forest (forest, t8_adapt_callback_refine, 0, 1, 0, &level);
    //forest = t8_adapt_forest (forest, t8_adapt_callback_remove, 0, 0, 0, &level);
  }

  //t8_forest_write_vtk (forest, "/home/ioannis/VBshare/paraview_export/t8_benchmark_menger_remove");
  t8_forest_unref (&forest);
}


int
main (int argc, char **argv)
{
  int                 mpiret;
  sc_MPI_Comm         mpic;

  mpiret = sc_MPI_Init (&argc, &argv);
  SC_CHECK_MPI (mpiret);

  mpic = sc_MPI_COMM_WORLD;
  sc_init (mpic, 1, 1, NULL, SC_LP_PRODUCTION);
  t8_init (SC_LP_DEFAULT);

  for (size_t i = 0; i < 10; i++)
  {
  t8_construct_menger (4);
      
  }
  
  
  sc_finalize ();

  mpiret = sc_MPI_Finalize ();
  SC_CHECK_MPI (mpiret);

  return 0;
}