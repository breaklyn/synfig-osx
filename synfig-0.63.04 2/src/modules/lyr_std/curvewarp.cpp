/* === S Y N F I G ========================================================= */
/*!	\file curvewarp.cpp
**	\brief Implementation of the "Curve Warp" layer
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007-2008 Chris Moore
**	Copyright (c) 2011 Carlos López
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
**
** === N O T E S ===========================================================
**
** ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "curvewarp.h"

#include <synfig/context.h>
#include <synfig/paramdesc.h>
#include <synfig/surface.h>
#include <synfig/valuenode.h>
#include <ETL/calculus>

#endif

/* === M A C R O S ========================================================= */

#define FAKE_TANGENT_STEP 0.000001
#define TOO_THIN 0.01

/* === G L O B A L S ======================================================= */

SYNFIG_LAYER_INIT(CurveWarp);
SYNFIG_LAYER_SET_NAME(CurveWarp,"curve_warp");
SYNFIG_LAYER_SET_LOCAL_NAME(CurveWarp,N_("Curve Warp"));
SYNFIG_LAYER_SET_CATEGORY(CurveWarp,N_("Distortions"));
SYNFIG_LAYER_SET_VERSION(CurveWarp,"0.0");
SYNFIG_LAYER_SET_CVS_ID(CurveWarp,"$Id$");

/* === P R O C E D U R E S ================================================= */

inline float calculate_distance(const std::vector<synfig::BLinePoint>& bline)
{
	std::vector<synfig::BLinePoint>::const_iterator iter,next,ret;
	std::vector<synfig::BLinePoint>::const_iterator end(bline.end());

	float dist(0);

	if (bline.empty()) return dist;

	next=bline.begin();
	iter=next++;

	for(;next!=end;iter=next++)
	{
		// Setup the curve
		etl::hermite<Vector> curve(iter->get_vertex(), next->get_vertex(), iter->get_tangent2(), next->get_tangent1());
		dist+=curve.length();
	}

	return dist;
}

std::vector<synfig::BLinePoint>::const_iterator
find_closest_to_bline(bool fast, const std::vector<synfig::BLinePoint>& bline,const Point& p,float& t, float& len, bool& extreme)
{
	std::vector<synfig::BLinePoint>::const_iterator iter,next,ret;
	std::vector<synfig::BLinePoint>::const_iterator end(bline.end());

	ret=bline.end();
	float dist(100000000000.0);
	next=bline.begin();
	float best_pos(0), best_len(0);
	etl::hermite<Vector> best_curve;
	iter=next++;
	Point bp;
	float total_len(0);
	bool first = true, last = false;
	extreme = false;

	for(;next!=end;iter=next++)
	{
		// Setup the curve
		etl::hermite<Vector> curve(iter->get_vertex(), next->get_vertex(), iter->get_tangent2(), next->get_tangent1());
		float thisdist(0);
		last = false;

		if (fast)
		{
#define POINT_CHECK(x) bp=curve(x);	thisdist=(bp-p).mag_squared(); if(thisdist<dist) { extreme = (first&&x<0.01); ret=iter; best_len = total_len; dist=thisdist; best_curve=curve; last=true; best_pos=x;}
			POINT_CHECK(0.0001);  POINT_CHECK((1.0/6)); POINT_CHECK((2.0/6)); POINT_CHECK((3.0/6));
			POINT_CHECK((4.0/6)); POINT_CHECK((5.0/6)); POINT_CHECK(0.9999);
		}
		else
		{
			float pos = curve.find_closest(fast, p);
			thisdist=(curve(pos)-p).mag_squared();
			if(thisdist<dist)
			{
				extreme = (first && pos == 0);
				ret=iter;
				dist=thisdist;
				best_pos = pos;
				best_curve = curve;
				best_len = total_len;
				last = true;
			}
		}
		total_len += curve.length();
		first = false;
	}

	t = best_pos;
	if (fast)
	{
		len = best_len + best_curve.find_distance(0,best_curve.find_closest(fast, p));
		if (last && t > .99) extreme = true;
	}
	else
	{
		len = best_len + best_curve.find_distance(0,best_pos);
		if (last && t == 1) extreme = true;
	}
	return ret;
}

