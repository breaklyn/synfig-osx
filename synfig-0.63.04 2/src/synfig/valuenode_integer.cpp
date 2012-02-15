/* === S Y N F I G ========================================================= */
/*!	\file valuenode_integer.cpp
**	\brief Implementation of the "Integer" valuenode conversion.
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**  Copyright (c) 2011 Carlos López
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "valuenode_integer.h"
#include "valuenode_const.h"
#include "general.h"
#include <ETL/misc>

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;

/* === M A C R O S ========================================================= */

/* === G L O B A L S ======================================================= */

/* === P R O C E D U R E S ================================================= */

/* === M E T H O D S ======================================================= */

ValueNode_Integer::ValueNode_Integer(const ValueBase::Type &x):
	LinkableValueNode(x)
{
}

ValueNode_Integer::ValueNode_Integer(const ValueBase &x):
	LinkableValueNode(x.get_type())
{
	Vocab ret(get_children_vocab());
	set_children_vocab(ret);
	switch(x.get_type())
	{
	case ValueBase::TYPE_ANGLE:
		set_link("integer", ValueNode_Const::create(round_to_int(Angle::deg(x.get(Angle())).get())));
		break;
	case ValueBase::TYPE_BOOL:
		set_link("integer", ValueNode_Const::create(int(x.get(bool()))));
		break;
	case ValueBase::TYPE_REAL:
		set_link("integer", ValueNode_Const::create(round_to_int(x.get(Real()))));
		break;
	case ValueBase::TYPE_TIME:
		set_link("integer", ValueNode_Const::create(round_to_int(x.get(Time()))));
		break;
	default:
		assert(0);
		throw runtime_error(get_local_name()+_(":Bad type ")+ValueBase::type_local_name(x.get_type()));
	}
}

ValueNode_Integer*
ValueNode_Integer::create(const ValueBase &x)
{
	return new ValueNode_Integer(x);
}

LinkableValueNode*
ValueNode_Integer::create_new()const
{
	return new ValueNode_Integer(get_type());
}

ValueNode_Integer::~ValueNode_Integer()
{
	unlink_all();
}

bool
ValueNode_Integer::set_link_vfunc(int i,ValueNode::Handle value)
{
	assert(i>=0 && i<link_count());

	switch(i)
	{
	case 0: CHECK_TYPE_AND_SET_VALUE(integer_, get_type());
	}
	return false;
}

ValueNode::LooseHandle
ValueNode_Integer::get_link_vfunc(int i)const
{
	assert(i>=0 && i<link_count());

	if(i==0) return integer_;

	return 0;
}

ValueBase
ValueNode_Integer::operator()(Time t)const
{
	if (getenv("SYNFIG_DEBUG_VALUENODE_OPERATORS"))
		printf("%s:%d operator()\n", __FILE__, __LINE__);

	int integer = (*integer_)(t).get(int());

	switch (get_type())
	{
	case ValueBase::TYPE_ANGLE:
		return Angle::deg(integer);
	case ValueBase::TYPE_BOOL:
		return bool(integer);
	case ValueBase::TYPE_REAL:
		return Real(integer);
	case ValueBase::TYPE_TIME:
		return Time(integer);
	default:
		assert(0);
		throw runtime_error(get_local_name()+_(":Bad type ")+ValueBase::type_local_name(get_type()));
	}
}

String
ValueNode_Integer::get_name()const
{
	return "fromint";
}

String
ValueNode_Integer::get_local_name()const
{
	return _("From Integer");
}

// don't show this to the user at the moment - maybe it's not very useful
bool
ValueNode_Integer::check_type(ValueBase::Type type __attribute__ ((unused)))
{
	return false;
//	return
//		type==ValueBase::TYPE_ANGLE ||
//		type==ValueBase::TYPE_BOOL  ||
//		type==ValueBase::TYPE_REAL  ||
//		type==ValueBase::TYPE_TIME;
}

LinkableValueNode::Vocab
ValueNode_Integer::get_children_vocab_vfunc()const
{
	if(children_vocab.size())
		return children_vocab;

	LinkableValueNode::Vocab ret;

	ret.push_back(ParamDesc(ValueBase(),"integer")
		.set_local_name(_("Integer"))
		.set_description(_("The integer value to be converted"))
	);

	return ret;
}
