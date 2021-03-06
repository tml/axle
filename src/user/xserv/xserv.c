#include "xserv.h"
#include <kernel/drivers/mouse/mouse.h>
#include <stddef.h>
#include <std/math.h>
#include <std/panic.h>
#include <std/std.h>
#include <gfx/lib/gfx.h>
#include <kernel/util/syscall/sysfuncs.h>
#include <kernel/util/multitasking/tasks/task.h>
#include <kernel/drivers/rtc/clock.h>
#include <kernel/drivers/vesa/vesa.h>
#include <tests/gfx_test.h>
#include <kernel/drivers/kb/kb.h>
#include "animator.h"
#include <user/programs/launcher.h>
#include <user/programs/calculator.h>
#include <user/programs/usage_monitor.h>

Window* create_window_int(Rect frame, bool is_root_window);

//has the screen been modified this refresh?
static char dirtied = 0;
static volatile Window* active_window;
const float shadow_count = 4.0;

void xserv_quit(Screen* screen) {
	switch_to_text();
	gfx_teardown(screen);
	resign_first_responder();
	_kill();
}
void launcher_button_clicked(Button* UNUSED(b)) {
	//launcher_invoke(point_make(20, rect_min_y(b->frame) - 200));
	launcher_invoke(point_make(20, 20));
}

void add_taskbar(Screen* screen) {
	View* content = screen->window->content_view;
	Size taskbar_size = size_make(content->frame.size.width, content->frame.size.height * 0.045);
	Rect border_r = rect_make(point_make(0, 0), size_make(taskbar_size.width, 5));
	taskbar_size.height -= border_r.size.height;

	Point taskbar_origin = point_make(0, content->frame.size.height - taskbar_size.height + border_r.size.height);

	View* taskbar_view = create_view(rect_make(taskbar_origin, taskbar_size));
	taskbar_view->background_color = color_make(245, 120, 80);
	add_subview(content, taskbar_view);

	//add top 'border' to taskbar
	//TODO add window border API
	View* border = create_view(border_r);
	border->background_color = color_make(200, 80, 245);
	add_subview(taskbar_view, border);

	//inner border seperating top and bottom of taskbar
	View* inner_border = create_view(rect_make(point_make(0, border_r.size.height), size_make(content->frame.size.width, 1)));
	inner_border->background_color = color_make(50, 50, 50);
	add_subview(taskbar_view, inner_border);

	Rect usable = rect_make(point_make(0, border_r.origin.y + border_r.size.height), size_make(taskbar_view->frame.size.width, taskbar_view->frame.size.height - border_r.size.height));
	Point name_label_origin = point_make(taskbar_view->frame.size.width * 0.925, usable.origin.y + (usable.size.height / 2) - (CHAR_HEIGHT / 2));
	Rect label_rect = rect_make(name_label_origin, size_make(taskbar_size.width - name_label_origin.x, taskbar_size.height));
	Label* name_label = create_label(label_rect, "axle OS");
	add_sublabel(taskbar_view, name_label);

	//launcher button
	char* launcher_title = "Launcher";
	Button* launcher = create_button(rect_make(point_zero(), size_make(strlen(launcher_title) * CHAR_WIDTH + 20, taskbar_view->frame.size.height)), launcher_title);
	launcher->mousedown_handler = (event_handler)&launcher_button_clicked;
	add_button(taskbar_view, launcher);
}

void add_status_bar(Screen* screen) {
	Rect status_bar_r = rect_make(point_make(0, 0), size_make(screen->window->content_view->frame.size.width, screen->window->frame.size.height * 0.03));
	View* status_bar = create_view(status_bar_r);
	status_bar->background_color = color_make(150, 150, 150);
	add_subview(screen->window->content_view, status_bar);

	//border
	View* border = create_view(rect_make(point_make(0, status_bar_r.size.height - 1), size_make(status_bar_r.size.width, 1)));
	border->background_color = color_purple();
	add_subview(status_bar, border);
}