/* === M E T H O D S ======================================================= */

inline void
CurveWarp::sync()
{
	curve_length_=calculate_distance(bline);
	perp_ = (end_point - start_point).perp().norm();
}

CurveWarp::CurveWarp():
	origin(0,0),
	perp_width(1),
	start_point(-2.5,-0.5),
	end_point(2.5,-0.3),
	fast(true)
{
	bline.push_back(BLinePoint());
	bline.push_back(BLinePoint());
	bline[0].set_vertex(Point(-2.5,0));
	bline[1].set_vertex(Point( 2.5,0));
	bline[0].set_tangent(Point(1,  0.1));
	bline[1].set_tangent(Point(1, -0.1));
	bline[0].set_width(1.0f);
	bline[1].set_width(1.0f);

	sync();
	Layer::Vocab voc(get_param_vocab());
	Layer::fill_static(voc);
}

inline Point
CurveWarp::transform(const Point &point_, Real *dist, Real *along, int quality)const
{
	Vector tangent;
	Vector diff;
	Point p1;
	Real thickness;
	bool edge_case = false;
	float len(0);
	bool extreme;
	float t;

	if(bline.size()==0)
		return Point();
	else if(bline.size()==1)
	{
		tangent=bline.front().get_tangent1();
		p1=bline.front().get_vertex();
		thickness=bline.front().get_width();
		t = 0.5;
		extreme = false;
	}
	else
	{
		Point point(point_-origin);

		std::vector<synfig::BLinePoint>::const_iterator iter,next;

		// Figure out the BLinePoint we will be using,
		next=find_closest_to_bline(fast,bline,point,t,len,extreme);

		iter=next++;
		if(next==bline.end()) next=bline.begin();

		// Setup the curve
		etl::hermite<Vector> curve(iter->get_vertex(), next->get_vertex(), iter->get_tangent2(), next->get_tangent1());

		// Setup the derivative function
		etl::derivative<etl::hermite<Vector> > deriv(curve);

		int search_iterations(7);

		if(quality<=6)search_iterations=7;
		else if(quality<=7)search_iterations=6;
		else if(quality<=8)search_iterations=5;
		else search_iterations=4;

		// Figure out the closest point on the curve
		if (fast) t = curve.find_closest(fast, point,search_iterations);

		// Calculate our values
		p1=curve(t);			 // the closest point on the curve
		tangent=deriv(t);		 // the tangent at that point

		// if the point we're nearest to is at either end of the
		// bline, our distance from the curve is the distance from the
		// point on the curve.  we need to know which side of the
		// curve we're on, so find the average of the two tangents at
		// this point
		if (t<0.00001 || t>0.99999)
		{
			bool zero_tangent = (tangent[0] == 0 && tangent[1] == 0);

			if (t<0.5)
			{
				if (iter->get_split_tangent_flag() || zero_tangent)
				{
					// fake the current tangent if we need to
					if (zero_tangent) tangent = curve(FAKE_TANGENT_STEP) - curve(0);

					// calculate the other tangent
					Vector other_tangent(iter->get_tangent1());
					if (other_tangent[0] == 0 && other_tangent[1] == 0)
					{
						// find the previous blinepoint
						std::vector<synfig::BLinePoint>::const_iterator prev;
						if (iter != bline.begin()) (prev = iter)--;
						else prev = iter;

						etl::hermite<Vector> other_curve(prev->get_vertex(), iter->get_vertex(), prev->get_tangent2(), iter->get_tangent1());
						other_tangent = other_curve(1) - other_curve(1-FAKE_TANGENT_STEP);
					}

					// normalise and sum the two tangents
					tangent=(other_tangent.norm()+tangent.norm());
					edge_case=true;
				}
			}
			else
			{
				if (next->get_split_tangent_flag() || zero_tangent)
				{
					// fake the current tangent if we need to
					if (zero_tangent) tangent = curve(1) - curve(1-FAKE_TANGENT_STEP);

					// calculate the other tangent
					Vector other_tangent(next->get_tangent2());
					if (other_tangent[0] == 0 && other_tangent[1] == 0)
					{
						// find the next blinepoint
						std::vector<synfig::BLinePoint>::const_iterator next2(next);
						if (++next2 == bline.end())
							next2 = next;

						etl::hermite<Vector> other_curve(next->get_vertex(), next2->get_vertex(), next->get_tangent2(), next2->get_tangent1());
						other_tangent = other_curve(FAKE_TANGENT_STEP) - other_curve(0);
					}

					// normalise and sum the two tangents
					tangent=(other_tangent.norm()+tangent.norm());
					edge_case=true;
				}
			}
		}
		tangent = tangent.norm();

		// the width of the bline at the closest point on the curve
		thickness=(next->get_width()-iter->get_width())*t+iter->get_width();
	}

	if (thickness < TOO_THIN && thickness > -TOO_THIN)
	{
		if (thickness > 0) thickness = TOO_THIN;
		else thickness = -TOO_THIN;
	}

	if (extreme)
	{
		Vector tangent;

		if (t < 0.5)
		{
			std::vector<synfig::BLinePoint>::const_iterator iter(bline.begin());
			tangent = iter->get_tangent1().norm();
			len = 0;
		}
		else
		{
			std::vector<synfig::BLinePoint>::const_iterator iter(--bline.end());
			tangent = iter->get_tangent2().norm();
			len = curve_length_;
		}
		len += (point_-origin - p1)*tangent;
		diff = tangent.perp();
	}
	else if (edge_case)
	{
		diff=(p1-(point_-origin));
		if(diff*tangent.perp()<0) diff=-diff;
		diff=diff.norm();
	}
	else
		diff=tangent.perp();

	// diff is a unit vector perpendicular to the bline
	const Real unscaled_distance((point_-origin - p1)*diff);
	if (dist) *dist = unscaled_distance;
	if (along) *along = len;
	return ((start_point + (end_point - start_point) * len / curve_length_) +
			perp_ * unscaled_distance/(thickness*perp_width));
}

