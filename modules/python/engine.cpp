// K-3D
// Copyright (c) 1995-2009, Timothy M. Shead
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
	\brief Implements Python engine, an implementation of k3d::iscript_engine that supports the Python language
	\author Anders Dahnielson (anders@dahnielson.com)
	\author Romain Behar (romainbehar@yahoo.com)
	\author Adam Hupp (hupp@cs.wisc.edu)
	\author Timothy M. Shead (tshead@k-3d.com)
*/

#include <k3d-i18n-config.h>
#include <k3dsdk/application_plugin_factory.h>
#include <k3dsdk/classes.h>
#include <k3dsdk/command_node.h>
#include <k3dsdk/file_helpers.h>
#include <k3dsdk/fstream.h>
#include <k3dsdk/iscript_engine.h>
#include <k3dsdk/module.h>
#include <k3dsdk/python/file_signal_python.h>
#include <k3dsdk/python/object_model_python.h>
#include <k3dsdk/result.h>
#include <k3dsdk/string_modifiers.h>

#include <boost/assign/list_of.hpp>
#include <boost/python.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/algorithm/string.hpp>

namespace module
{

namespace python
{

/// Magic token used to identify Python scripts
const k3d::string_t magic_token("#python");

/// Ensures that Py_Initialize() is called before any other Python API functions
class initialize_python
{
public:
	initialize_python()
	{
		if(!Py_IsInitialized())
		{
			Py_Initialize();

			try
			{
				initk3d();
			}
			catch(std::exception& e)
			{
				k3d::log() << error << e.what() << std::endl;
			}
			catch(...)
			{
				k3d::log() << error << "Unknown exception" << std::endl;
			}
		}
	}
};

class engine :
	public k3d::iscript_engine
{
public:
	k3d::iplugin_factory& factory()
	{
		return get_factory();
	}

	static k3d::iplugin_factory& get_factory()
	{
		static k3d::application_plugin_factory<engine, k3d::interface_list<k3d::iscript_engine> > factory(
			k3d::classes::PythonEngine(),
			"Python",
			_("Python scripting engine"),
			"ScriptEngine",
			k3d::iplugin_factory::STABLE,
			boost::assign::map_list_of("k3d:mime-types", "text/x-python"));

		return factory;
	}

	const k3d::string_t language()
	{
		return "Python";
	}

	k3d::bool_t execute(const k3d::string_t& ScriptName, const k3d::string_t& Script, context_t& Context, output_t* Stdout, output_t* Stderr)
	{
		k3d::bool_t succeeded = true;

		try
		{
			boost::scoped_ptr<k3d::python::file_signal> stdout_signal;
			boost::scoped_ptr<k3d::python::file_signal> stderr_signal;

			if(Stdout)
			{
				stdout_signal.reset(new k3d::python::file_signal());
				stdout_signal->connect_output_signal(*Stdout);
				PySys_SetObject(const_cast<char*>("stdout"), boost::python::object(*stdout_signal).ptr());
			}

			if(Stderr)
			{
				stderr_signal.reset(new k3d::python::file_signal());
				stderr_signal->connect_output_signal(*Stderr);
				PySys_SetObject(const_cast<char*>("stderr"), boost::python::object(*stderr_signal).ptr());
			}

			k3d::python::set_context(Context, m_local_dict);

			// The embedded python interpreter cannot handle DOS line-endings, see http://sourceforge.net/tracker/?group_id=5470&atid=105470&func=detail&aid=1167922
			k3d::string_t script = Script;
			script.erase(std::remove(script.begin(), script.end(), '\r'), script.end());

			PyDict_Update(m_local_dict.ptr(), PyObject_GetAttrString(PyImport_AddModule("__main__"), "__dict__"));
			
			PyObject* const result = PyRun_String(const_cast<char*>(script.c_str()), Py_file_input, m_local_dict.ptr(), m_local_dict.ptr());
			if(result)
			{
				Py_DECREF(result);
				if(Py_FlushLine())
					PyErr_Clear();
			}
			else
			{
				PyErr_Print();
			}

			k3d::python::get_context(m_local_dict, Context);
			
			succeeded = result ? true : false;
		}
		catch(std::exception& e)
		{
			k3d::log() << error << k3d_file_reference << ": " << e.what() << std::endl;
			succeeded = false;
		}
		catch(...)
		{
			k3d::log() << error << k3d_file_reference << ": " << "Unknown exception" << std::endl;
			succeeded = false;
		}

		if(Stdout)
		      PySys_SetObject(const_cast<char*>("stdout"), PySys_GetObject(const_cast<char*>("__stdout__")));

		if(Stderr)
		      PySys_SetObject(const_cast<char*>("stderr"), PySys_GetObject(const_cast<char*>("__stderr__")));

		return succeeded;
	}

	k3d::bool_t halt()
	{
		return false;
	}

	const completions_t complete(const k3d::string_t& Command)
	{
		completions_t tokens_equal_token;
		boost::split(tokens_equal_token, Command, boost::is_any_of("= "));
		completions_t tokens, completions;
		boost::split(tokens, tokens_equal_token.back(), boost::is_any_of("."));
		if(tokens.size() == 1)
		{
			const boost::python::list locals_keys = m_local_dict.keys();
			append_completions(locals_keys, tokens[0], completions);
		}
		else if(tokens.size() > 1)
		{
			if(!m_local_dict.has_key(tokens[0]))
				return completions;
			boost::python::object obj = m_local_dict[tokens[0]];
			for(k3d::uint_t i = 1; i != (tokens.size() - 1); ++i)
			{
				obj = boost::python::getattr(obj, tokens[i].c_str());
				if(!obj.ptr())
					return completions;
			}
			PyObject* dir = PyObject_Dir(obj.ptr());
			return_val_if_fail(PyList_Check(dir), completions);
			const boost::python::list candidates = boost::python::extract<boost::python::list>(dir);
			append_completions(candidates, tokens[tokens.size() - 1], completions);
		}
		return completions;
	}

private:
	void append_completions(const boost::python::list& Candidates, const k3d::string_t& ToComplete, completions_t& Completions)
	{
		for(k3d::uint_t i = 0; i != boost::python::len(Candidates); ++i)
		{
			const k3d::string_t completion_candidate = boost::python::extract<k3d::string_t>(Candidates[i]);
			if(boost::algorithm::starts_with(completion_candidate, ToComplete))
				Completions.push_back(completion_candidate);
		}
	}

	initialize_python m_initialize_python;
	boost::python::dict m_local_dict;
};

} // namespace python

} // namespace module

K3D_MODULE_START(Registry)
	Registry.register_factory(module::python::engine::get_factory());
K3D_MODULE_END