static Point last_grabbed_window_pos;
static void draw_window_shadow(Screen* screen, Window* window, Point new) {
	Point old = last_grabbed_window_pos;
	float actual_shadow_count = shadow_count;
	if (window->layer->alpha < 1.0) actual_shadow_count = 2.0;
	for (float i = 0; i < actual_shadow_count; i++) {
		int lerp_x = lerp(old.x, new.x, (1 / actual_shadow_count) * i);
		int lerp_y = lerp(old.y, new.y, (1 / actual_shadow_count) * i);
		/*
		if (abs(old.x - new.x) < 2 || abs(old.y - new.y) < 2) {
			continue;
		}
		*/
		Point shadow_loc = point_make(lerp_x, lerp_y);

		//draw snapshot of window
		blit_layer(screen->vmem, window->layer, rect_make(shadow_loc, window->layer->size), rect_make(point_zero(), window->layer->size));
	}
}

static Window* grabbed_window = NULL;
void draw_desktop(Screen* screen) {
	//paint root desktop
	draw_window(screen->window);
	blit_layer(screen->vmem, screen->window->layer, screen->window->frame, screen->window->frame);

	if (screen->window->subviews->size) {
		Window* highest = array_m_lookup(screen->window->subviews, screen->window->subviews->size - 1);

		//find clipping intersections with every window below the highest one
		//redraw every child window at the same time if necessary
		for (int i = 0; i < screen->window->subviews->size - 1; i++) {
			Window* win = (Window*)(array_m_lookup(screen->window->subviews, i));
			draw_window(win);

			//Rect* visible_rects = rect_clip(win->frame, highest->frame);
			//if (!visible_rects || highest->layer->alpha < 1.0) {
				//not occluded by highest at all
				//draw view normally, composite onto vmem
				blit_layer(screen->vmem, win->layer, win->frame, rect_make(point_zero(), win->frame.size));
				/*
			}
			else {
				//maximum 4 sub-rects
				for (int j = 0; j < 4; j++) {
					Rect r = visible_rects[j];
					if (r.size.width == 0 && r.size.height == 0) break;

					Point origin_offset;
					origin_offset.x = abs(win->frame.origin.x - r.origin.x);
					origin_offset.y = abs(win->frame.origin.y - r.origin.y);
					blit_layer(screen->vmem, win->layer, r, rect_make(origin_offset, r.size));
				}
			}
			*/
		}
		
		//finally, composite highest window
		draw_window(highest);
		blit_layer(screen->vmem, highest->layer, highest->frame, rect_make(point_zero(), highest->layer->size));

	}
	if (grabbed_window) {
		draw_window_shadow(screen, grabbed_window, grabbed_window->frame.origin);
	}
}

static void display_about_window(Point origin) {
	//TODO this text should load off a file
	//localization?
	Window* about_win = create_window(rect_make(origin, size_make(800, 300)));
	about_win->title = "About axle";
	Label* body_label = create_label(rect_make(point_make(0, 0), size_make(about_win->content_view->frame.size.width, about_win->content_view->frame.size.height)), "Welcome to axle OS.\n\nVisit www.github.com/codyd51/axle for this OS's source code.\nYou are in axle's window manager, called xserv. xserv is a compositing window manager, meaning window bitmaps are stored offscreen and combined into a final image each frame.\n\tYou can right click anywhere on the desktop to access axle's application launcher. These are a few apps to show what axle and xserv can do, such as load a bitmap from axle's filesystem or display live CPU usage animations.\n\nIf you want to force a full xserv redraw, press 'r'\nIf you want to toggle the transparency of the topmost window between 0.5 and 1, press 'a'\n\nPress ctrl+m any time to log source file dynamic memory usage.\nPress ctrl+p at any time to log CPU usage.\n");
	body_label->font_size = size_make(8, 8);
	add_sublabel(about_win->content_view, body_label);
	present_window(about_win);
}