synfig::Layer::Handle
CurveWarp::hit_check(synfig::Context context, const synfig::Point &point)const
{
	return context.hit_check(transform(point));
}

bool
CurveWarp::set_param(const String & param, const ValueBase &value)
{
	IMPORT(origin);
	IMPORT(start_point);
	IMPORT(end_point);
	IMPORT(fast);
	IMPORT(perp_width);

	if(param=="bline" && value.get_type()==ValueBase::TYPE_LIST)
	{
		bline=value;
		sync();

		return true;
	}

	IMPORT_AS(origin,"offset");

	return false;
}

ValueBase
CurveWarp::get_param(const String & param)const
{
	EXPORT(origin);
	EXPORT(start_point);
	EXPORT(end_point);
	EXPORT(bline);
	EXPORT(fast);
	EXPORT(perp_width);

	EXPORT_NAME();
	EXPORT_VERSION();

	return ValueBase();
}

Layer::Vocab
CurveWarp::get_param_vocab()const
{
	Layer::Vocab ret;

	ret.push_back(ParamDesc("origin")
				  .set_local_name(_("Origin"))
				  .set_description(_("Position of the source line"))
	);
	ret.push_back(ParamDesc("perp_width")
				  .set_local_name(_("Width"))
				  .set_origin("start_point")
				  .set_description(_("How much is expanded the result perpendicular to the source line"))
	);
	ret.push_back(ParamDesc("start_point")
				  .set_local_name(_("Start Point"))
				  .set_connect("end_point")
				  .set_description(_("First point of the source line"))
	);
	ret.push_back(ParamDesc("end_point")
				  .set_local_name(_("End Point"))
				  .set_description(_("Final point of the source line"))
	);
	ret.push_back(ParamDesc("bline")
				  .set_local_name(_("Vertices"))
				  .set_origin("origin")
				  .set_hint("width")
				  .set_description(_("List of BLine Points where the source line is curved to"))
	);
	ret.push_back(ParamDesc("fast")
				  .set_local_name(_("Fast"))
				  .set_description(_("When checked, renders quickly but with artifacts"))
	);
	return ret;
}

