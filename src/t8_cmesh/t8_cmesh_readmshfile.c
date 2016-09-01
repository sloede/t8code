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

#include <t8_eclass.h>
#include <t8_cmesh_readmshfile.h>
#include <t8_cmesh_vtk.h>
#include "t8_cmesh_types.h"
#include "t8_cmesh_stash.h"

/* The supported number of gmesh element classes.
 * Currently, we only support first order elements.
 */
#define       T8_NUM_GMSH_ELEM_CLASSES  15
/* look-up table to translate the gmsh element class to a t8code element class.
 */
const t8_eclass_t   t8_msh_element_type_to_eclass[T8_NUM_GMSH_ELEM_CLASSES +
                                                  1] = {
  T8_ECLASS_COUNT,              /* 0 is not valid */
  T8_ECLASS_LINE,               /* 1 */
  T8_ECLASS_TRIANGLE,
  T8_ECLASS_QUAD,
  T8_ECLASS_TET,
  T8_ECLASS_HEX,                /* 5 */
  T8_ECLASS_PRISM,
  T8_ECLASS_PYRAMID,            /* 7 This is the last first order element type,
                                   except the Point, which is type 15 */
  /* We do not support type 8 to 14 */
  T8_ECLASS_COUNT, T8_ECLASS_COUNT, T8_ECLASS_COUNT, T8_ECLASS_COUNT,
  T8_ECLASS_COUNT, T8_ECLASS_COUNT, T8_ECLASS_COUNT,
  T8_ECLASS_VERTEX              /* 15 */
};

/* translate the msh file vertex number to the t8code vertex number */
/* TODO: Check if these are correct */
const int           t8_msh_element_vertex_to_t8_vertex_num[T8_ECLASS_COUNT][8]
  = {
  {0},                          /* VERTEX */
  {0, 1},                       /* LINE */
  {0, 1, 3, 2},                 /* QUAD */
  {0, 1, 2},                    /* TRIANGLE */
  {0, 1, 5, 4, 2, 3, 7, 6},     /* HEX */
  {0, 1, 2, 3},                 /* TET */
  {0, 1, 2, 3, 4, 5, 8},        /* PRISM */
  {0, 1, 3, 2, 4}               /* PYRAMID */
};

/* TODO: if partitioned then only add the needed face-connections to join faces
 *       maybe also only trees and ghosts to classes.
 *       Specifying all face-connections makes commit algorithm slow! */

/* TODO: eventually compute neighbours only from .node and .ele files, since
 *       creating .neigh files with tetgen/triangle is not common and even seems
 *       to not work sometimes */

/* Read a the next line from a file stream that does not start with '#' or
 * contains only whitespaces (tabs etc.)
 *
 * \param [in,out] line     An allocated string to store the line.
 * \param [in,out] n        The number of allocated bytes.
 *                          If more bytes are needed line is reallocated and
 *                          the new number of bytes is stored in n.
 * \param [in]     fp       The file stream to read from.
 * \return                  The number of read arguments of the last line read.
 *                          negative on failure */
static int
t8_cmesh_msh_read_next_line (char **line, size_t * n, FILE * fp)
{
  int                 retval;

  do {
    /* read first non-comment line from file */
    /* TODO: getline depends on IEEE Std 1003.1-2008 (``POSIX.1'')
     *       p4est therefore has its own getline function in p4est_connectivity.h. */
    retval = getline (line, n, fp);
    if (retval < 0) {
      return retval;
    }
  }
  /* check if line is a comment (trailing '#') or consists solely of
   * blank spaces/tabs */
  while (*line[0] == '#' || strspn (*line, " \t\r\v\n") == strlen (*line));
  return retval;
}

/* The nodes are stored in the .msh file in the format
 *
 * $Nodes
 * n_nodes     // The number of nodes
 * i x_i y_i z_i  // the node index and the node coordinates
 * j x_j y_j z_j
 * .....
 * $EndNodes
 *
 * The node indices do not need to be in consecutive order.
 * We thus use a hash table to read all node indices and coordinates.
 * The hash value is the node index modulo the number of nodes.
 */
typedef struct
{
  t8_locidx_t         index;
  double              coordinates[3];
} t8_msh_file_node_t;