void desktop_setup(Screen* screen) {
	screen->window = create_window_int(rect_make(point_zero(), screen->resolution), true);
	screen->window->superview = NULL;

	//set up background image
	Bmp* background = load_bmp(screen->window->content_view->frame, "altitude.bmp");
	if (background) {
		add_bmp(screen->window->content_view, background);
	}
	add_status_bar(screen);
	add_taskbar(screen);

	//display_sample_image(point_make(450, 100));
	display_about_window(point_make(100, 200));
	//calculator_xserv(point_make(400, 300));
	display_usage_monitor(point_make(350, 500));
}

static void draw_mouse_shadow(Screen* screen, Point old, Point new) {
	for (float i = 0; i < shadow_count; i++) {
		int lerp_x = lerp(old.x, new.x, (1 / shadow_count) * i);
		int lerp_y = lerp(old.y, new.y, (1 / shadow_count) * i);
		Point shadow_loc = point_make(lerp_x, lerp_y);

		//draw cursor shadow
		draw_rect(screen->vmem, rect_make(shadow_loc, size_make(10, 12)), color_make(200, 200, 230), THICKNESS_FILLED);
		//draw border
		draw_rect(screen->vmem, rect_make(shadow_loc, size_make(10, 12)), color_dark_gray(), 1);
	}
}

static Point last_mouse_pos = { -1, -1 };
void draw_cursor(Screen* screen) {
	//actual cursor bitmap
	static Bmp* cursor = 0;
	static bool tried_loading_cursor = false;

	if (!tried_loading_cursor) {
		//cursor = load_bmp(rect_make(point_zero(), size_make(12, 18)), "cursor.bmp");
		tried_loading_cursor = true;
	}

	//update cursor position
	//cursor->frame.origin = mouse_point();

	//drawing cursor shouldn't change dirtied flag
	//save dirtied flag, draw cursor, and restore it
	char prev_dirtied = dirtied;

	//we do not call add_bmp on the cursor
	//we draw it manually to ensure it is always above all other content
	if (cursor) {
	//	draw_bmp(screen->vmem, cursor);
	}
	else {
		//couldn't load cursor, use backup
		//draw_rect(screen->vmem, rect_make(mouse_point(), size_make(10, 12)), color_green(), THICKNESS_FILLED);
	}

	draw_mouse_shadow(screen, last_mouse_pos, mouse_point());

	dirtied = prev_dirtied;
}

char xserv_draw(Screen* screen) {
	screen->finished_drawing = 0;

	dirtied = 0;
	draw_desktop(screen);
	draw_cursor(screen);

	screen->finished_drawing = 1;

	return (char)dirtied;
}

//recursively checks view hierarchy, returning lowest view bounding point
static View* view_containing_point_sub(View* view, Point p) {
	if (rect_contains_point(view->frame, p)) {
		if (!view->subviews->size) {
			return view;
		}
		//traverse subviews and find one containing this point
		for (int i = 0; i < view->subviews->size; i++) {
			View* subview = (View*)array_m_lookup(view->subviews, i);

			//convert point to subview's coordinate space
			Point converted = p;
			converted.x -= view->frame.origin.x;
			converted.y -= view->frame.origin.y;

			if (rect_contains_point(subview->frame, converted)) {
				return view_containing_point_sub(subview, converted);
			}
		}
		//no subviews contained the point
		//return root window
		return view;
	}
	//this view doesn't bound the point
	return NULL;
}

//uses view_containing_point_sub to try to find a point owner
//if none own click, checks against window's title and content views
static View* view_containing_point(Window* window, Point p) {
	//is this point bounded at all?
	if (!rect_contains_point(window->frame, p)) {
		return NULL;
	}
	
	p.x -= window->frame.origin.x;
	p.y -= window->frame.origin.y;

	//quick check to check if it was in the window's title view
	if (rect_contains_point(window->title_view->frame, p)) {
		return window->title_view;
	}

	//remove title view offset
	Point c = p;
	c.x -= window->title_view->frame.origin.x;
	c.y -= window->title_view->frame.origin.y;

	View* owner = view_containing_point_sub(window->content_view, c);
	if (owner) {
		return owner;
	}
	
	return window->content_view;
}

