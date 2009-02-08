/*
 * Copyright (c) 2003-2009, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pyinterp.h"

namespace ledger {

using namespace boost::python;

void export_chain();
void export_commodity();
void export_entry();
void export_flags();
void export_format();
void export_global();
void export_item();
void export_journal();
void export_report();
void export_scope();
void export_session();
void export_timelog();
void export_times();
void export_utils();
void export_value();
void export_xact();

void initialize_for_python()
{
  export_chain();
  export_commodity();
  export_entry();
  export_flags();
  export_format();
  export_global();
  export_item();
  export_journal();
  export_report();
  export_scope();
  export_session();
  export_timelog();
  export_times();
  export_utils();
  export_value();
  export_xact();
}

struct python_run
{
  object result;

  python_run(python_interpreter_t * intepreter,
	     const string& str, int input_mode)
    : result(handle<>(borrowed(PyRun_String(str.c_str(), input_mode,
					    intepreter->main_nspace.ptr(),
					    intepreter->main_nspace.ptr())))) {}
  operator object() {
    return result;
  }
};

void python_interpreter_t::initialize()
{
  TRACE_START(python_init, 1, "Initialized Python");

  try {
    DEBUG("python.interp", "Initializing Python");

    Py_Initialize();
    assert(Py_IsInitialized());

    object main_module = boost::python::import("__main__");
    if (! main_module)
      throw_(std::logic_error, "Python failed to initialize");

    main_nspace = extract<dict>(main_module.attr("__dict__"));
    if (! main_nspace)
      throw_(std::logic_error, "Python failed to initialize");

    boost::python::detail::init_module("ledger", &initialize_for_python);

    is_initialized = true;
  }
  catch (const error_already_set&) {
    PyErr_Print();
    throw_(std::logic_error, "Python failed to initialize");
  }

  TRACE_FINISH(python_init, 1);
}

object python_interpreter_t::import(const string& str)
{
  if (! is_initialized)
    initialize();

  try {
    TRACE_START(python_import, 1, "Imported Python module: " << str);

    object mod = boost::python::import(str.c_str());
    if (! mod)
      throw_(std::logic_error, "Failed to import Python module " << str);
 
    // Import all top-level entries directly into the main namespace
    main_nspace.update(mod.attr("__dict__"));

    TRACE_FINISH(python_import, 1);

    return mod;
  }
  catch (const error_already_set&) {
    PyErr_Print();
  }
  return object();
}

object python_interpreter_t::eval(std::istream& in, py_eval_mode_t mode)
{
  bool	 first = true;
  string buffer;
  buffer.reserve(4096);

  while (! in.eof()) {
    char buf[256];
    in.getline(buf, 255);
    if (buf[0] == '!')
      break;
    if (first)
      first = false;
    else
      buffer += "\n";
    buffer += buf;
  }

  if (! is_initialized)
    initialize();

  try {
    int input_mode;
    switch (mode) {
    case PY_EVAL_EXPR:  input_mode = Py_eval_input;   break;
    case PY_EVAL_STMT:  input_mode = Py_single_input; break;
    case PY_EVAL_MULTI: input_mode = Py_file_input;   break;
    }

    return python_run(this, buffer, input_mode);
  }
  catch (const error_already_set&) {
    PyErr_Print();
    throw_(std::logic_error, "Failed to evaluate Python code");
  }
  return object();
}

object python_interpreter_t::eval(const string& str, py_eval_mode_t mode)
{
  if (! is_initialized)
    initialize();

  try {
    int input_mode;
    switch (mode) {
    case PY_EVAL_EXPR:  input_mode = Py_eval_input;   break;
    case PY_EVAL_STMT:  input_mode = Py_single_input; break;
    case PY_EVAL_MULTI: input_mode = Py_file_input;   break;
    }

    return python_run(this, str, input_mode);
  }
  catch (const error_already_set&) {
    PyErr_Print();
    throw_(std::logic_error, "Failed to evaluate Python code");
  }
  return object();
}

expr_t::ptr_op_t python_interpreter_t::lookup(const string& name)
{
  // Give our superclass first dibs on symbol definitions
  if (expr_t::ptr_op_t op = session_t::lookup(name))
    return op;

  const char * p = name.c_str();
  switch (*p) {
  case 'o':
    if (std::strncmp(p, "opt_", 4) == 0) {
      p = p + 4;
      switch (*p) {
      case 'i':
	if (std::strcmp(p, "import_") == 0)
	  return MAKE_FUNCTOR(python_interpreter_t::option_import_);
	else if (std::strcmp(p, "import") == 0)
	  return expr_t::ptr_op_t();
	break;
      }
    }
    break;
  }

  if (is_initialized && main_nspace.has_key(name)) {
    DEBUG("python.interp", "Python lookup: " << name);

    if (boost::python::object obj = main_nspace.get(name))
      return WRAP_FUNCTOR(functor_t(name, obj));
  }

  return expr_t::ptr_op_t();
}
  
value_t python_interpreter_t::functor_t::operator()(call_scope_t& args)
{
  try {
    if (! PyCallable_Check(func.ptr())) {
      extract<value_t> val(func);
      if (val.check())
	return val();
      throw_(calc_error,
	     "Could not evaluate Python variable '" << name << "'");
    } else {
      if (args.size() > 0) {
	list arglist;
	if (args.value().is_sequence())
	  foreach (const value_t& value, args.value().as_sequence())
	    arglist.append(value);
	else
	  arglist.append(args.value());

	if (PyObject * val =
	    PyObject_CallObject(func.ptr(),
				boost::python::tuple(arglist).ptr())) {
	  extract<value_t> xval(val);
	  value_t result;
	  if (xval.check()) {
	    result = xval();
	    Py_DECREF(val);
	  } else {
	    Py_DECREF(val);
	    throw_(calc_error,
		   "Could not evaluate Python variable '" << name << "'");
	  }
	  return result;
	}
	else if (PyObject * err = PyErr_Occurred()) {
	  PyErr_Print();
	  throw_(calc_error,
		 "Failed call to Python function '" << name << "': " << err);
	} else {
	  assert(false);
	}
      } else {
	return call<value_t>(func.ptr());
      }
    }
  }
  catch (const error_already_set&) {
    PyErr_Print();
    throw_(calc_error,
	   "Failed call to Python function '" << name << "'");
  }
  return NULL_VALUE;
}

value_t python_interpreter_t::lambda_t::operator()(call_scope_t& args)
{
  try {
    assert(args.size() == 1);
    value_t item = args[0];
    return call<value_t>(func.ptr(), item);
  }
  catch (const error_already_set&) {
    PyErr_Print();
    throw_(calc_error, "Failed to evaluate Python lambda expression");
  }
  return NULL_VALUE;
}

} // namespace ledger
