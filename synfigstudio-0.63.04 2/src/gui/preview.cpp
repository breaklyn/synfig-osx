/* === S Y N F I G ========================================================= */
/*!	\file preview.cpp
**	\brief Preview implementation file
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007 Chris Moore
**  Copyright (c) 2011 Nikita Kitaev
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

#include "preview.h"
#include "app.h"
#include "audiocontainer.h"
#include <gtkmm/stock.h>
#include <gtkmm/separator.h>
#include <gdkmm/general.h>

#include <synfig/target_scanline.h>
#include <synfig/surface.h>

#include <algorithm>
#include "asyncrenderer.h"

#include "general.h"

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;
using namespace studio;

/* === M A C R O S ========================================================= */

/* === G L O B A L S ======================================================= */

/* === P R O C E D U R E S ================================================= */

/* === M E T H O D S ======================================================= */

/* === E N T R Y P O I N T ================================================= */

class studio::Preview::Preview_Target : public Target_Scanline
{
	Surface	surface;

	sigc::signal<void, const Preview_Target *>		signal_frame_done_;

	int scanline;

	double	tbegin,tend;

	int		nframes,curframe;

public:

	Preview_Target()
	{
		set_remove_alpha();
		tbegin = tend = 0;
		scanline = 0;
		nframes = curframe = 0;
	}

	const RendDesc &get_rend_desc() const { return desc; }

	virtual bool set_rend_desc(RendDesc *r)
	{
		if(Target_Scanline::set_rend_desc(r))
		{
			/*synfig::warning("Succeeded in setting the desc to new one: %d x %d, %.2f fps [%.2f,%.2f]",
							desc.get_w(),desc.get_h(),desc.get_frame_rate(),
					(float)desc.get_time_start(),(float)desc.get_time_end());*/

			surface.set_wh(desc.get_w(),desc.get_h());

			curframe = 0;
			nframes = (int)floor((desc.get_time_end() - desc.get_time_start())*desc.get_frame_rate());

			tbegin = desc.get_time_start();
			tend = tbegin + nframes/desc.get_frame_rate();

			return true;
		}
		return false;
	}

	virtual bool start_frame(ProgressCallback */*cb*/=NULL)
	{
		return true;
	}

	virtual void end_frame()
	{
		//ok... notify our subscribers...
		signal_frame_done_(this);
		curframe += 1;
		//synfig::warning("Finished the frame stuff, and changed time to %.3f",t);
	}

	virtual Color * start_scanline(int scanline)
	{
		return surface[scanline];
	}

	virtual bool end_scanline() {return true;}

	sigc::signal<void, const Preview_Target *>	&signal_frame_done() {return signal_frame_done_;}

	const Surface &get_surface() const {return surface;}

	float get_time() const
	{
		double time = ((nframes-curframe)/(double)nframes)*tbegin
					+ ((curframe)/(double)nframes)*tend;
		return time;
	}
};

studio::Preview::Preview(const studio::CanvasView::LooseHandle &h, float zoom, float f)
:canvasview(h),zoom(zoom),fps(f)
{
	overbegin = false;
	overend = false;
}

void studio::Preview::set_canvasview(const studio::CanvasView::LooseHandle &h)
{
	canvasview = h;

	if(canvasview)
	{
		//perhaps reset override values...
		const RendDesc &r = canvasview->get_canvas()->rend_desc();
		if(r.get_frame_rate())
		{
			float rate = 1/r.get_frame_rate();
			overbegin = false; begintime = r.get_time_start() + r.get_frame_start()*rate;
			overend = false; endtime = r.get_time_start() + r.get_frame_end()*rate;
		}
	}
}

studio::Preview::~Preview()
{
	signal_destroyed_(this); //tell anything that attached to us, we're dying
}