/* Return the hash value of a node.
 * \param [in]  node    The node whose hash value should be computed.
 * \param [in]  num_nodes A pointer to a locidx_t storing the total number of nodes.
 * \return              The hash value for a node. This is its index modulo the number of nodes.
 */
static unsigned
t8_msh_file_node_hash (const void *node, const void *num_nodes)
{
  t8_msh_file_node_t *Node;
  t8_locidx_t         Num_nodes;

  T8_ASSERT (node != NULL);
  T8_ASSERT (num_nodes != NULL);
  /* The data parameter stores the total number of nodes */
  Num_nodes = *(t8_locidx_t *) num_nodes;
  /* The node parameter stores a node structure */
  Node = (t8_msh_file_node_t *) node;
  /* The hash value of the node is its index modulo the number of nodes */
  return Node->index % Num_nodes;
}

/* Returns true if two given nodes are the same.
 * False otherwise.
 * Two nodes are considered equal if their indices are the same.
 * u_data is not needed.
 */
static int
t8_msh_file_node_compare (const void *node_a, const void *node_b,
                          const void *u_data)
{
  t8_msh_file_node_t *Node_a, *Node_b;

  Node_a = (t8_msh_file_node_t *) node_a;
  Node_b = (t8_msh_file_node_t *) node_b;

  return Node_a->index == Node_b->index;
}

/* Read an open .msh file and parse the nodes into a hash table.
 */
static sc_hash_t   *
t8_msh_file_read_nodes (FILE * fp, t8_locidx_t * num_nodes,
                        sc_mempool_t ** node_mempool)
{
  t8_msh_file_node_t *Node;
  sc_hash_t          *node_table = NULL;
  t8_locidx_t         ln, last_index;
  char               *line = malloc (1024);
  char                first_word[2048] = "\0";
  size_t              linen = 1024;
  int                 retval;
  long                index, lnum_nodes;

  T8_ASSERT (fp != NULL);
  /* Go to the beginning of the file */
  fseek (fp, 0, SEEK_SET);
  /* Search for the line beginning with "$Nodes" */
  while (!feof (fp) && strcmp (first_word, "$Nodes")) {
    (void) t8_cmesh_msh_read_next_line (&line, &linen, fp);
    /* Get the first word of this line */
    retval = sscanf (line, "%2048s", first_word);

    /* Checking for read/write error */
    if (retval != 1) {
      t8_global_errorf ("Premature end of line while reading num nodes.\n");
      t8_debugf ("The line is %s", line);
      goto die_node;
    }
  }

  /* Read the line containing the number of nodes */
  (void) t8_cmesh_msh_read_next_line (&line, &linen, fp);
  /* Read the number of nodes in a long int before converting it
   * to t8_locidx_t. */
  retval = sscanf (line, "%li", &lnum_nodes);
  /* Checking for read/write error */
  if (retval != 1) {
    t8_global_errorf ("Premature end of line while reading num nodes.\n");
    t8_debugf ("The line is %s", line);
    goto die_node;
  }
  *num_nodes = lnum_nodes;
  /* Check for type conversion error. */
  T8_ASSERT (*num_nodes == lnum_nodes);

  /* Create the mempool for the nodes */
  *node_mempool = sc_mempool_new (sizeof (t8_msh_file_node_t));
  /* Create the hash table */
  node_table = sc_hash_new (t8_msh_file_node_hash, t8_msh_file_node_compare,
                            num_nodes, NULL);

  /* read each node and add it to the hash table */
  last_index = 0;
  for (ln = 0; ln < *num_nodes; ln++) {
    /* Read the next line. Its format should be %i %f %f %f
     * The node index followed by its coordinates. */
    retval = t8_cmesh_msh_read_next_line (&line, &linen, fp);
    if (retval < 0) {
      t8_global_errorf ("Error reading node file\n");
      goto die_node;
    }
    /* Allocate a new node */
    Node = (t8_msh_file_node_t *) sc_mempool_alloc (*node_mempool);
    /* Fill the node with the entries in the file */
    retval = sscanf (line, "%li %lf %lf %lf", &index,
                     &Node->coordinates[0], &Node->coordinates[1],
                     &Node->coordinates[2]);
    if (retval != 4) {
      t8_global_errorf ("Error reading node file after node %li\n",
                        (long) last_index);
      goto die_node;
    }
    Node->index = index;
    /* Check for type conversion error */
    T8_ASSERT (Node->index == index);
    /* Insert the node in the hash table */
    retval = sc_hash_insert_unique (node_table, Node, NULL);
    /* If retval is zero then the node was already in the hash table.
     * This case should not occur. */
    T8_ASSERT (retval);
    last_index = Node->index;
  }

  free (line);
  t8_debugf ("Successfully read all Nodes.\n");
  return node_table;
  /* If everything went well, the function ends here. */

  /* This code is execute when a read/write error occurs */
die_node:
  /* If we allocated the hash table, destroy it */
  if (node_table != NULL) {
    sc_hash_destroy (node_table);
    sc_mempool_destroy (*node_mempool);
    node_mempool = NULL;
  }
  /* Free memory */
  free (line);
  /* Return NULL as error code */
  return NULL;
}