static Window* window_containing_point(Point p) {
	Screen* screen = gfx_screen();
	//traverse window hierarchy, starting with the topmost window
	for (int i = screen->window->subviews->size - 1; i >= 0; i--) {
		Window* w = (Window*)array_m_lookup(screen->window->subviews, i);
		if (rect_contains_point(w->frame, p)) {
			return w;
		}
	}
	//wasn't in any subwindows
	//point must be within root window
	return screen->window;
}

static void set_active_window(Screen* screen, Window* grabbed_window) {
	active_window = grabbed_window;
	for (int i = 0; i < screen->window->subviews->size; i++) {
		Window* win = array_m_lookup(screen->window->subviews, i);
		Color color;
		if (win == active_window) {
			color = color_make(120, 245, 80);
		}
		else {
			color = color_make(50, 122, 40);
		}
		set_background_color(win->title_view, color);
		mark_needs_redraw((View*)win);
	}
}

static Point world_point_to_owner_space_sub(Point p, View* view) {
	if (rect_contains_point(view->frame, p)) {
		p.x -= view->frame.origin.x;
		p.y -= view->frame.origin.y;

		if (!view->subviews->size) {
			return p;
		}
		//traverse subviews
		for (int i = 0; i < view->subviews->size; i++) {
			View* subview = (View*)array_m_lookup(view->subviews, i);
			if (rect_contains_point(subview->frame, p)) {
				return world_point_to_owner_space_sub(p, subview);
			}
		}
	}
	return p;
}

//converts gloabl point to view's coordinate space
Point world_point_to_owner_space(Point p) {
	Point converted = p;
	Window* win = window_containing_point(p);
	converted.x -= win->frame.origin.x;
	converted.y -= win->frame.origin.y;

	if (rect_contains_point(win->title_view->frame, converted)) {
		return converted;
	}

	return world_point_to_owner_space_sub(converted, win->content_view);
}

static void process_kb_events(Screen* screen) {
	//check if there are any keys pending
	if (!haskey()) return;

	char ch;
	if ((ch = kgetch())) {
		if (ch == 'q') {
			//quit xserv
			xserv_quit(screen);
		}
		else if (ch == 'r') {
			//force everything to refresh
			screen->window->needs_redraw = 1;
			for (int i = 0; i < screen->window->subviews->size; i++) {
				Window* w = array_m_lookup(screen->window->subviews, i);
				w->needs_redraw = 1;
			}
		}
		else if (ch == 'a') {
			//toggle alpha of topmost window between 0.5 and 1.0
			if (screen->window->subviews->size) {
				Window* topmost = array_m_lookup(screen->window->subviews, screen->window->subviews->size - 1);
				float new = 0.5;
				if (topmost->layer->alpha == new) {
					new = 1.0;
				}
				set_alpha((View*)topmost, new);
			}
		}
		else if (ch == 'c') {
			calculator_xserv(point_make(100, 500));
		}
	}
}