void studio::Preview::render()
{
	if(canvasview)
	{
		//render using the preview target
		etl::handle<Preview_Target>	target = new Preview_Target;

		//connect our information to his...
		//synfig::warning("Connecting to the end frame function...");
		target->signal_frame_done().connect(sigc::mem_fun(*this,&Preview::frame_finish));

		//set the options
		//synfig::warning("Setting Canvas");
		target->set_canvas(get_canvas());
		target->set_quality(quality);

		//render description
		RendDesc desc = get_canvas()->rend_desc();

		//set the global fps of the preview
		set_global_fps(desc.get_frame_rate());

		desc.clear_flags();

		int neww = (int)floor(desc.get_w()*zoom+0.5),
			newh = (int)floor(desc.get_h()*zoom+0.5);
		float newfps = fps;

		/*synfig::warning("Setting the render description: %d x %d, %f fps, [%f,%f]",
						neww,newh,newfps, overbegin?begintime:(float)desc.get_time_start(),
						overend?endtime:(float)desc.get_time_end());*/

		desc.set_w(neww);
		desc.set_h(newh);
		desc.set_frame_rate(newfps);

		if(overbegin)
		{
			desc.set_time_start(std::max(begintime,(float)desc.get_time_start()));
			//synfig::warning("Set start time to %.2f...",(float)desc.get_time_start());
		}
		if(overend)
		{
			desc.set_time_end(std::min(endtime,(float)desc.get_time_end()));
			//synfig::warning("Set end time to %.2f...",(float)desc.get_time_end());
		}

		//setting the description

		//HACK - add on one extra frame because the renderer can't render the last frame
		desc.set_time_end(desc.get_time_end() + 1.000001/fps);

		target->set_rend_desc(&desc);

		//... first we must clear our current selves of space
		frames.resize(0);

		//now tell it to go... with inherited prog. reporting...
		//synfig::info("Rendering Asynchronously...");
		if(renderer) renderer->stop();
		renderer = new AsyncRenderer(target);
		renderer->start();
	}
}

static void free_guint8(const guint8 *mem)
{
	free((void*)mem);
}

void studio::Preview::frame_finish(const Preview_Target *targ)
{
	//copy image with time to next frame (can just push back)
	FlipbookElem	fe;
	float time = targ->get_time();
	const Surface &surf = targ->get_surface();
	const RendDesc& r = targ->get_rend_desc();

	//synfig::warning("Finished a frame at %f s",time);

	//copy EVERYTHING!
	PixelFormat pf(PF_RGB);
	const int total_bytes(r.get_w()*r.get_h()*synfig::channels(pf));

	//synfig::warning("Creating a buffer");
	unsigned char *buffer((unsigned char*)malloc(total_bytes));

	if(!buffer)
		return;

	//convert all the pixels to the pixbuf... buffer... thing...
	//synfig::warning("Converting...");
	convert_color_format(buffer, surf[0], surf.get_w()*surf.get_h(), pf, App::gamma);

	//load time
	fe.t = time;
	//uses and manages the memory for the buffer...
	//synfig::warning("Create a pixmap...");
	fe.buf =
	Gdk::Pixbuf::create_from_data(
		buffer,	// pointer to the data
		Gdk::COLORSPACE_RGB, // the colorspace
		((pf&PF_A)==PF_A), // has alpha?
		8, // bits per sample
		surf.get_w(),	// width
		surf.get_h(),	// height
		surf.get_w()*synfig::channels(pf), // stride (pitch)
		sigc::ptr_fun(free_guint8)
	);

	//add the flipbook element to the list (assume time is correct)
	//synfig::info("Prev: Adding %f s to the list", time);
	frames.push_back(fe);

	signal_changed()();
}

#define IMAGIFY_BUTTON(button,stockid,tooltip) \
        icon = manage(new Gtk::Image(Gtk::StockID(stockid), Gtk::ICON_SIZE_BUTTON)); \
	button->set_tooltip_text(tooltip); \
        button->add(*icon); \
        button->set_relief(Gtk::RELIEF_NONE); \
        button->show(); \
	icon->set_padding(0,0); \
	icon->show();