/* fp should be set after the Nodes section, right before the element section. */
int
t8_cmesh_msh_file_read_eles (t8_cmesh_t cmesh, FILE * fp,
                             sc_hash_t * vertices, int dim)
{
  char               *line = malloc (1024), *line_modify;
  char                first_word[2048] = "\0";
  size_t              linen = 1024;
  t8_locidx_t         num_elements, element_count;
  t8_gloidx_t         tree_count;
  t8_eclass_t         eclass;
  t8_msh_file_node_t  Node, **found_node;
  long                lnum_elements;
  int                 retval, i;
  int                 ele_type, num_tags;
  int                 num_nodes, t8_vertex_num;
  long                node_indices[8];
  double              tree_vertices[24];

  T8_ASSERT (fp != NULL);
  /* Search for the line beginning with "$Elements" */
  while (!feof (fp) && strcmp (first_word, "$Elements")) {
    (void) t8_cmesh_msh_read_next_line (&line, &linen, fp);
    /* Get the first word of this line */
    retval = sscanf (line, "%2048s", first_word);

    /* Checking for read/write error */
    if (retval != 1) {
      t8_global_errorf
        ("Premature end of line while reading num elements.\n");
      t8_debugf ("The line is %s", line);
      goto die_ele;
    }
  }

  /* Read the line containing the number of elements */
  (void) t8_cmesh_msh_read_next_line (&line, &linen, fp);
  /* Since t8_locidx_t could be int32 or int64, we first read the
   * number of elements in a long int and store it as t8_locidx_t later. */
  retval = sscanf (line, "%li", &lnum_elements);
  /* Checking for read/write error */
  if (retval != 1) {
    t8_global_errorf ("Premature end of line while reading num elements.\n");
    t8_debugf ("The line is %s", line);
    goto die_ele;
  }
  num_elements = lnum_elements;
  /* Check for type conversion error */
  T8_ASSERT (num_elements == lnum_elements);

  tree_count = 0;
  for (element_count = 0; element_count < num_elements; element_count++) {
    /* Read the next line containing element information */
    retval = t8_cmesh_msh_read_next_line (&line, &linen, fp);
    if (retval < 0) {
      t8_global_errorf ("Premature end of line while reading elements.\n");
      goto die_ele;
    }
    /* The line describing the element looks like
     * Element_number Element_type Number_tags tag_1 ... tag_n Node_1 ... Node_m
     *
     * We ignore the element number, read the type and the number of (integer) tags.
     * We also ignore the tags and after we know the type, we read the
     * nodes.
     */
    sscanf (line, "%*i %i %i", &ele_type, &num_tags);
    /* Check if the element type is supported */
    if (ele_type > T8_NUM_GMSH_ELEM_CLASSES || ele_type < 0
        || t8_msh_element_type_to_eclass[ele_type] == T8_ECLASS_COUNT) {
      t8_global_errorf ("Element type %i is not supported by t8code.\n",
                        ele_type);
      goto die_ele;
    }
    /* Continue if element type is supported */
    eclass = t8_msh_element_type_to_eclass[ele_type];
    T8_ASSERT (eclass != T8_ECLASS_COUNT);
    /* Check if the element is of the correct dimension */
    if (t8_eclass_to_dimension[eclass] == dim) {
      /* The element is of the correct dimension,
       * add it to the cmesh and read its nodes */
      t8_cmesh_set_tree_class (cmesh, tree_count, eclass);
      line_modify = line;
      /* Since the tags are stored before the node indices, we need to
       * skip them first. But since the number of them is unknown and the
       * lenght (in characters) of them, we have to skip one by one. */
      for (i = 0; i < 3 + num_tags; i++) {
        T8_ASSERT (strcmp (line_modify, "\0"));
        /* move line_modify to the next word in the line */
        (void) strsep (&line_modify, " ");
      }
      /* At this point line_modify contains only the node indices. */
      num_nodes = t8_eclass_num_vertices[eclass];
      for (i = 0; i < num_nodes; i++) {
        T8_ASSERT (strcmp (line_modify, "\0"));
        retval = sscanf (line_modify, "%li", node_indices + i);
        if (retval != 1) {
          t8_global_errorf ("Premature end of line while reading element.\n");
          t8_debugf ("The line is %s", line);
          goto die_ele;
        }
        /* move line_modify to the next word in the line */
        (void) strsep (&line_modify, " ");
      }
      /* Now the nodes are read and we get their coordinates from
       * the stored nodes */
      for (i = 0; i < num_nodes; i++) {
        Node.index = node_indices[i];
        sc_hash_lookup (vertices, (void *) &Node, (void ***) &found_node);
        /* Add node coordinates to the tree vertices */
        t8_vertex_num = t8_msh_element_vertex_to_t8_vertex_num[eclass][i];
        tree_vertices[3 * t8_vertex_num] = (*found_node)->coordinates[0];
        tree_vertices[3 * t8_vertex_num + 1] = (*found_node)->coordinates[1];
        tree_vertices[3 * t8_vertex_num + 2] = (*found_node)->coordinates[2];
      }
      /* Set the vertices of this tree */
      t8_cmesh_set_tree_vertices (cmesh, tree_count, t8_get_package_id (),
                                  0, tree_vertices, num_nodes);
      /* advance the tree counter */
      tree_count++;
    }
  }
  free (line);
  return 0;
die_ele:
  /* Error handling */
  free (line);
  t8_cmesh_destroy (&cmesh);
  return -1;
}

