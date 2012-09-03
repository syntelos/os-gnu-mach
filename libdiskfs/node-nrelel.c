/* 
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "priv.h"

/* Release a light reference on NP.  If NP is locked by anyone, then
   this cannot be the last reference (because you must hold a
   hard reference in order to hold the lock).  */
void
diskfs_nrele_light (struct node *np)
{
  pthread_spin_lock (&diskfs_node_refcnt_lock);
  assert (np->light_references);
  np->light_references--;
  if (np->references + np->light_references == 0)
    {
      pthread_mutex_lock (&np->lock);
      diskfs_drop_node (np);
    }
  else
    pthread_spin_unlock (&diskfs_node_refcnt_lock);
}