Widget_Preview::Widget_Preview():
	Gtk::Table(1, 5),
	adj_time_scrub(0, 0, 1000, 0, 10, 0),
	scr_time_scrub(adj_time_scrub),
	b_loop(/*_("Loop")*/),
	currentindex(0),
	audiotime(0),
	adj_sound(0, 0, 4),
	l_lasttime("0s"),
	playing(false)
{
	//connect to expose events
	//signal_expose_event().connect(sigc::mem_fun(*this, &studio::Widget_Preview::redraw));

	//manage all the change in values etc...
	adj_time_scrub.signal_value_changed().connect(sigc::mem_fun(*this,&Widget_Preview::slider_move));
	scr_time_scrub.signal_event().connect(sigc::mem_fun(*this,&Widget_Preview::scroll_move_event));
	draw_area.signal_expose_event().connect(sigc::mem_fun(*this,&Widget_Preview::redraw));
	
	scr_time_scrub.set_draw_value(0);

	disp_sound.set_time_adjustment(&adj_sound);
	timedisp = -1;

	//Set up signals to modify time value as it should be...
	disp_sound.signal_start_scrubbing().connect(sigc::mem_fun(*this,&Widget_Preview::scrub_updated));
	disp_sound.signal_scrub().connect(sigc::mem_fun(*this,&Widget_Preview::scrub_updated));


	Gtk::Button *button = 0;
	Gtk::Image *icon = 0;

	#if 1

	//2nd row: prevframe play/pause nextframe loop | halt-render re-preview erase-all  
	Gtk::HBox *controller = manage(new Gtk::HBox);

	//prev rendered frame
	Gtk::Button *prev_framebutton;
	Gtk::Image *icon0 = manage(new Gtk::Image(Gtk::StockID("synfig-animate_seek_prev_frame"), Gtk::ICON_SIZE_BUTTON));
	prev_framebutton = manage(new class Gtk::Button());
	prev_framebutton->set_tooltip_text(_("Prev frame"));
	icon0->set_padding(0,0);
	icon0->show();
	prev_framebutton->add(*icon0);
	prev_framebutton->set_relief(Gtk::RELIEF_NONE);
	prev_framebutton->show();
	prev_framebutton->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this,&Widget_Preview::seek_frame), -1));

	controller->pack_start(*prev_framebutton, Gtk::PACK_SHRINK, 0);

	//play pause
	Gtk::Image *icon1 = manage(new Gtk::Image(Gtk::StockID("synfig-animate_play"), Gtk::ICON_SIZE_BUTTON));
	play_pausebutton = manage(new class Gtk::Button());
	play_pausebutton->set_tooltip_text(_("Play"));
	icon1->set_padding(0,0);
	icon1->show();
	play_pausebutton->add(*icon1);
	play_pausebutton->set_relief(Gtk::RELIEF_NONE);
	play_pausebutton->show();
	play_pausebutton->signal_clicked().connect(sigc::mem_fun(*this,&Widget_Preview::on_play_pause_pressed));

	controller->pack_start(*play_pausebutton, Gtk::PACK_SHRINK, 0);

	//next rendered frame
	Gtk::Button *next_framebutton;
	Gtk::Image *icon2 = manage(new Gtk::Image(Gtk::StockID("synfig-animate_seek_next_frame"), Gtk::ICON_SIZE_BUTTON));
	next_framebutton = manage(new class Gtk::Button());
	next_framebutton->set_tooltip_text(_("Next frame"));
	icon2->set_padding(0,0);
	icon2->show();
	next_framebutton->add(*icon2);
	next_framebutton->set_relief(Gtk::RELIEF_NONE);
	next_framebutton->show();
	next_framebutton->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this,&Widget_Preview::seek_frame), 1));

	controller->pack_start(*next_framebutton, Gtk::PACK_SHRINK, 0);

	//spacing
	Gtk::Alignment *space = Gtk::manage(new Gtk::Alignment());
	space->set_size_request(8);
	controller->pack_start(*space, false, true);


	//loop
	button = &b_loop;
	IMAGIFY_BUTTON(button,"synfig-animate_loop", _("Loop"));
	controller->pack_start(b_loop, Gtk::PACK_SHRINK,0);

	//spacing
	Gtk::Alignment *space1 = Gtk::manage(new Gtk::Alignment());
        space1->set_size_request(24);
        controller->pack_start(*space1, false, true);


	//halt render
	button = manage(new Gtk::Button(/*_("Halt Render")*/));
	button->signal_clicked().connect(sigc::mem_fun(*this,&Widget_Preview::stoprender));
	IMAGIFY_BUTTON(button,Gtk::Stock::STOP, _("Halt render"));

	controller->pack_start(*button, Gtk::PACK_SHRINK, 0);

	//re-preview
	button = manage(new Gtk::Button(/*_("Re-Preview")*/));
	button->signal_clicked().connect(sigc::mem_fun(*this,&Widget_Preview::repreview));
	IMAGIFY_BUTTON(button, Gtk::Stock::EDIT, _("Re-preview"));

	controller->pack_start(*button, Gtk::PACK_SHRINK, 0);

	//erase all
	button = manage(new Gtk::Button(/*_("Erase All")*/));
	button->signal_clicked().connect(sigc::mem_fun(*this,&Widget_Preview::eraseall));
	IMAGIFY_BUTTON(button, Gtk::Stock::CLEAR, _("Erase all rendered frame(s)"));

	controller->pack_start(*button, Gtk::PACK_SHRINK, 0);

	controller->show_all();

	//3rd row: last rendered frame
	Gtk::HBox *lastrendered = manage(new Gtk::HBox);
	Gtk::Label *label = manage(new Gtk::Label(_("Last rendered: ")));
	lastrendered->pack_start(*label, Gtk::PACK_SHRINK, 10);
	//l_lasttime.show();
	lastrendered->pack_start(l_lasttime, Gtk::PACK_SHRINK, 0);
	lastrendered->show_all();

	//5th row: sound track
	disp_sound.set_size_request(-1,32);

	// attach all widgets	
	attach(draw_area, 0, 1, 0, 1, Gtk::EXPAND|Gtk::FILL, Gtk::EXPAND|Gtk::FILL, 0);
	attach(scr_time_scrub, 0, 1, 1, 2, Gtk::EXPAND|Gtk::FILL, Gtk::SHRINK);
	attach(*controller, 0, 1, 2, 3, Gtk::EXPAND|Gtk::FILL, Gtk::SHRINK|Gtk::FILL);
	attach(*lastrendered, 0, 1, 3, 4, Gtk::EXPAND|Gtk::FILL, Gtk::SHRINK);