Color
CurveWarp::get_color(Context context, const Point &point)const
{
	return context.get_color(transform(point));
}

bool
CurveWarp::accelerated_render(Context context,Surface *surface,int quality, const RendDesc &renddesc, ProgressCallback *cb)const
{
	SuperCallback stageone(cb,0,9000,10000);
	SuperCallback stagetwo(cb,9000,10000,10000);

	int x,y;

	const Real pw(renddesc.get_pw()),ph(renddesc.get_ph());
	Point tl(renddesc.get_tl());
	Point br(renddesc.get_br());
	const int w(renddesc.get_w());
	const int h(renddesc.get_h());

	// find a bounding rectangle for the context we need to render
	// todo: find a better way of doing this - this way doesn't work
	Rect src_rect(transform(tl));
	Point pos1, pos2;
	Real dist, along;
	Real min_dist(999999), max_dist(-999999), min_along(999999), max_along(-999999);

#define UPDATE_DIST \
	if (dist < min_dist) min_dist = dist; \
	if (dist > max_dist) max_dist = dist; \
	if (along < min_along) min_along = along; \
	if (along > max_along) max_along = along

	// look along the top and bottom edges
	pos1[0] = pos2[0] = tl[0]; pos1[1] = tl[1]; pos2[1] = br[1];
	for (x = 0; x < w; x++, pos1[0] += pw, pos2[0] += pw)
	{
		src_rect.expand(transform(pos1, &dist, &along)); UPDATE_DIST;
		src_rect.expand(transform(pos2, &dist, &along)); UPDATE_DIST;
	}

	// look along the left and right edges
	pos1[0] = tl[0]; pos2[0] = br[0]; pos1[1] = pos2[1] = tl[1];
	for (y = 0; y < h; y++, pos1[1] += ph, pos2[1] += ph)
	{
		src_rect.expand(transform(pos1, &dist, &along)); UPDATE_DIST;
		src_rect.expand(transform(pos2, &dist, &along)); UPDATE_DIST;
	}

	// look along the diagonals
	const int max_wh(std::max(w,h));
	const Real inc_x((br[0]-tl[0])/max_wh),inc_y((br[1]-tl[1])/max_wh);
	pos1[0] = pos2[0] = tl[0]; pos1[1] = tl[1]; pos2[1] = br[1];
	for (x = 0; x < max_wh; x++, pos1[0] += inc_x, pos2[0] = pos1[0], pos1[1]+=inc_y, pos2[1]-=inc_y)
	{
		src_rect.expand(transform(pos1, &dist, &along)); UPDATE_DIST;
		src_rect.expand(transform(pos2, &dist, &along)); UPDATE_DIST;
	}

#if 0
	// look at each blinepoint
	std::vector<synfig::BLinePoint>::const_iterator iter;
	for (iter=bline.begin(); iter!=bline.end(); iter++)
		src_rect.expand(transform(iter->get_vertex()+origin, &dist, &along)); UPDATE_DIST;
#endif

	Point src_tl(src_rect.get_min());
	Point src_br(src_rect.get_max());

	Vector ab((end_point - start_point).norm());
	Angle::tan ab_angle(ab[1], ab[0]);

	Real used_length = max_along - min_along;
	Real render_width = max_dist - min_dist;

	int src_w = (abs(used_length*Angle::cos(ab_angle).get()) +
				 abs(render_width*Angle::sin(ab_angle).get())) / abs(pw);
	int src_h = (abs(used_length*Angle::sin(ab_angle).get()) +
				 abs(render_width*Angle::cos(ab_angle).get())) / abs(ph);

	Real src_pw((src_br[0] - src_tl[0]) / src_w);
	Real src_ph((src_br[1] - src_tl[1]) / src_h);

	if (src_pw > abs(pw))
	{
		src_w = int((src_br[0] - src_tl[0]) / abs(pw));
		src_pw = (src_br[0] - src_tl[0]) / src_w;
	}

	if (src_ph > abs(ph))
	{
		src_h = int((src_br[1] - src_tl[1]) / abs(ph));
		src_ph = (src_br[1] - src_tl[1]) / src_h;
	}

#define MAXPIX 10000
	if (src_w > MAXPIX) src_w = MAXPIX;
	if (src_h > MAXPIX) src_h = MAXPIX;

	// this is an attempt to remove artifacts around tile edges - the
	// cubic interpolation uses at most 2 pixels either side of the
	// target pixel, so add an extra 2 pixels around the tile on all
	// sides
	src_tl -= (Point(src_pw,src_ph)*2);
	src_br += (Point(src_pw,src_ph)*2);
	src_w += 4;
	src_h += 4;
	src_pw = (src_br[0] - src_tl[0]) / src_w;
	src_ph = (src_br[1] - src_tl[1]) / src_h;

	// set up a renddesc for the context to render
	RendDesc src_desc(renddesc);
	src_desc.clear_flags();
	src_desc.set_tl(src_tl);
	src_desc.set_br(src_br);
	src_desc.set_wh(src_w, src_h);

	// render the context onto a new surface
	Surface source;
	source.set_wh(src_w,src_h);
	if(!context.accelerated_render(&source,quality,src_desc,&stageone))
		return false;

	float u,v;
	Point pos, tmp;

	surface->set_wh(w,h);
	surface->clear();

	if(quality<=4)				// CUBIC
		for(y=0,pos[1]=tl[1];y<h;y++,pos[1]+=ph)
		{
			for(x=0,pos[0]=tl[0];x<w;x++,pos[0]+=pw)
			{
				tmp=transform(pos);
				u=(tmp[0]-src_tl[0])/src_pw;
				v=(tmp[1]-src_tl[1])/src_ph;
				if(u<0 || v<0 || u>=src_w || v>=src_h || isnan(u) || isnan(v))
					(*surface)[y][x]=context.get_color(tmp);
				else
					(*surface)[y][x]=source.cubic_sample(u,v);
			}
			if((y&31)==0 && cb && !stagetwo.amount_complete(y,h)) return false;
		}
	else if (quality<=6)		// INTERPOLATION_LINEAR
		for(y=0,pos[1]=tl[1];y<h;y++,pos[1]+=ph)
		{
			for(x=0,pos[0]=tl[0];x<w;x++,pos[0]+=pw)
			{
				tmp=transform(pos);
				u=(tmp[0]-src_tl[0])/src_pw;
				v=(tmp[1]-src_tl[1])/src_ph;
				if(u<0 || v<0 || u>=src_w || v>=src_h || isnan(u) || isnan(v))
					(*surface)[y][x]=context.get_color(tmp);
				else
					(*surface)[y][x]=source.linear_sample(u,v);
			}
			if((y&31)==0 && cb && !stagetwo.amount_complete(y,h)) return false;
		}
	else						// NEAREST_NEIGHBOR
		for(y=0,pos[1]=tl[1];y<h;y++,pos[1]+=ph)
		{
			for(x=0,pos[0]=tl[0];x<w;x++,pos[0]+=pw)
			{
				tmp=transform(pos);
				u=(tmp[0]-src_tl[0])/src_pw;
				v=(tmp[1]-src_tl[1])/src_ph;
				if(u<0 || v<0 || u>=src_w || v>=src_h || isnan(u) || isnan(v))
					(*surface)[y][x]=context.get_color(tmp);
				else
					(*surface)[y][x]=source[floor_to_int(v)][floor_to_int(u)];
			}
			if((y&31)==0 && cb && !stagetwo.amount_complete(y,h)) return false;
		}

	// Mark our progress as finished
	if(cb && !cb->amount_complete(10000,10000))
		return false;

	return true;
}
