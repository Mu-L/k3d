// K-3D
// Copyright (c) 1995-2004, Timothy M. Shead
//
// Contact: tshead@k-3d.com
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

/** \file
		\author Carsten Haubold (CarstenHaubold@web.de)
*/

#include <k3dsdk/document_plugin_factory.h>
#include <k3dsdk/log.h>
#include <k3dsdk/module.h>
#include <k3dsdk/node.h>
#include <k3dsdk/mesh.h>
#include <k3dsdk/mesh_source.h>
#include <k3dsdk/material_sink.h>
#include <k3dsdk/mesh_operations.h>
#include <k3dsdk/nurbs.h>
#include <k3dsdk/measurement.h>
#include <k3dsdk/selection.h>
#include <k3dsdk/data.h>
#include <k3dsdk/point3.h>
#include <k3dsdk/mesh_modifier.h>
#include <k3dsdk/mesh_selection_sink.h>
#include <k3dsdk/shared_pointer.h>

#include <iostream>
#include <vector>
#include <sstream>

#include <k3dsdk/metadata.h>

namespace k3d
{

/// Helper class for adding k3d::imetadata to properties
template<typename value_t, class name_policy_t>
class property_metadata :
	public data::writable_property<value_t, name_policy_t>,
	public metadata
{
protected:
	template<typename init_t>
	property_metadata(const init_t& Init) :
		data::writable_property<value_t, name_policy_t>(Init)
	{
	}
};

} // namespace k3d



namespace module
{

	namespace nurbs
	{
		class edit_knot_vector :
			public k3d::mesh_selection_sink<k3d::mesh_modifier<k3d::node > >
		{
			typedef k3d::mesh_selection_sink<k3d::mesh_modifier<k3d::node > > base;
		public:
			edit_knot_vector(k3d::iplugin_factory& Factory, k3d::idocument& Document) :
				base(Factory, Document),
				m_knot_vector(init_owner(*this) + init_name("knot_vector") + init_label(_("knot_vector")) + init_description(_("Knot Vector (csv):")) + init_value(k3d::mesh::knots_t()) )
			{
				m_knot_vector.set_metadata("k3d:property-type", "k3d:nurbs-knot-vector");
				m_mesh_selection.changed_signal().connect(make_update_mesh_slot());
				m_knot_vector.changed_signal().connect(make_update_mesh_slot());
			}

			void on_create_mesh(const k3d::mesh& Input, k3d::mesh& Output) 
			{
				k3d::log() << debug << "CreateMesh" << std::endl;
				Output=Input;
				
				if(!k3d::validate_nurbs_curve_groups(Output))
					return;

				int my_curve = select_mesh(Output);
				if(my_curve < 0)
				{
					k3d::log() << error << "More than one curve or no curve selected! " << my_curve << std::endl;
					return;
				}
				
				
				m_knot_vector.set_value(extract_knots(Output, my_curve));
			}
			

			void on_update_mesh(const k3d::mesh& Input, k3d::mesh& Output)
			{
				k3d::log() << debug << "UpdateMesh" << std::endl;
				Output=Input;
				
				if(!k3d::validate_nurbs_curve_groups(Output))
					return;

				merge_selection(m_mesh_selection.pipeline_value(), Output);
				
				const k3d::mesh::knots_t& knots = m_knot_vector.pipeline_value();
				
				int my_curve = select_mesh(Output);
				if(my_curve < 0)
				{
					k3d::log() << error << "More than one curve or no curve selected! " << my_curve << std::endl;
					return;
				}
				
				if(!insert_knots(knots, Output, my_curve))
					k3d::log() << error << "Invalid Knot Vector" << my_curve << std::endl;
			}

			static k3d::iplugin_factory& get_factory()
			{
				static k3d::document_plugin_factory<edit_knot_vector, k3d::interface_list<k3d::imesh_source, k3d::interface_list<k3d::imesh_sink > > > factory(
				k3d::uuid(0x8ff0922c, 0xf6412c8d, 0x2cf9c5a3, 0x54bdd064),
					"NurbsEditCurveKnotVector",
					_("Edit the knot vector of a NURBS curve"),
					"NURBS",
					k3d::iplugin_factory::EXPERIMENTAL);

				return factory;
			}
			
		private:
			k3d_data(k3d::mesh::knots_t, immutable_name, change_signal, no_undo, local_storage, no_constraint, k3d::property_metadata, no_serialization) m_knot_vector;
			
			int select_mesh(k3d::mesh& Output)
			{
				merge_selection(m_mesh_selection.pipeline_value(), Output);
				
				int my_curve=-1;
				
				const size_t group_begin = 0;
				const size_t group_end = group_begin + (*Output.nurbs_curve_groups->first_curves).size();
				for(size_t group = group_begin; group != group_end; ++group)
				{
					const size_t curve_begin = (*Output.nurbs_curve_groups->first_curves)[group];
					const size_t curve_end = curve_begin + (*Output.nurbs_curve_groups->curve_counts)[group];
					for(size_t curve = curve_begin; curve != curve_end; ++curve)
					{
						if( (*Output.nurbs_curve_groups->curve_selection)[curve]>0.0)
							if(my_curve>=0)
							{
								return -1;
							}
							else 
							{
								my_curve=curve;
							}						
					}
				}
				return my_curve;
			}
			
			k3d::mesh::knots_t extract_knots(const k3d::mesh& Output, int curve)
			{
				const k3d::mesh::nurbs_curve_groups_t& groups = *Output.nurbs_curve_groups;
				const k3d::mesh::knots_t& knots = *groups.curve_knots;
				k3d::mesh::knots_t curve_knots;
				
				const size_t curve_knots_begin = (*groups.curve_first_knots)[curve];
				const size_t curve_knots_end = curve_knots_begin + (*groups.curve_point_counts)[curve] + (*groups.curve_orders)[curve];
				
				for(size_t i = curve_knots_begin; i< curve_knots_end; i++)
					curve_knots.push_back( knots[i] );
					
				return curve_knots;
			}
			
			bool insert_knots(const k3d::mesh::knots_t& curve_knots, k3d::mesh& Output, int curve)
			{
				k3d::mesh::nurbs_curve_groups_t& groups = *k3d::make_unique(Output.nurbs_curve_groups);
				k3d::mesh::knots_t& knots = *k3d::make_unique(groups.curve_knots);
				
				const size_t curve_knots_begin = (*groups.curve_first_knots)[curve];
				const size_t curve_knots_end = curve_knots_begin + (*groups.curve_point_counts)[curve] + (*groups.curve_orders)[curve];
				
				if(curve_knots.size() != curve_knots_end - curve_knots_begin)
					return false;
					
				for(size_t i = curve_knots_begin; i < curve_knots_end; i++)
					knots[i] = curve_knots[i-curve_knots_begin];
					
				return true;
			}
			

		};

		//Create connect_curve factory
		k3d::iplugin_factory& edit_knot_vector_factory()
		{
			return edit_knot_vector::get_factory();
		}

	}//namespace nurbs
} //namespace module