//	attach(disp_sound,0,1,4,5,Gtk::EXPAND|Gtk::FILL,Gtk::SHRINK);
	show_all();

	//if(draw_area.get_window()) gc_area = Gdk::GC::create(draw_area.get_window());
	#endif
}

studio::Widget_Preview::~Widget_Preview()
{
}

void studio::Widget_Preview::update()
{
	//the meat goes in this locker...
	double time = adj_time_scrub.get_value();

	//find the frame and display it...
	if(preview)
	{
		//synfig::warning("Updating at %.3f s",time);

		//use time to find closest frame...
		studio::Preview::FlipBook::const_iterator 	beg = preview->begin(),end = preview->end();
		studio::Preview::FlipBook::const_iterator 	i;

		i = beg;

		//go to current hint if need be...
		if(currentindex >= 0 && currentindex < (int)preview->numframes())
		{
			i = beg+currentindex;
		}

		//we can't have a picture if there are none to get
		if(beg != end)
		{
			//don't bother with binary search it will just be slower...

			//synfig::info("Search for time %f",time);

			//incrementally go in either direction
			//(bias downward towards beg, because that's what we want)
			for(;i != end;++i)
			{
				//synfig::info("Look at %f",i->t);
				if(i->t > time) break;
				//synfig::info("Go past...");
			}

			//if(i!=beg)--i;

			//bias down, so we can't be at end... and it still is valid...
			for(;i != beg;)
			{
				--i;
				//synfig::info("Look at %f",i->t);
				if(i->t <= time) break;
				//synfig::info("Go past...");
			}

			/*i = preview->begin(); end = preview->end();
			if(i == end) return;

			j = i;
			for(;i != end; j = i++)
			{
				if(i->t > time) break;
			}*/

			//we should be at a valid edge since we biased downward

			//don't get the closest, round down... (if we can)
			if(i == end)
			{
				synfig::error("i == end....");
				//assert(0);
				currentbuf.clear();
				currentindex = 0;
				timedisp = -1;
			}else
			{
				currentbuf = i->buf;
				currentindex = i-beg;
				if(timedisp != i->t)
				{
					timedisp = i->t;
					//synfig::warning("Update at: %f seconds (%f s)",time,timedisp);
					preview_draw();
					//synfig::warning("success!");
				}
			}
		}
	}

	if(disp_sound.get_profile() && adj_sound.get_value() != time)
	{
		//timeupdate = time;

		//Set the position of the sound (short circuited for sound modifying the time)

		disp_sound.set_position(time);
		disp_sound.queue_draw();
	}
}
void studio::Widget_Preview::preview_draw()
{
	draw_area.queue_draw();//on_expose_event();
}

