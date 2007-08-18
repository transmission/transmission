/*
 * Xmission - a cross-platform bittorrent client
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id$
 */


#ifndef _Foreach_h_
#define _Foreach_h_

#define foreach(Type,var,itname) \
  for (Type::iterator itname(var.begin()), \
                            itname##end(var.end()); itname!=itname##end; \
                            ++itname)

#define foreach_const(Type,var,itname) \
  for (Type::const_iterator itname(var.begin()), \
                            itname##end(var.end()); itname!=itname##end; \
                            ++itname)

#define foreach_r(Type,var,itname) \
  for (Type::reverse_iterator itname(var.rbegin()), \
                              itname##end(var.rend()); itname!=itname##end; \
                              ++itname)

#define foreach_const_r(Type,var,itname) \
  for (Type::const_reverse_iterator itname(var.rbegin()), \
                           itname##end(var.rend()); itname!=itname##end; \
                           ++itname)

#endif