t8_cmesh_t
t8_cmesh_from_msh_file (char *fileprefix, int partition,
                        sc_MPI_Comm comm, int dim)
{
  int                 mpirank, mpisize, mpiret;
  t8_cmesh_t          cmesh;
  sc_hash_t          *vertices;
  t8_locidx_t         num_vertices;
  sc_mempool_t       *node_mempool = NULL;
  int                 retval;
  char                current_file[BUFSIZ];
  FILE               *file;

  mpiret = sc_MPI_Comm_size (comm, &mpisize);
  SC_CHECK_MPI (mpiret);
  mpiret = sc_MPI_Comm_rank (comm, &mpirank);
  SC_CHECK_MPI (mpiret);

  /* TODO: implement partitioned input using gmesh's
   * partitioned files.
   * Or using a single file and computing the partition on the run. */
  T8_ASSERT (partition == 0);

  snprintf (current_file, BUFSIZ, "%s.msh", fileprefix);
  /* Open the file */
  t8_debugf ("Opening file %s\n", current_file);
  file = fopen (current_file, "r");
  if (file == NULL) {
    t8_global_errorf ("Could not open file %s\n", current_file);
    return NULL;
  }
  /* read nodes from the file */
  vertices = t8_msh_file_read_nodes (file, &num_vertices, &node_mempool);

  /* initialize cmesh structure */
  t8_cmesh_init (&cmesh);
  t8_cmesh_msh_file_read_eles (cmesh, file, vertices, dim);
  /* close the file and free the memory for the nodes */
  fclose (file);
  if (vertices != NULL) {
    sc_hash_destroy (vertices);
  }
  sc_mempool_destroy (node_mempool);
  /* Commit the cmesh */
  if (cmesh != NULL) {
    T8_ASSERT (cmesh->dimension == dim);
    t8_cmesh_commit (cmesh, comm);
  }
  return cmesh;
}