bool studio::Widget_Preview::redraw(GdkEventExpose */*heh*/)
{
	//And render the drawing area
	Glib::RefPtr<Gdk::Pixbuf> pxnew, px = currentbuf;

	if(!px || draw_area.get_height() == 0
		|| px->get_height() == 0 || px->get_width() == 0 /*|| is_visible()*/) //made not need this line
		return true;

	//figure out the scaling factors...
	float sx, sy;
	int nw,nh;

	sx = draw_area.get_width() / (float)px->get_width();
	sy = draw_area.get_height() / (float)px->get_height();

	//synfig::info("widget_preview redraw: now to scale the bitmap: %.3f x %.3f",sx,sy);

	//round to smallest scale (fit entire thing in window without distortion)
	if(sx > sy) sx = sy;
	//else sy = sx;

	//scale to a new pixmap and then copy over to the window
	nw = (int)(px->get_width()*sx);
	nh = (int)(px->get_height()*sx);

	if(nw == 0 || nh == 0)return true;

	pxnew = px->scale_simple(nw,nh,Gdk::INTERP_NEAREST);

	//synfig::info("Now to draw to the window...");
	//copy to window
	Glib::RefPtr<Gdk::Window>	wind = draw_area.get_window();
	Cairo::RefPtr<Cairo::Context> cr = wind->create_cairo_context();

	if(!wind) synfig::warning("The destination window is broken...");

	{
		/* Options for drawing...
			1) store with alpha, then clear and render with alpha every frame
				- more time consuming
				+ more expandable
			2) store with just pixel info
				- less expandable
				+ faster
				+ better memory footprint
		*/
		//px->composite(const Glib::RefPtr<Gdk::Pixbuf>& dest, int dest_x, int dest_y, int dest_width, int dest_height, double offset_x, double offset_y, double scale_x, double scale_y, InterpType interp_type, int overall_alpha) const

		cr->save();
		Gdk::Cairo::set_source_pixbuf(
			cr, //cairo context
			pxnew, //pixbuf
			//coordinates to place center of the preview window
			(draw_area.get_width() - nw) / 2, (draw_area.get_height() - nh) / 2
			);
		cr->paint();
		cr->restore();

		if(timedisp >= 0)
		{
			Glib::RefPtr<Pango::Layout> layout(Pango::Layout::create(get_pango_context()));
			Glib::ustring timecode(Time((double)timedisp).round(preview->get_global_fps())
			.get_string(preview->get_global_fps(),
			App::get_time_format()));
			//synfig::info("Time for preview draw is: %s for time %g", timecode.c_str(), adj_time_scrub.get_value());

			cr->save();

			layout->set_text(timecode);

			cr->set_source_rgb(1,0,0);
			cr->move_to(4,4);
			layout->show_in_cairo_context(cr);
			cr->stroke();

			cr->restore();
		}
	}

	//synfig::warning("Refresh the draw area");
	//make sure the widget refreshes

	return false;
}