static void process_mouse_events(Screen* screen) {
	static uint8_t last_event = 0;

	//get mouse events
	uint8_t events = mouse_events();
	Point p = mouse_point();

	//0th bit is left mouse button
	bool left = events & 0x1;
	//2nd bit is right button
	bool right = events & 0x2;

	//find the window that got this click
	Window* owner = window_containing_point(p);
	//find element within window that owns click
	View* local_owner = view_containing_point(owner, p);
	
	if (left) {
		if (!grabbed_window) {
			//don't move root window! :p
			if (owner != screen->window && owner->layer->alpha > 0.0) {
				set_active_window(screen, owner);

				//bring this window to forefont
				array_m_remove(screen->window->subviews, array_m_index(screen->window->subviews, (type_t)active_window));
				array_m_insert(screen->window->subviews, (type_t)active_window);

				//only move window if title view was selected
				if (local_owner == owner->title_view) {
					grabbed_window = owner;
					last_grabbed_window_pos = grabbed_window->frame.origin;
				}
			}
		}
		else {
			if (last_mouse_pos.x != -1 && grabbed_window != screen->window) {
				//move this window by the difference between current mouse position and last mouse position
				Rect old_frame = grabbed_window->frame;
				Rect new_frame = old_frame;
				new_frame.origin.x -= (last_mouse_pos.x - p.x);
				new_frame.origin.y -= (last_mouse_pos.y - p.y);

				set_frame((View*)grabbed_window, new_frame);
				last_grabbed_window_pos = old_frame.origin;
			}
		}
	}
	else {
		if (grabbed_window) {
			//click event ended, release window
			grabbed_window = NULL;
		}
	}
	
	if (local_owner) {
		for (int j = 0; j < local_owner->buttons->size; j++) {
			//convert point to local coordinate space
			Point conv = world_point_to_owner_space(p);

			Button* b = (Button*)array_m_lookup(local_owner->buttons, j);
			if (rect_contains_point(b->frame, conv)) {
				//only perform mousedown handler if mouse was previously not clicked
				if (left && !(last_event & 0x1)) {
					button_handle_mousedown(b);
				}
				//only perform mouseup handler if mouse was just released
				else if (!left && (last_event & 0x1)) {
					button_handle_mouseup(b);
				}
				break;
			}
		}
	}

	//did a right click just happen?
	if (right && !(last_event & 0x2)) {
		printk("invoking launcher for right click...\n");
		launcher_invoke(p);
	}

	draw_mouse_shadow(screen, last_mouse_pos, p);

	last_mouse_pos = p;
	last_event = events;
}

static Label* fps;
void xserv_refresh(Screen* screen) {
	//if (!screen->finished_drawing) return;
	
	double time_start = time();

	//handle mouse events
	process_mouse_events(screen);
	//keyboard events
	process_kb_events(screen);
	//main refresh loop
	//traverse view hierarchy,
	//redraw views if necessary,
	//composite everything onto root layer
	xserv_draw(screen);

	double frame_time = (time() - time_start) / 1000.0;
	double fps_conv = 1 / frame_time / 10;

	update_all_animations(screen, frame_time);

	//update frame time tracker
	char buf[32];
	itoa(fps_conv, (char*)&buf);
	strcat(buf, " FPS");
	fps->text = buf;
	draw_label(screen->window->layer, fps);

	write_screen(screen);

	dirtied = 0;
}

void xserv_pause() {
	switch_to_text();
}

void xserv_resume() {
	switch_to_vesa(0x118, false);
}

void xserv_temp_stop(uint32_t pause_length) {
	xserv_pause();
	sleep(pause_length);
	xserv_resume();
}

void xserv_init() {
	become_first_responder();
	Screen* screen = gfx_screen();
	desktop_setup(screen);

	//add FPS tracker
	//don't call add_sublabel on fps because it's drawn manually
	//(drawn manually so we can update text with accurate frame draw time)
	fps = create_label(rect_make(point_make(3, 3), size_make(60, 25)), "FPS counter");
	fps->text_color = color_black();

	test_xserv();

	while (1) {
		xserv_refresh(screen);
		mouse_event_wait();
	}

	_kill();
}

void xserv_fail() {
	printk_err("xserv fail");
	Screen* screen = gfx_screen();
	if (!screen) return;

	float rect_width = rect_max_x(screen->window->frame);
	float rect_height = rect_max_y(screen->window->frame) / 8;
	float origin_x = 0;
	float origin_y = (rect_max_y(screen->window->frame) / 2) - (rect_height / 2);
	Rect error_box = rect_make(point_make(origin_x, origin_y), size_make(rect_width, rect_height));

	draw_rect(screen->vmem, error_box, color_red(), THICKNESS_FILLED);
	draw_rect(screen->vmem, error_box, color_black(), 2);
	draw_string(screen->vmem, "CRITICAL ERROR: xserv has died.\naxle will switch back to text in 5 seconds.\nIf this fails, please restart axle.\nError: corrupted heap", point_make(error_box.origin.x + CHAR_WIDTH, error_box.origin.y + CHAR_HEIGHT), color_black(), size_make(CHAR_WIDTH, CHAR_HEIGHT));
	write_screen(screen);
	sleep(5000);
	kernel_end_critical();
}