bool studio::Widget_Preview::play_update()
{
	float diff = timer.pop_time();
	//synfig::info("Play update: diff = %.2f",diff);

	if(playing)
	{
		//we go to the next one...
		double time = adj_time_scrub.get_value() + diff;

		//adjust it to be synced with the audio if it can...
		{
			double newtime = audiotime;
			if(audio && audio->is_playing()) audio->get_current_time(newtime);

			if(newtime != audiotime)
			{
				//synfig::info("Adjusted time from %.3lf to %.3lf", time,newtime);
				time = audiotime = newtime;
			}
		}

		//Looping conditions...
		if(time >= adj_time_scrub.get_upper())
		{
			if(get_loop_flag())
			{
				time = adj_time_scrub.get_lower();// + time-adj_time_scrub.get_upper();
				currentindex = 0;
			}else
			{
				time = adj_time_scrub.get_upper();
				adj_time_scrub.set_value(time);
				play_pause();
				update();

				//synfig::info("Play Stopped: time set to %f",adj_time_scrub.get_value());
				return false;
			}
		}

		//set the new time...
		adj_time_scrub.set_value(time);
		adj_time_scrub.value_changed();

		//update the window to the correct image we might want to do this later...
		//update();
		//synfig::warning("Did update pu");
	}
	return true;
}

void studio::Widget_Preview::slider_move()
{
	//if(!playing)
	{
		update();
		//synfig::warning("Did update sm");
	}
}

//for other things updating the value changed signal...
void studio::Widget_Preview::scrub_updated(double t)
{
	pause();

	//Attempt at being more accurate... the time is adjusted to be exactly where the sound says it is
	//double oldt = t;
	if(audio)
	{
		if(!audio->isPaused())
		{
			audio->get_current_time(t);
		}
	}

	//synfig::info("Scrubbing to %.3f, setting adj to %.3f",oldt,t);

	if(adj_time_scrub.get_value() != t)
	{
		adj_time_scrub.set_value(t);
		adj_time_scrub.value_changed();
	}
}

void studio::Widget_Preview::disconnect_preview(Preview *prev)
{
	if(prev == preview)
	{
		preview = 0;
		prevchanged.disconnect();
	}
}

void studio::Widget_Preview::set_preview(etl::handle<Preview>	prev)
{
	preview = prev;

	synfig::info("Setting preview");

	//stop playing the mini animation...
	pause();

	if(preview)
	{
		//set the internal values
		float rate = preview->get_fps();
		synfig::info("	FPS = %f",rate);
		if(rate)
		{
			float start = preview->get_begintime();
			float end = preview->get_endtime();

			rate = 1/rate;

			adj_time_scrub.set_lower(start);
			adj_time_scrub.set_upper(end);
			adj_time_scrub.set_value(start);
			adj_time_scrub.set_step_increment(rate);
			adj_time_scrub.set_page_increment(10*rate);

			//if the begin time and the end time are the same there is only a single frame
			singleframe = end==start;
		}else
		{
			adj_time_scrub.set_lower(0);
			adj_time_scrub.set_upper(0);
			adj_time_scrub.set_value(0);
			adj_time_scrub.set_step_increment(0);
			adj_time_scrub.set_page_increment(0);
			singleframe = true;
		}

		//connect so future information will be found...
		prevchanged = prev->signal_changed().connect(sigc::mem_fun(*this,&Widget_Preview::whenupdated));
		prev->signal_destroyed().connect(sigc::mem_fun(*this,&Widget_Preview::disconnect_preview));
		update();
		//synfig::warning("Did update sp");
		queue_draw();
	}
}

void studio::Widget_Preview::whenupdated()
{
	l_lasttime.set_text((Time((double)(--preview->end())->t)
							.round(preview->get_global_fps())
							.get_string(preview->get_global_fps(),App::get_time_format())));
	update();
}

void studio::Widget_Preview::clear()
{
	preview = 0;
	prevchanged.disconnect();
}

void studio::Widget_Preview::play()
{
	if(preview)
	{
		//synfig::info("Playing at %lf",adj_time_scrub.get_value());
		//audiotime = adj_time_scrub.get_value();
		playing = true;

		//adj_time_scrub.set_value(adj_time_scrub.get_lower());
		update(); //we don't want to call play update because that will try to advance the timer
		//synfig::warning("Did update p");

		//approximate length of time in seconds, right?
		double rate = /*std::min(*/adj_time_scrub.get_step_increment()/*,1/30.0)*/;
		int timeout = (int)floor(1000*rate);

		//synfig::info("	rate = %.3lfs = %d ms",rate,timeout);

		signal_play_(adj_time_scrub.get_value());

		//play the audio...
		if(audio) audio->play(adj_time_scrub.get_value());

		timecon = Glib::signal_timeout().connect(sigc::mem_fun(*this,&Widget_Preview::play_update),timeout);
		timer.reset();
	}
}

void studio::Widget_Preview::play_pause()
{
	playing = false;
	signal_pause()();
	if(audio) audio->stop(); //!< stop the audio
	//synfig::info("Stopping...");

	Gtk::Image *icon;
	icon = manage(new Gtk::Image(Gtk::StockID("synfig-animate_play"), Gtk::ICON_SIZE_BUTTON));
	play_pausebutton->remove();
	play_pausebutton->add(*icon);
	play_pausebutton->set_tooltip_text(_("Play"));
	icon->set_padding(0,0);
	icon->show();
	
}

void studio::Widget_Preview::pause()
{
	//synfig::warning("stopping");
	play_pause();
	timecon.disconnect();
}

void studio::Widget_Preview::on_play_pause_pressed()
{
	bool play_flag;
	float begin = preview->get_begintime();
	float end = preview->get_endtime();
	float current = adj_time_scrub.get_value();
	Gtk::Image *icon;

	if(!playing)
	{
		play_pausebutton->remove();
		if(current == end) adj_time_scrub.set_value(begin);
		icon = manage(new Gtk::Image(Gtk::StockID("synfig-animate_pause"), Gtk::ICON_SIZE_BUTTON));
		play_pausebutton->set_tooltip_text(_("Pause"));
		play_pausebutton->add(*icon);
		icon->set_padding(0,0);
		icon->show();

		play_flag=true;
	}
	else
	{
		play_flag=false;
	}
	if(play_flag) play(); else pause();
}

void studio::Widget_Preview::seek_frame(int frames)
{
//	if(!frames)	return;

	if(playing) pause();	//pause playing when seek frame called

	float fps = preview->get_fps();
	float currenttime = adj_time_scrub.get_value();
	Time newtime(currenttime+(float)frames/fps);
	newtime = newtime.round(fps);
	
	adj_time_scrub.set_value(newtime);
}

bool studio::Widget_Preview::scroll_move_event(GdkEvent *event)
{
	switch(event->type)
	{
		case GDK_BUTTON_PRESS:
		{
			if(event->button.button == 1 || event->button.button == 3)
			{
				pause();
			}
		}

		default: break;
	}

	return false;
}

void studio::Widget_Preview::set_audioprofile(etl::handle<AudioProfile> p)
{
	disp_sound.set_profile(p);
}

void studio::Widget_Preview::set_audio(etl::handle<AudioContainer> a)
{
	audio = a;

	//disconnect any previous signals
	scrstartcon.disconnect(); scrstopcon.disconnect(); scrubcon.disconnect();

	//connect the new signals
	scrstartcon = disp_sound.signal_start_scrubbing().connect(sigc::mem_fun(*a,&AudioContainer::start_scrubbing));
	scrstopcon = disp_sound.signal_stop_scrubbing().connect(sigc::mem_fun(*a,&AudioContainer::stop_scrubbing));
	scrubcon = disp_sound.signal_scrub().connect(sigc::mem_fun(*a,&AudioContainer::scrub));
}

void studio::Widget_Preview::seek(float t)
{
	pause();
	adj_time_scrub.set_value(t);
}

void studio::Widget_Preview::repreview()
{
	if(preview)
	{
		stoprender();
		pause();
		preview->get_canvasview()->preview_option();
	}
}

void studio::Widget_Preview::stoprender()
{
	if(preview)
	{
		// don't crash if the render has already been stopped
		if (!preview->renderer)
			return;

#ifdef SINGLE_THREADED
		if (preview->renderer->updating)
			preview->renderer->pause();
		else
#endif
			preview->renderer.detach();
	}
}

void studio::Widget_Preview::eraseall()
{
	pause();
	stoprender();

	currentbuf.clear();
	currentindex = 0;
	timedisp = 0;
	queue_draw();

	if(preview)
	{
		preview->clear();
	}
}
